//! Teferi, Time Raveler: instant speed taken from the other player and
//! handed to you.

use super::*;

/// Teferi on the battlefield under Player One, with an instant in each hand
/// and a sorcery in Player One's, on Player One's turn.
fn staged() -> (Game, GameObjectId, GameObjectId, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[1].hand.clear();
    game.players[0].library.clear();
    for (index, definition) in [cards::MOUNTAIN, cards::FOREST].into_iter().enumerate() {
        let id = 274_000 + u32::try_from(index).expect("two cards");
        game.players[0]
            .library
            .push(card(id, definition, PlayerId::One));
    }
    let teferi = game
        .put_onto_battlefield(PlayerId::One, cards::TEFERI_TIME_RAVELER)
        .expect("cataloged");
    drain_pending(&mut game);
    let mut mine = game
        .build_zone(PlayerId::One, &[cards::LIGHTNING_BOLT, cards::PONDER])
        .expect("cataloged")
        .into_iter();
    let my_bolt = mine.next().expect("the Bolt first");
    let ponder = mine.next().expect("the sorcery second");
    let (my_bolt_id, ponder_id) = (my_bolt.id, ponder.id);
    game.players[0].hand.push(my_bolt);
    game.players[0].hand.push(ponder);
    let their_bolt = game
        .build_zone(PlayerId::Two, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let their_bolt_id = their_bolt.id;
    game.players[1].hand.push(their_bolt);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    for player in [PlayerId::One, PlayerId::Two] {
        for color in ManaColor::COLORS {
            game.add_unrestricted_mana(player, color, 3);
        }
    }
    (game, teferi, my_bolt_id, ponder_id, their_bolt_id)
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

fn can_cast(game: &Game, player: PlayerId, spell: GameObjectId) -> bool {
    game.legal_actions(player)
        .into_iter()
        .any(|action| matches!(action, Action::CastSpell { card, .. } if card == spell))
}

/// Activates Teferi's `index`th ability, naming `wanted` if it asks.
fn activate(game: &mut Game, teferi: GameObjectId, index: u8, wanted: Option<GameObjectId>) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source,
                ability: AbilityOrigin::Printed { ability, .. },
                targets,
                ..
            } => {
                *source == teferi
                    && *ability == AbilityId(index)
                    && match wanted {
                        Some(wanted) => targets
                            .iter()
                            .any(|slot| slot.targets().contains(&Target::Permanent(wanted))),
                        None => targets.iter().all(|slot| slot.targets().is_empty()),
                    }
            }
            _ => false,
        })
        .unwrap_or_else(|| panic!("loyalty ability {index} is activatable"));
    game.apply(PlayerId::One, action).expect("it activates");
    settle(game);
}

/// The other player's instant is stuck: on your turn there is no moment they
/// could cast a sorcery, so there is no moment they can cast anything.
#[test]
fn an_opponent_cannot_cast_at_instant_speed() {
    let (mut game, teferi, _mine, _ponder, theirs) = staged();
    game.priority = PlayerId::Two;

    assert!(
        !can_cast(&game, PlayerId::Two, theirs),
        "their instant waits for their own turn",
    );

    game.battlefield
        .retain(|permanent| permanent.card.id != teferi);
    assert!(
        can_cast(&game, PlayerId::Two, theirs),
        "and it was Teferi holding it back",
    );
}

/// Their own main phase with an empty stack is exactly what the clause still
/// allows.
#[test]
fn an_opponent_may_still_cast_at_sorcery_speed() {
    let (mut game, _teferi, _mine, _ponder, theirs) = staged();
    game.active_player = PlayerId::Two;
    game.priority = PlayerId::Two;

    assert!(can_cast(&game, PlayerId::Two, theirs));

    // A spell already on the stack is not such a moment.
    game.players[1].hand.retain(|card| card.id != theirs);
    let other = game
        .build_zone(PlayerId::Two, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let other_id = other.id;
    game.players[1].hand.push(other);
    let angel_card = game
        .build_zone(PlayerId::Two, &[cards::SERRA_ANGEL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let angel = angel_card.id;
    game.players[1].hand.push(angel_card);
    let cast = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == angel))
        .expect("the Angel is castable at sorcery speed");
    game.apply(PlayerId::Two, cast).expect("it is cast");

    assert!(
        !can_cast(&game, PlayerId::Two, other_id),
        "with their own spell on the stack there is no sorcery moment left",
    );
}

/// The clause names each opponent, so his controller keeps instant speed.
#[test]
fn his_controller_is_not_restricted() {
    let (mut game, _teferi, mine, _ponder, _theirs) = staged();
    game.active_player = PlayerId::Two;
    game.priority = PlayerId::One;

    assert!(can_cast(&game, PlayerId::One, mine));
}

/// The plus lends your sorceries flash, and it outlives the turn it was
/// activated on.
#[test]
fn the_plus_one_casts_sorceries_at_instant_speed() {
    let (mut game, teferi, _mine, ponder, _theirs) = staged();

    assert!(
        can_cast(&game, PlayerId::One, ponder),
        "your own main phase needs no help",
    );

    activate(&mut game, teferi, 1, None);
    game.active_player = PlayerId::Two;
    game.priority = PlayerId::One;

    assert!(
        can_cast(&game, PlayerId::One, ponder),
        "their main phase is a fine time for a sorcery now",
    );
}

/// Without the plus, the same sorcery is stuck where every sorcery is.
#[test]
fn a_sorcery_is_otherwise_stuck_on_your_own_main_phase() {
    let (mut game, _teferi, mine, ponder, _theirs) = staged();
    game.active_player = PlayerId::Two;
    game.priority = PlayerId::One;

    assert!(
        can_cast(&game, PlayerId::One, mine),
        "an instant of yours reaches their turn either way",
    );
    assert!(!can_cast(&game, PlayerId::One, ponder));
}

/// The minus returns a permanent and draws.
#[test]
fn the_minus_three_bounces_and_draws() {
    let (mut game, teferi, _mine, _ponder, _theirs) = staged();
    let bears = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);

    activate(&mut game, teferi, 2, Some(bears));

    assert_eq!(
        game.players[1]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::LIGHTNING_BOLT, cards::GRIZZLY_BEARS],
        "the creature went back to its owner",
    );
    assert_eq!(game.players[0].library.len(), 1, "and you drew");
}

/// "Up to one" means naming nothing is legal, and the card is drawn anyway.
#[test]
fn the_minus_three_draws_with_no_target() {
    let (mut game, teferi, _mine, _ponder, _theirs) = staged();

    activate(&mut game, teferi, 2, None);

    assert_eq!(game.players[0].library.len(), 1, "you drew regardless");
}
