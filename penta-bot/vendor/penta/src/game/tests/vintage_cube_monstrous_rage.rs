//! Monstrous Rage: three power and trample this turn, two of which stay.

use super::*;

/// Player One holding the spell, with a Grizzly Bears out and a red mana.
fn staged(copies: usize) -> (Game, Vec<GameObjectId>, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let bears = game
        .put_onto_battlefield(PlayerId::One, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);
    let spells: Vec<_> = game
        .build_zone(PlayerId::One, &vec![cards::MONSTROUS_RAGE; copies])
        .expect("cataloged")
        .into_iter()
        .map(|card| {
            let id = card.id;
            game.players[0].hand.push(card);
            id
        })
        .collect();
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(
        PlayerId::One,
        ManaColor::Red,
        u16::try_from(copies).expect("a short list"),
    );
    (game, spells, bears)
}

fn settle(game: &mut Game) {
    for _ in 0..32 {
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

fn cast(game: &mut Game, spell: GameObjectId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == spell))
        .expect("one red casts it");
    game.apply(PlayerId::One, action).expect("it is cast");
    settle(game);
}

fn roles(game: &Game) -> Vec<GameObjectId> {
    game.battlefield
        .iter()
        .filter(|permanent| permanent.card.definition == ObjectKind::Token)
        .map(|permanent| permanent.card.id)
        .collect()
}

/// The turn's pump and the Role's, on a creature that keeps only one of
/// them.
#[test]
fn it_pumps_now_and_leaves_a_role_behind() {
    let (mut game, spells, bears) = staged(1);

    cast(&mut game, spells[0]);

    let creature = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == bears)
        .expect("the Bears are there");
    assert_eq!(game.power(creature), Some(5), "2 base, +2 now, +1 from it");
    assert_eq!(game.toughness(creature), Some(3));
    assert!(game.permanent_has_executable_keyword(creature, KeywordAbility::Trample));

    let role = roles(&game);
    assert_eq!(role.len(), 1, "one Role token");
    let token = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == role[0])
        .expect("still there");
    assert_eq!(
        token.attached_to,
        Some(bears),
        "created attached to the creature it named",
    );
}

/// The +2/+0 lapses with the turn; the Role does not.
#[test]
fn the_role_outlives_the_turn() {
    let (mut game, spells, bears) = staged(1);
    cast(&mut game, spells[0]);

    game.finish_cleanup();
    settle(&mut game);

    let creature = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == bears)
        .expect("the Bears are there");
    assert_eq!(game.power(creature), Some(3), "only the Role's +1 is left");
    assert_eq!(game.toughness(creature), Some(3));
    assert!(game.permanent_has_executable_keyword(creature, KeywordAbility::Trample));
    assert_eq!(roles(&game).len(), 1);
}

/// The Role rule: a second Role from the same player on the same creature
/// sends the first to the graveyard, and the newest is the one that stays.
#[test]
fn a_second_role_replaces_the_first() {
    let (mut game, spells, bears) = staged(2);
    cast(&mut game, spells[0]);
    let first = roles(&game).into_iter().next().expect("one Role");

    cast(&mut game, spells[1]);

    let remaining = roles(&game);
    assert_eq!(remaining.len(), 1, "two Roles cannot share a creature");
    assert_ne!(remaining[0], first, "and the newer one is the one kept");
    let creature = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == bears)
        .expect("the Bears are there");
    assert_eq!(
        game.power(creature),
        Some(7),
        "both pumps this turn, one Role",
    );
}

/// Its host leaving takes the Role with it, the way any Aura goes.
#[test]
fn the_role_dies_with_its_host() {
    let (mut game, spells, bears) = staged(1);
    cast(&mut game, spells[0]);
    assert_eq!(roles(&game).len(), 1);

    game.battlefield
        .retain(|permanent| permanent.card.id != bears);
    game.check_state_based_actions();

    assert!(
        roles(&game).is_empty(),
        "an Aura with no host does not stay",
    );
}
