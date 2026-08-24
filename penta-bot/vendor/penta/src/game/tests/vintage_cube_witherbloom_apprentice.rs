//! Witherbloom Apprentice: a 2/2 that turns a deck full of cheap spells
//! into a clock, two life at a time.

use super::*;

/// Her on the battlefield, with `hand` to cast.
fn staged(hand: &[CardDefinitionId]) -> (Game, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.put_onto_battlefield(PlayerId::One, cards::WITHERBLOOM_APPRENTICE)
        .expect("cataloged");
    drain_pending(&mut game);
    let mut ids = Vec::new();
    for definition in hand {
        let card = game
            .build_zone(PlayerId::One, &[*definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        ids.push(card.id);
        game.players[0].hand.push(card);
    }
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.players[0].life = 20;
    game.players[1].life = 20;
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    for color in ManaColor::COLORS {
        game.add_unrestricted_mana(PlayerId::One, color, 4);
    }
    (game, ids)
}

fn settle(game: &mut Game) {
    for _ in 0..40 {
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

/// Casts it, pointing anything that targets at the opponent so the drain is
/// the only thing this player's own side takes.
fn cast(game: &mut Game, card: GameObjectId) {
    let casts = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::CastSpell { card: cast, .. } if *cast == card))
        .collect::<Vec<_>>();
    let action = casts
        .iter()
        .find(|action| match action {
            Action::CastSpell { choices, .. } => choices
                .targets()
                .iter()
                .any(|slot| slot.targets().contains(&Target::Player(PlayerId::Two))),
            _ => false,
        })
        .or_else(|| casts.first())
        .cloned()
        .expect("there is mana for it");
    game.apply(PlayerId::One, action).expect("it is cast");
    settle(game);
}

/// Casting an instant drains for one.
#[test]
fn casting_an_instant_drains() {
    let (mut game, cards) = staged(&[cards::LIGHTNING_BOLT]);

    cast(&mut game, cards[0]);

    // Three from the Bolt and one from her, and one gained.
    assert_eq!(game.players[1].life, 16);
    assert_eq!(game.players[0].life, 21);
}

/// A sorcery counts the same way.
#[test]
fn casting_a_sorcery_drains() {
    let (mut game, cards) = staged(&[cards::DEMONIC_TUTOR]);

    cast(&mut game, cards[0]);

    assert_eq!(game.players[1].life, 19);
    assert_eq!(game.players[0].life, 21);
}

/// A creature spell is neither an instant nor a sorcery.
#[test]
fn casting_a_creature_does_nothing() {
    let (mut game, cards) = staged(&[cards::GRIZZLY_BEARS]);

    cast(&mut game, cards[0]);

    assert_eq!(game.players[1].life, 20);
    assert_eq!(game.players[0].life, 20);
}

/// Two spells, two drains: it is not once per turn.
#[test]
fn every_spell_drains() {
    let (mut game, cards) = staged(&[cards::LIGHTNING_BOLT, cards::DEMONIC_TUTOR]);

    cast(&mut game, cards[0]);
    cast(&mut game, cards[1]);

    assert_eq!(game.players[1].life, 15, "three, one, and one");
    assert_eq!(game.players[0].life, 22);
}

/// "Or copy": a storm copy is not cast, and it drains all the same. Brain
/// Freeze with one spell already cast this turn makes one copy, so the
/// Apprentice sees the cast and the copy both.
#[test]
fn a_copy_drains_too() {
    let (mut game, cards) = staged(&[cards::LIGHTNING_BOLT, cards::BRAIN_FREEZE]);
    cast(&mut game, cards[0]);
    let after_bolt = game.players[0].life;

    cast(&mut game, cards[1]);

    assert_eq!(
        game.players[0].life,
        after_bolt + 2,
        "one for casting it and one for the storm copy",
    );
}

/// An opponent's instant is not one of yours.
#[test]
fn their_spell_does_nothing() {
    let (mut game, _) = staged(&[]);
    let theirs = game
        .build_zone(PlayerId::Two, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let theirs_id = theirs.id;
    game.players[1].hand.push(theirs);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::Red, 2);
    game.priority = PlayerId::Two;

    let action = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == theirs_id))
        .expect("they have the mana");
    game.apply(PlayerId::Two, action).expect("it is cast");
    settle(&mut game);

    assert_eq!(game.players[0].life, 17, "they pointed it at you");
    assert_eq!(game.players[1].life, 20, "and lost nothing for it");
}
