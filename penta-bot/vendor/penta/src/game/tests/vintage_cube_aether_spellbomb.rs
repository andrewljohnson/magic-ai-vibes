//! Aether Spellbomb: an answer when it has to be, and a card when it does
//! not.

use super::*;

/// The Spellbomb on the battlefield with a card to draw.
fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    let drawn = game
        .build_zone(PlayerId::One, &[cards::SERRA_ANGEL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[0].library.push(drawn);
    let spellbomb = game
        .put_onto_battlefield(PlayerId::One, cards::AETHER_SPELLBOMB)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, spellbomb)
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

fn activation(game: &Game, source: GameObjectId, ability: u8) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source: activated,
                ability: AbilityOrigin::Printed { ability: id, .. },
                ..
            } => *activated == source && *id == AbilityId(ability),
            _ => false,
        })
}

/// Blue and the artifact answer a creature.
#[test]
fn it_returns_a_creature_to_hand() {
    let (mut game, spellbomb) = staged();
    let bears = creature(280_000, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 1);

    let bounce = activation(&game, spellbomb, 0).expect("blue pays for it");
    game.apply(PlayerId::One, bounce).expect("it activates");
    settle(&mut game);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == bears_id),
        "the creature left the battlefield",
    );
    assert_eq!(
        game.players[1]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::GRIZZLY_BEARS],
        "and went to its owner's hand",
    );
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == spellbomb),
        "the Spellbomb sacrificed itself to do it",
    );
}

/// Any mana and the artifact draw a card instead.
#[test]
fn it_replaces_itself() {
    let (mut game, spellbomb) = staged();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    let draw = activation(&game, spellbomb, 1).expect("one mana pays for it");
    game.apply(PlayerId::One, draw).expect("it activates");
    settle(&mut game);

    assert_eq!(
        game.players[0]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::SERRA_ANGEL],
    );
    assert!(game.players[0].library.is_empty());
}

/// Colorless mana does not pay for the blue half.
#[test]
fn the_bounce_wants_blue() {
    let (mut game, spellbomb) = staged();
    game.battlefield
        .push(creature(280_100, cards::GRIZZLY_BEARS, PlayerId::Two));
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    assert!(activation(&game, spellbomb, 0).is_none());
    assert!(
        activation(&game, spellbomb, 1).is_some(),
        "though the draw is happy with it",
    );
}

/// With no creature on the battlefield the bounce has nothing to name.
#[test]
fn the_bounce_needs_a_creature() {
    let (mut game, spellbomb) = staged();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 1);

    assert!(activation(&game, spellbomb, 0).is_none());
}
