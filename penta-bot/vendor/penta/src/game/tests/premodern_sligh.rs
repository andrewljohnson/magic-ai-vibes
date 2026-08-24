//! The Sligh cards, and the two mechanics they needed.
//!
//! Barbarian Ring's second ability is gated by threshold, which is a
//! restriction on whether the activation is offered at all rather than on
//! what it does. Goblin Vandal's trigger is an optional payment that trades
//! the swing for an artifact.

use super::*;

fn ready() -> Game {
    let mut game = ready_game();
    game.turn = 5;
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.battlefield.clear();
    game.players[PlayerId::One.index()].hand.clear();
    game.players[PlayerId::One.index()].graveyard.clear();
    game
}

fn settle(game: &mut Game) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .last()
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

/// The Ring's burn is offered only once seven cards are in your graveyard,
/// and the mana ability is offered either way -- threshold gates one clause,
/// not the card.
fn ring_activations(graveyard: usize) -> (usize, usize) {
    let mut game = ready();
    let ring = creature(10_000, cards::BARBARIAN_RING, PlayerId::One);
    let ring_id = ring.card.id;
    game.battlefield.push(ring);
    for index in 0..graveyard {
        game.players[PlayerId::One.index()].graveyard.push(card(
            30_000 + u32::try_from(index).expect("small"),
            cards::GRIZZLY_BEARS,
            PlayerId::One,
        ));
    }
    game.players[PlayerId::One.index()].mana_pool.red = 1;

    let actions = game.legal_actions(PlayerId::One);
    let burn = actions
        .iter()
        .filter(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == ring_id),
        )
        .count();
    let mana = actions
        .iter()
        .filter(
            |action| matches!(action, Action::ActivateManaAbility { source, .. } if *source == ring_id),
        )
        .count();
    (burn, mana)
}

#[test]
fn barbarian_ring_withholds_its_burn_below_threshold() {
    let (burn, mana) = ring_activations(6);
    assert_eq!(burn, 0, "six cards is one short of threshold");
    assert!(mana > 0, "but it still taps for red");
}

#[test]
fn barbarian_ring_offers_its_burn_at_threshold() {
    let (burn, mana) = ring_activations(7);
    assert!(burn > 0, "seven cards turns the land into a burn spell");
    assert!(mana > 0, "and it still taps for red");
}

/// Only your own graveyard counts: filling the opponent's does nothing.
#[test]
fn barbarian_ring_counts_only_your_own_graveyard() {
    let mut game = ready();
    let ring = creature(10_000, cards::BARBARIAN_RING, PlayerId::One);
    let ring_id = ring.card.id;
    game.battlefield.push(ring);
    for index in 0..9 {
        game.players[PlayerId::Two.index()].graveyard.push(card(
            30_000 + index,
            cards::GRIZZLY_BEARS,
            PlayerId::Two,
        ));
    }
    game.players[PlayerId::One.index()].mana_pool.red = 1;

    let burn = game
        .legal_actions(PlayerId::One)
        .iter()
        .filter(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == ring_id),
        )
        .count();
    assert_eq!(burn, 0, "their graveyard is not your threshold");
}

/// And once it is offered, it actually deals its two.
#[test]
fn barbarian_ring_deals_two_and_sacrifices_itself() {
    let mut game = ready();
    let ring = creature(10_000, cards::BARBARIAN_RING, PlayerId::One);
    let ring_id = ring.card.id;
    game.battlefield.push(ring);
    let target = creature(10_001, cards::SERRA_ANGEL, PlayerId::Two);
    let target_id = target.card.id;
    game.battlefield.push(target);
    for index in 0..7 {
        game.players[PlayerId::One.index()].graveyard.push(card(
            30_000 + index,
            cards::GRIZZLY_BEARS,
            PlayerId::One,
        ));
    }
    game.players[PlayerId::One.index()].mana_pool.red = 1;

    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == ring_id
                    && targets
                        .iter()
                        .any(|slot| slot.targets().contains(&Target::Permanent(target_id)))
            }
            _ => false,
        })
        .expect("the Angel can be named");
    game.apply(PlayerId::One, action).expect("it is activated");
    settle(&mut game);

    let damage = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == target_id)
        .map(|permanent| permanent.damage);
    assert_eq!(damage, Some(2), "two damage to the Angel");
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == ring_id),
        "and the land sacrificed itself to do it",
    );
}

/// Connecting with the Vandal offers a trade: pay {R} to break an artifact
/// and deal no combat damage this turn.
#[test]
fn the_vandal_trades_its_damage_for_an_artifact() {
    let mut game = ready();
    let vandal = creature(10_000, cards::GOBLIN_VANDAL, PlayerId::One);
    let vandal_id = vandal.card.id;
    game.battlefield.push(vandal);
    let scroll = creature(10_001, cards::BLACK_VISE, PlayerId::Two);
    let scroll_id = scroll.card.id;
    game.battlefield.push(scroll);
    game.players[PlayerId::One.index()].mana_pool.red = 1;

    let attacking = game.trigger_event_object(
        game.battlefield
            .iter()
            .find(|permanent| permanent.card.id == vandal_id)
            .expect("still there"),
    );
    game.capture_battlefield_triggers(&CommittedTriggerEvent::AttacksAndIsNotBlocked {
        object: attacking,
    });
    settle(&mut game);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == scroll_id),
        "the artifact was destroyed",
    );
    let assigns_none = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == vandal_id)
        .is_some_and(|permanent| {
            game.has_applied_rule(permanent, AppliedRuleDef::AssignsNoCombatDamage)
        });
    assert!(assigns_none, "and the Vandal deals nothing this turn");
}

/// Echo comes due on your next upkeep and only then. The Patrol is put onto
/// the battlefield on one turn, survives that turn, and is asked for its echo
/// at the following upkeep -- after which it is never asked again.
fn patrol_at_upkeep(turns_later: u32, pay: bool) -> Game {
    let mut game = ready();
    let mut patrol = creature(10_000, cards::GOBLIN_PATROL, PlayerId::One);
    patrol.entered_controller_turn = 5;
    game.battlefield.push(patrol);
    game.turns_started[PlayerId::One.index()] = 5 + turns_later;
    game.turn = 5 + turns_later;
    game.step = Step::Upkeep;
    if pay {
        game.players[PlayerId::One.index()].mana_pool.red = 1;
    }

    game.capture_battlefield_triggers(&CommittedTriggerEvent::StepBegins {
        step: crate::TurnStepDef::Upkeep,
        player: PlayerId::One,
    });
    settle(&mut game);
    game
}

fn patrol_survived(game: &Game) -> bool {
    game.battlefield
        .iter()
        .any(|permanent| permanent.card.id == CardInstanceId(10_000))
}

#[test]
fn the_echo_is_not_asked_on_the_turn_it_arrived() {
    // Same turn it entered: the condition is false, so nothing triggers and
    // no payment is demanded.
    let game = patrol_at_upkeep(0, false);
    assert!(patrol_survived(&game), "echo is not due yet");
}

#[test]
fn an_unpaid_echo_sacrifices_the_creature() {
    let game = patrol_at_upkeep(1, false);
    assert!(
        !patrol_survived(&game),
        "with no red available the echo goes unpaid",
    );
}

#[test]
fn a_paid_echo_keeps_the_creature() {
    let game = patrol_at_upkeep(1, true);
    assert!(patrol_survived(&game), "the echo was paid");
    assert_eq!(
        game.players[PlayerId::One.index()].mana_pool.red,
        0,
        "and it cost the red mana",
    );
}

#[test]
fn the_echo_never_comes_due_a_second_time() {
    // Two turns on, with no mana at all: if the condition were still true the
    // Patrol would be sacrificed, so surviving is the whole assertion.
    let game = patrol_at_upkeep(2, false);
    assert!(
        patrol_survived(&game),
        "echo is a one-time cost, not an upkeep tax",
    );
}

/// Fireblast can be cast for six mana or for two Mountains, and the second is
/// what the deck plays it for -- from an empty board on the turn the lands
/// stop mattering.
fn fireblast_casts(mountains: usize, mana: bool) -> (Game, Vec<Action>) {
    let mut game = ready();
    for index in 0..mountains {
        game.battlefield.push(creature(
            10_000 + u32::try_from(index).expect("small"),
            cards::MOUNTAIN,
            PlayerId::One,
        ));
    }
    let fireblast = card(20_000, cards::FIREBLAST, PlayerId::One);
    let fireblast_id = fireblast.id;
    game.players[PlayerId::One.index()].hand.push(fireblast);
    if mana {
        game.players[PlayerId::One.index()].mana_pool.red = 2;
        game.players[PlayerId::One.index()].mana_pool.colorless = 4;
    }

    let casts = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::CastSpell { card, .. } if *card == fireblast_id))
        .collect();
    (game, casts)
}

/// "Any target" enumerates a cast per legal target, so what is asserted is
/// the shape of every offer rather than how many there are.
#[test]
fn fireblast_is_free_with_two_mountains_and_no_mana() {
    let (_, casts) = fireblast_casts(2, false);
    assert!(!casts.is_empty(), "two Mountains pay for it with no mana");
    assert!(
        casts.iter().all(|action| matches!(action,
            Action::CastSpell { choices, sacrifices, .. }
                if choices.costs().alternative().is_some() && sacrifices.len() == 2)),
        "every offer is the free one, and each spends both Mountains",
    );
}

#[test]
fn one_mountain_does_not_pay_for_fireblast() {
    let (_, casts) = fireblast_casts(1, false);
    assert!(casts.is_empty(), "the cost is two Mountains, not one");
}

/// With both routes available each target is offered twice: once for six
/// mana, once for the Mountains.
#[test]
fn fireblast_offers_the_printed_cost_and_the_sacrifice_separately() {
    let (_, casts) = fireblast_casts(2, true);
    let free = casts
        .iter()
        .filter(|action| {
            matches!(action, Action::CastSpell { choices, .. }
                if choices.costs().alternative().is_some())
        })
        .count();
    let paid = casts.len() - free;
    assert!(free > 0, "the free cast is offered");
    assert_eq!(free, paid, "and the printed cost reaches the same targets");
}

/// Paying with Mountains actually spends them and deals the four.
#[test]
fn the_free_fireblast_sacrifices_its_mountains() {
    let (mut game, casts) = fireblast_casts(2, false);
    let cast = casts
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { choices, .. } => choices
                .targets()
                .iter()
                .any(|slot| slot.targets().contains(&Target::Player(PlayerId::Two))),
            _ => false,
        })
        .expect("the opponent can be named");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::MOUNTAIN),
        "both Mountains were sacrificed",
    );
    assert_eq!(
        game.players[PlayerId::Two.index()].life,
        16,
        "and the opponent took four",
    );
}

/// The Lavamancer spends two cards per shot, and the player picks which two,
/// so each pair is its own offered activation.
fn lavamancer_with_graveyard(cards_in_graveyard: usize) -> (Game, Vec<Action>) {
    let mut game = ready();
    let lavamancer = creature(10_000, cards::GRIM_LAVAMANCER, PlayerId::One);
    let lavamancer_id = lavamancer.card.id;
    game.battlefield.push(lavamancer);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    for index in 0..cards_in_graveyard {
        game.players[PlayerId::One.index()].graveyard.push(card(
            30_000 + u32::try_from(index).expect("small"),
            cards::GRIZZLY_BEARS,
            PlayerId::One,
        ));
    }
    game.players[PlayerId::One.index()].mana_pool.red = 1;

    let shots = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| {
            matches!(action, Action::ActivateAbility { source, .. } if *source == lavamancer_id)
        })
        .collect();
    (game, shots)
}

#[test]
fn one_card_in_the_graveyard_does_not_pay_for_the_lavamancer() {
    let (_, shots) = lavamancer_with_graveyard(1);
    assert!(shots.is_empty(), "the cost is two cards, not one");
}

/// With three cards there are three pairs, and "any target" doubles each --
/// what matters is that every offer names exactly two distinct cards.
#[test]
fn the_lavamancer_offers_every_pair_it_could_exile() {
    let (_, shots) = lavamancer_with_graveyard(3);
    assert!(!shots.is_empty(), "two cards pay for it");
    assert!(
        shots.iter().all(|action| matches!(action,
            Action::ActivateAbility { cost_objects, .. }
                if cost_objects.len() == 2 && cost_objects[0] != cost_objects[1])),
        "every offer spends two distinct cards",
    );
    let pairs: std::collections::BTreeSet<Vec<GameObjectId>> = shots
        .iter()
        .filter_map(|action| match action {
            Action::ActivateAbility { cost_objects, .. } => Some(cost_objects.clone()),
            _ => None,
        })
        .collect();
    assert_eq!(pairs.len(), 3, "three cards make three pairs");
}

/// And activating one really exiles both and deals the two.
#[test]
fn the_lavamancer_exiles_both_cards_and_deals_two() {
    let (mut game, shots) = lavamancer_with_graveyard(2);
    let shot = shots
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility { targets, .. } => targets
                .iter()
                .any(|slot| slot.targets().contains(&Target::Player(PlayerId::Two))),
            _ => false,
        })
        .expect("the opponent can be named");
    game.apply(PlayerId::One, shot).expect("it is activated");
    settle(&mut game);

    assert!(
        game.players[PlayerId::One.index()].graveyard.is_empty(),
        "both cards left the graveyard",
    );
    assert_eq!(
        game.players[PlayerId::One.index()].exile.len(),
        2,
        "and both went to exile rather than anywhere else",
    );
    assert_eq!(game.players[PlayerId::Two.index()].life, 18, "two damage");
}

/// Cursed Scroll names a card and then reveals one at random. With a hand of
/// one card the name is certain to match; with a hand of one *other* card it
/// is certain to miss. Both are settled by the hand, not by the RNG, which is
/// what makes them assertable.
fn cursed_scroll_shot(hand: &[CardDefinitionId], name_index: usize) -> Game {
    let mut game = ready();
    let scroll = creature(10_000, cards::CURSED_SCROLL, PlayerId::One);
    let scroll_id = scroll.card.id;
    game.battlefield.push(scroll);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    for (index, definition) in hand.iter().enumerate() {
        game.players[PlayerId::One.index()].hand.push(card(
            20_000 + u32::try_from(index).expect("small"),
            *definition,
            PlayerId::One,
        ));
    }
    game.players[PlayerId::One.index()].mana_pool.colorless = 3;

    let shot = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == scroll_id
                    && targets
                        .iter()
                        .any(|slot| slot.targets().contains(&Target::Player(PlayerId::Two)))
            }
            _ => false,
        })
        .expect("the Scroll can shoot the opponent");
    game.apply(PlayerId::One, shot).expect("it is activated");

    // The ability resolves off the stack before it asks anything, so let it
    // get there and then answer the naming decision.
    for _ in 0..8 {
        if !game.pending_decisions.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
    // A hand with only one name to give has nothing to ask about, so the
    // decision appears only when there is a real choice.
    if let Some(decision) = game
        .pending_decisions
        .first()
        .map(|pending| pending.observation.clone())
    {
        let option = decision.options[name_index].id;
        game.apply(
            decision.player,
            Action::ChooseDecision {
                decision: decision.id,
                options: vec![option],
            },
        )
        .expect("the name is accepted");
    }
    settle(&mut game);
    game
}

#[test]
fn cursed_scroll_hits_when_the_hand_holds_only_the_named_card() {
    let game = cursed_scroll_shot(&[cards::GRIZZLY_BEARS], 0);
    assert_eq!(
        game.players[PlayerId::Two.index()].life,
        18,
        "the only card in hand is the named one, so the reveal must match",
    );
}

/// Two copies of the same card: whichever is revealed, the name matches.
#[test]
fn cursed_scroll_matches_a_second_copy_of_the_named_card() {
    let game = cursed_scroll_shot(&[cards::GRIZZLY_BEARS, cards::GRIZZLY_BEARS], 0);
    assert_eq!(
        game.players[PlayerId::Two.index()].life,
        18,
        "a second copy of the named card is still the named card",
    );
}

/// The reveal is public, so the opponent learns a card either way.
#[test]
fn cursed_scroll_reveals_the_card_it_drew() {
    let game = cursed_scroll_shot(&[cards::GRIZZLY_BEARS], 0);
    assert!(
        game.events.iter().any(|event| matches!(
            event,
            GameEvent::CardRevealed { definition, .. } if *definition == cards::GRIZZLY_BEARS
        )),
        "the randomly chosen card was revealed to everyone",
    );
}

/// The comparison has to be able to fail, and a two-card hand proves it
/// without depending on which card the seed picks: the reveal is the same in
/// both runs, so naming one card and then the other must hit exactly once.
#[test]
fn cursed_scroll_misses_when_the_named_card_is_not_the_revealed_one() {
    let named_first = cursed_scroll_shot(&[cards::GRIZZLY_BEARS, cards::SAVANNAH_LIONS], 0);
    let named_second = cursed_scroll_shot(&[cards::GRIZZLY_BEARS, cards::SAVANNAH_LIONS], 1);
    let damage = |game: &Game| 20 - game.players[PlayerId::Two.index()].life;

    assert_eq!(
        damage(&named_first) + damage(&named_second),
        2,
        "exactly one of the two names matches the card the seed revealed",
    );
    assert!(
        damage(&named_first) == 0 || damage(&named_second) == 0,
        "and the other names a card that was not revealed, so it deals nothing",
    );
}
