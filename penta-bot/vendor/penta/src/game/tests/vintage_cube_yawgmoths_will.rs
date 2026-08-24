//! Yawgmoth's Will: the turn played over again out of the graveyard, and an
//! exile clause that stops it being played a third time.

use super::*;

/// Player One holding the Will, with `graveyard` behind them.
fn staged(graveyard: &[CardDefinitionId], mana: u16) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].graveyard.clear();
    for (index, definition) in graveyard.iter().enumerate() {
        let id = 279_000 + u32::try_from(index).expect("a short list");
        game.players[0]
            .graveyard
            .push(card(id, *definition, PlayerId::One));
    }
    let will = game
        .build_zone(PlayerId::One, &[cards::YAWGMOTH_S_WILL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let will_id = will.id;
    game.players[0].hand.push(will);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.players[0].lands_played_this_turn = 0;
    for color in ManaColor::COLORS {
        game.add_unrestricted_mana(PlayerId::One, color, mana);
    }
    (game, will_id)
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

/// Casts the Will and hands priority back.
fn resolve_will(game: &mut Game, will: GameObjectId) {
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == will))
        .expect("three mana casts it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(game);
    game.priority = PlayerId::One;
}

fn graveyard_play(game: &Game, definition: CardDefinitionId) -> Option<Action> {
    let card = game.players[0]
        .graveyard
        .iter()
        .find(|card| card.definition == definition)
        .map(|card| card.id)?;
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card: id, .. } | Action::PlayLand { card: id, .. } => *id == card,
            _ => false,
        })
}

/// A spell in the graveyard becomes castable.
#[test]
fn a_spell_in_the_graveyard_becomes_castable() {
    let (mut game, will) = staged(&[cards::LIGHTNING_BOLT], 3);
    assert!(
        graveyard_play(&game, cards::LIGHTNING_BOLT).is_none(),
        "not before it resolves",
    );

    resolve_will(&mut game, will);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);

    assert!(
        graveyard_play(&game, cards::LIGHTNING_BOLT).is_some(),
        "and afterwards it is",
    );
}

/// A land in the graveyard becomes playable, which is the other half of the
/// permission.
#[test]
fn a_land_in_the_graveyard_becomes_playable() {
    let (mut game, will) = staged(&[cards::MOUNTAIN], 3);

    resolve_will(&mut game, will);

    assert!(
        graveyard_play(&game, cards::MOUNTAIN).is_some(),
        "lands as well as spells",
    );
}

/// The Will itself is exiled rather than binned: its own replacement is in
/// place before it finishes resolving.
#[test]
fn the_will_exiles_itself() {
    let (mut game, will) = staged(&[], 3);

    resolve_will(&mut game, will);

    assert!(game.players[0].graveyard.is_empty(), "not the graveyard");
    assert_eq!(
        game.players[0]
            .exile
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::YAWGMOTH_S_WILL],
    );
}

/// A spell cast afterwards goes to exile too, so nothing is played twice.
#[test]
fn what_would_be_binned_is_exiled_instead() {
    let (mut game, will) = staged(&[], 5);
    let bolt = game
        .build_zone(PlayerId::One, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let bolt_id = bolt.id;
    game.players[0].hand.push(bolt);

    resolve_will(&mut game, will);
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == bolt_id))
        .expect("one red casts it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);

    assert!(
        game.players[0].graveyard.is_empty(),
        "the Bolt did not reach a graveyard either",
    );
    assert_eq!(game.players[0].exile.len(), 2, "both cards are in exile");
}

/// A creature dying under it is exiled: the battlefield exit reads the same
/// effect object.
#[test]
fn a_dying_creature_is_exiled() {
    let (mut game, will) = staged(&[], 3);
    let bears = game
        .put_onto_battlefield(PlayerId::One, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);

    resolve_will(&mut game, will);
    game.damage_target_from(None, Some(Target::Permanent(bears)), 5);
    settle(&mut game);

    assert!(
        game.players[0].graveyard.is_empty(),
        "the Bears went to exile with the Will",
    );
    assert!(
        game.players[0]
            .exile
            .iter()
            .any(|card| card.definition == cards::GRIZZLY_BEARS),
    );
}

/// Both halves last only the turn.
#[test]
fn it_all_ends_with_the_turn() {
    let (mut game, will) = staged(&[cards::LIGHTNING_BOLT], 3);
    resolve_will(&mut game, will);

    game.finish_cleanup();
    settle(&mut game);
    game.priority = PlayerId::One;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);

    assert!(
        graveyard_play(&game, cards::LIGHTNING_BOLT).is_none(),
        "the permission is gone",
    );
    let bears = game
        .put_onto_battlefield(PlayerId::One, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.damage_target_from(None, Some(Target::Permanent(bears)), 5);
    settle(&mut game);

    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::GRIZZLY_BEARS),
        "and so is the exiling",
    );
}
