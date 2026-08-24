//! Thieving Skydiver: a two-mana flier, or two plus X and the best artifact
//! on the other side of the table comes with him.

use super::*;

/// Player One holding the Skydiver with `mana` blue mana available.
fn staged(mana: u16) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let skydiver = game
        .build_zone(PlayerId::One, &[cards::THIEVING_SKYDIVER])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let id = skydiver.id;
    game.players[0].hand.push(skydiver);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, mana);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, id)
}

fn settle(game: &mut Game) {
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .iter()
                .map(|option| option.id)
                .take(decision.minimum.max(1))
                .collect();
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("the offered choice is legal");
            continue;
        }
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            break;
        }
    }
    game.check_state_based_actions();
}

/// The casts of the Skydiver on offer, as (X, whether it is kicked).
fn casts(game: &Game, card: GameObjectId) -> Vec<(u16, bool)> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::CastSpell {
                card: id, choices, ..
            } if id == card => Some((choices.x(), choices.costs().alternative().is_some())),
            _ => None,
        })
        .collect()
}

fn cast_kicked_for(game: &mut Game, card: GameObjectId, wanted: u16, target: GameObjectId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell {
                card: id, choices, ..
            } => *id == card && choices.x() == wanted,
            _ => false,
        })
        .unwrap_or_else(|| panic!("a kicked cast for X={wanted} is on offer"));
    game.apply(PlayerId::One, action).expect("it is cast");
    // The arrival trigger names the artifact.
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            eprintln!(
                "DECISION {:?} options {:?}",
                decision.prompt,
                decision
                    .options
                    .iter()
                    .map(|o| (o.id, o.label.clone(), o.card))
                    .collect::<Vec<_>>()
            );
            let options: Vec<_> = decision
                .options
                .iter()
                .filter(|option| option.card.is_some_and(|(object, _)| object == target))
                .map(|option| option.id)
                .take(1)
                .collect();
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("the offered choice is legal");
            continue;
        }
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            break;
        }
    }
    settle(game);
}

/// "X can't be 0": the kicked cast is never offered for nothing.
#[test]
fn the_kicker_is_never_free() {
    let (game, skydiver) = staged(4);

    let offered = casts(&game, skydiver);
    assert!(
        offered.contains(&(0, false)),
        "the plain two-mana cast is there: {offered:?}",
    );
    assert!(
        !offered.contains(&(0, true)),
        "and no kicked cast for X of nothing: {offered:?}",
    );
    assert!(
        offered.iter().any(|(x, kicked)| *kicked && *x == 1),
        "the smallest kick is one: {offered:?}",
    );
}

/// Kicked for one, he takes an artifact worth one.
#[test]
fn he_steals_an_artifact_worth_x() {
    let (mut game, skydiver) = staged(3);
    let key = game
        .put_onto_battlefield(PlayerId::Two, cards::MANIFOLD_KEY)
        .expect("cataloged");
    drain_pending(&mut game);
    game.priority = PlayerId::One;

    cast_kicked_for(&mut game, skydiver, 1, key);

    assert_eq!(
        game.battlefield
            .iter()
            .find(|permanent| permanent.card.id == key)
            .expect("it is still on the battlefield")
            .controller,
        PlayerId::One,
        "the artifact changed hands",
    );
}

/// An artifact that costs more than X is not a legal target, so the trigger
/// finds nothing and the artifact stays put.
#[test]
fn an_artifact_worth_more_than_x_is_safe() {
    let (mut game, skydiver) = staged(3);
    let greaves = game
        .put_onto_battlefield(PlayerId::Two, cards::LIGHTNING_GREAVES)
        .expect("cataloged");
    drain_pending(&mut game);
    game.priority = PlayerId::One;

    let kicked_for_one = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(action, Action::CastSpell { card, choices, .. }
                if *card == skydiver && choices.x() == 1)
        })
        .expect("a kick of one is affordable");
    game.apply(PlayerId::One, kicked_for_one)
        .expect("it is cast");
    settle(&mut game);

    assert_eq!(
        game.battlefield
            .iter()
            .find(|permanent| permanent.card.id == greaves)
            .expect("still there")
            .controller,
        PlayerId::Two,
        "a two-mana artifact is out of reach of a one-mana kick",
    );
}

/// An Equipment arrives already attached to him.
#[test]
fn a_stolen_equipment_attaches_itself() {
    let (mut game, skydiver) = staged(4);
    let greaves = game
        .put_onto_battlefield(PlayerId::Two, cards::LIGHTNING_GREAVES)
        .expect("cataloged");
    drain_pending(&mut game);
    game.priority = PlayerId::One;

    cast_kicked_for(&mut game, skydiver, 2, greaves);

    let body = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::THIEVING_SKYDIVER)
        .expect("he resolved");
    assert_eq!(
        game.attached_host(greaves),
        Some(body.card.id),
        "the Equipment came down on him",
    );
    assert!(
        game.permanent_has_executable_keyword(body, KeywordAbility::Haste),
        "and what it grants is his: a stolen Equipment equips nobody else",
    );
}
