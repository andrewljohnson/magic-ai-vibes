//! Displacer Kitten: a 2/2 that does nothing on its own and turns every
//! noncreature spell into another enter trigger.

use super::*;

/// The Kitten out since last turn, with `hand` to cast and `others` on the
/// battlefield beside her.
fn staged(hand: &[CardDefinitionId], others: &[CardDefinitionId]) -> (Game, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.put_onto_battlefield(PlayerId::One, cards::DISPLACER_KITTEN)
        .expect("cataloged");
    let mut ids = Vec::new();
    for definition in others {
        ids.push(
            game.put_onto_battlefield(PlayerId::One, *definition)
                .expect("cataloged"),
        );
    }
    drain_pending(&mut game);
    settle(&mut game);
    for definition in hand {
        let card = game
            .build_zone(PlayerId::One, &[*definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].hand.push(card);
    }
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
        permanent.tapped = false;
    }
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 8);
    (game, ids)
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

/// Casts the one card of `definition` in hand, pointing the Kitten's trigger
/// at `wanted` if it fires.
fn cast(game: &mut Game, definition: CardDefinitionId, wanted: Option<GameObjectId>) {
    let card = game.players[0]
        .hand
        .iter()
        .find(|card| card.definition == definition)
        .expect("it is in hand")
        .id;
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card: cast, .. } if *cast == card))
        .expect("there is mana for it");
    game.apply(PlayerId::One, action).expect("it is cast");
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options: Vec<_> = decision
                .options
                .iter()
                .filter(|option| {
                    wanted.is_some_and(|wanted| {
                        option.card.is_some_and(|(object, _)| object == wanted)
                    })
                })
                .map(|option| option.id)
                .take(1)
                .collect();
            let options = if options.len() < decision.minimum {
                decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum)
                    .collect()
            } else {
                options
            };
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

fn permanent_of(game: &Game, definition: CardDefinitionId) -> Option<&Permanent> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == ObjectKind::Card(definition))
}

/// A noncreature spell blinks what it names: a new object, still on the
/// battlefield.
#[test]
fn a_noncreature_spell_blinks_a_permanent() {
    let (mut game, others) = staged(&[cards::ANCESTRAL_RECALL], &[cards::MANIFOLD_KEY]);
    let key = others[0];

    cast(&mut game, cards::ANCESTRAL_RECALL, Some(key));

    let returned = permanent_of(&game, cards::MANIFOLD_KEY).expect("it came back");
    assert_ne!(returned.card.id, key, "a new object");
}

/// The blink is a real one: the permanent's own enter trigger fires again,
/// which is the whole reason the Kitten is worth four mana.
#[test]
fn the_blinked_permanent_enters_again() {
    let (mut game, others) = staged(&[cards::ANCESTRAL_RECALL], &[cards::COVETED_JEWEL]);
    let jewel = others[0];
    let before = game.players[0].hand.len();

    cast(&mut game, cards::ANCESTRAL_RECALL, Some(jewel));

    // Three from the Recall, three from the Jewel arriving a second time,
    // less the card that was cast.
    assert_eq!(game.players[0].hand.len(), before - 1 + 3 + 3);
    assert_ne!(
        permanent_of(&game, cards::COVETED_JEWEL)
            .expect("it came back")
            .card
            .id,
        jewel,
    );
}

/// A creature spell is not a noncreature spell, so nothing is blinked.
#[test]
fn a_creature_spell_does_not_trigger_it() {
    let (mut game, others) = staged(&[cards::GRIZZLY_BEARS], &[cards::MANIFOLD_KEY]);
    let key = others[0];
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 4);

    cast(&mut game, cards::GRIZZLY_BEARS, Some(key));

    assert_eq!(
        permanent_of(&game, cards::MANIFOLD_KEY)
            .expect("it never moved")
            .card
            .id,
        key,
        "the same object it always was",
    );
}

/// "Nonland": a land you control is not a legal target for it.
#[test]
fn it_cannot_blink_a_land() {
    let (mut game, others) = staged(&[cards::ANCESTRAL_RECALL], &[cards::FOREST]);
    let forest = others[0];
    let card = game.players[0].hand[0].id;
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card: cast, .. } if *cast == card))
        .expect("there is mana for it");
    game.apply(PlayerId::One, action).expect("it is cast");

    for _ in 0..8 {
        if !game.pending_decisions.is_empty() {
            break;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            break;
        }
    }
    let decision = game
        .pending_decisions
        .first()
        .map(|pending| pending.observation.clone())
        .expect("the trigger asks for a target");
    assert!(
        decision
            .options
            .iter()
            .all(|option| option.card.is_none_or(|(object, _)| object != forest)),
        "the land is not on offer",
    );
}

/// "Up to one": with nothing named the trigger simply does nothing, and the
/// spell still resolves.
#[test]
fn it_may_name_nothing() {
    let (mut game, _) = staged(&[cards::ANCESTRAL_RECALL], &[]);
    let before = game.players[0].hand.len();

    cast(&mut game, cards::ANCESTRAL_RECALL, None);

    assert_eq!(
        game.players[0].hand.len(),
        before - 1 + 3,
        "the spell drew its three either way",
    );
}
