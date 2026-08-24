//! Necromancy: reanimation on a leash, and the price of casting it early.

use super::*;

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
                .find(|option| option.label != "Decline")
                .or_else(|| decision.options.first())
                .map(|option| vec![option.id])
                .unwrap_or_default();
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("the decision accepts what it offered");
            continue;
        }
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
}

/// A game with an Angel in the opponent's graveyard and Necromancy in hand.
fn staged() -> (Game, CardInstanceId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[1].graveyard.clear();
    game.players[1]
        .graveyard
        .push(card(88_000, cards::SERRA_ANGEL, PlayerId::Two));
    let necromancy = card(88_001, cards::NECROMANCY, PlayerId::One);
    let necromancy_id = necromancy.id;
    game.players[0].hand.push(necromancy);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);
    (game, necromancy_id)
}

fn cast(game: &mut Game, necromancy: CardInstanceId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == necromancy))
        .expect("three mana casts it");
    game.apply(PlayerId::One, action).expect("it is cast");
    settle(game);
    drain_pending(game);
}

fn angel(game: &Game) -> Option<&Permanent> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::SERRA_ANGEL)
}

fn enchantment(game: &Game) -> Option<&Permanent> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::NECROMANCY)
}

/// It reanimates out of any graveyard, under your control, with the
/// enchantment attached to what it brought back.
#[test]
fn it_reanimates_under_your_control_and_attaches() {
    let (mut game, necromancy) = staged();

    cast(&mut game, necromancy);

    let angel = angel(&game).expect("the Angel is on the battlefield");
    assert_eq!(angel.controller, PlayerId::One, "under your control");
    let angel_id = angel.card.id;
    let enchantment = enchantment(&game).expect("the enchantment stayed");
    assert_eq!(
        enchantment.attached_to,
        Some(angel_id),
        "and it is attached to what it brought back",
    );
}

/// The enchantment leaving takes the creature with it.
#[test]
fn the_creature_is_sacrificed_when_the_enchantment_leaves() {
    let (mut game, necromancy) = staged();
    cast(&mut game, necromancy);
    let enchantment_id = enchantment(&game).expect("it is there").card.id;

    game.move_permanents_to_graveyard(&[enchantment_id]);
    settle(&mut game);
    drain_pending(&mut game);

    assert!(angel(&game).is_none(), "the creature went with it");
    assert!(
        game.players[1]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::SERRA_ANGEL),
        "back to its owner's graveyard",
    );
}

/// Cast on your own main phase there is no drawback at all.
#[test]
fn a_sorcery_speed_cast_has_no_cleanup_sacrifice() {
    let (mut game, necromancy) = staged();
    cast(&mut game, necromancy);

    game.capture_battlefield_triggers(&CommittedTriggerEvent::StepBegins {
        step: TurnStepDef::Cleanup,
        player: PlayerId::One,
    });
    game.finish_rules_procedure();
    settle(&mut game);
    drain_pending(&mut game);

    assert!(enchantment(&game).is_some(), "it stays");
    assert!(angel(&game).is_some(), "and so does the creature");
}

/// Cast when a sorcery could not have been, it gives everything back at the
/// next cleanup step.
#[test]
fn an_instant_speed_cast_gives_it_all_back_at_cleanup() {
    let (mut game, necromancy) = staged();
    // Their turn, so a sorcery could not have been cast.
    game.active_player = PlayerId::Two;
    game.priority = PlayerId::One;

    cast(&mut game, necromancy);
    assert!(angel(&game).is_some(), "it still reanimates");

    game.capture_battlefield_triggers(&CommittedTriggerEvent::StepBegins {
        step: TurnStepDef::Cleanup,
        player: PlayerId::Two,
    });
    game.finish_rules_procedure();
    settle(&mut game);
    drain_pending(&mut game);

    assert!(enchantment(&game).is_none(), "the enchantment is gone");
    assert!(angel(&game).is_none(), "and it took the creature with it");
}

/// And the other direction: the creature leaving leaves the enchantment
/// attached to nothing, which state-based actions bin.
#[test]
fn the_enchantment_falls_off_when_the_creature_leaves() {
    let (mut game, necromancy) = staged();
    cast(&mut game, necromancy);
    let angel_id = angel(&game).expect("it is there").card.id;

    game.move_permanents_to_graveyard(&[angel_id]);
    settle(&mut game);
    drain_pending(&mut game);
    game.check_state_based_actions();

    assert!(
        enchantment(&game).is_none(),
        "an Aura attached to nothing goes to the graveyard",
    );
}

/// It is not an Aura before its own trigger attaches it, so nothing bins it
/// in the window between entering and reanimating.
#[test]
fn it_survives_the_window_before_it_attaches() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[1].graveyard.clear();
    let necromancy = card(88_010, cards::NECROMANCY, PlayerId::One);
    let necromancy_id = necromancy.id;
    game.players[0].hand.push(necromancy);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);

    cast(&mut game, necromancy_id);

    assert!(
        enchantment(&game).is_some(),
        "no creature card to reanimate, and it still stands",
    );
}
