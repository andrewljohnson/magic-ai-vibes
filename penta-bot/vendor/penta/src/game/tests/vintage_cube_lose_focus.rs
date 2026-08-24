//! Lose Focus: a soft counter that stops being soft once there is spare
//! mana, and replicate — an additional cost paid as many times as you like.

use super::*;

/// Player Two casting a spell, Player One holding the answer with `mana`
/// blue to pay for it.
fn staged(mana: u16) -> (Game, GameObjectId, GameObjectId) {
    staged_with_their_mana(mana, 5)
}

/// The same, with the other player's spare mana named: the Angel costs five,
/// and what is left over is what they can pay a ransom with.
fn staged_with_their_mana(mana: u16, spare: u16) -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[1].hand.clear();
    game.players[1].graveyard.clear();
    let focus = game
        .build_zone(PlayerId::One, &[cards::LOSE_FOCUS])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let focus_id = focus.id;
    game.players[0].hand.push(focus);
    let theirs = game
        .build_zone(PlayerId::Two, &[cards::SERRA_ANGEL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let theirs_id = theirs.id;
    game.players[1].hand.push(theirs);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::Two;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::Two;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, mana);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::White, 5);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::Colorless, spare);
    (game, focus_id, theirs_id)
}

/// Answers everything, taking the option labelled `wanted` where it is
/// offered and the first otherwise.
fn settle(game: &mut Game, wanted: &str) {
    for _ in 0..48 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options: Vec<_> = decision
                .options
                .iter()
                .find(|option| option.label == wanted)
                .map(|option| option.id)
                .into_iter()
                .collect();
            let options = if options.len() < decision.minimum.max(1) {
                decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum.max(1))
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

/// Their spell goes on the stack and priority comes back to Player One.
fn cast_theirs(game: &mut Game, theirs: GameObjectId) {
    let cast = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == theirs))
        .expect("they have the mana");
    game.apply(PlayerId::Two, cast)
        .expect("their spell is cast");
    for _ in 0..4 {
        if game.priority == PlayerId::One {
            break;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            break;
        }
    }
}

/// Every way Player One could cast the Focus, by how many additional costs
/// each pays.
fn replicate_counts(game: &Game, focus: GameObjectId) -> Vec<usize> {
    let mut counts: Vec<_> = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::CastSpell { card, choices, .. } if card == focus => {
                Some(choices.costs().additional().len())
            }
            _ => None,
        })
        .collect();
    counts.sort_unstable();
    counts.dedup();
    counts
}

/// Casts the Focus paying `replicates` additional costs.
fn cast_focus(game: &mut Game, focus: GameObjectId, replicates: usize) {
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == focus && choices.costs().additional().len() == replicates
            }
            _ => false,
        })
        .unwrap_or_else(|| panic!("{replicates} replicates is payable"));
    game.apply(PlayerId::One, cast).expect("it is cast");
}

/// Two mana casts it and buys nothing; every further blue is another
/// replicate on offer.
#[test]
fn replicate_is_offered_once_for_each_spare_blue() {
    let (mut game, focus, theirs) = staged(4);
    cast_theirs(&mut game, theirs);

    assert_eq!(
        replicate_counts(&game, focus),
        vec![0, 1, 2],
        "two mana for the spell, and two spare for two replicates",
    );
}

/// With nothing spare, only the plain cast is on offer.
#[test]
fn no_spare_mana_means_no_replicate() {
    let (mut game, focus, theirs) = staged(2);
    cast_theirs(&mut game, theirs);

    assert_eq!(replicate_counts(&game, focus), vec![0]);
}

/// Unreplicated, it is one counter: paying {2} keeps the spell.
#[test]
fn unreplicated_it_is_one_soft_counter() {
    let (mut game, focus, theirs) = staged(2);
    cast_theirs(&mut game, theirs);

    cast_focus(&mut game, focus, 0);
    settle(&mut game, "Pay the cost");

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == ObjectKind::Card(cards::SERRA_ANGEL)),
        "they paid the two and kept it",
    );
}

/// Replicated once, there are two of it: the first payment is answerable and
/// the copy asks again.
#[test]
fn replicating_it_makes_a_copy_that_asks_again() {
    // Five for the Angel and two spare: enough for one ransom, not two.
    let (mut game, focus, theirs) = staged_with_their_mana(3, 2);
    cast_theirs(&mut game, theirs);

    cast_focus(&mut game, focus, 1);
    settle(&mut game, "Pay the cost");

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.definition == ObjectKind::Card(cards::SERRA_ANGEL)),
        "five mana pays one ransom, not two",
    );
    assert_eq!(
        game.players[1]
            .graveyard
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::SERRA_ANGEL],
        "the countered spell is in their graveyard",
    );
}

/// The copies are copies: only the card itself reaches a graveyard.
#[test]
fn only_the_card_goes_to_the_graveyard() {
    let (mut game, focus, theirs) = staged(4);
    cast_theirs(&mut game, theirs);

    cast_focus(&mut game, focus, 2);
    settle(&mut game, "Decline");

    assert_eq!(
        game.players[0]
            .graveyard
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::LOSE_FOCUS],
        "three Lose Focuses resolved and one card was ever there",
    );
}
