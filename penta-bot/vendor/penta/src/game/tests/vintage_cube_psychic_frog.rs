//! Psychic Frog: two mana that eats a hand, eats a graveyard, and draws a
//! card every time it connects.

use super::*;

/// Player One with a Frog that has been out since last turn, `hand` in hand
/// and `graveyard` behind it.
fn staged(hand: &[CardDefinitionId], graveyard: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].graveyard.clear();
    for (zone, definitions) in [(ZoneKind::Hand, hand), (ZoneKind::Graveyard, graveyard)] {
        for definition in definitions {
            let card = game
                .build_zone(PlayerId::One, &[*definition])
                .expect("cataloged")
                .into_iter()
                .next()
                .expect("one card");
            match zone {
                ZoneKind::Hand => game.players[0].hand.push(card),
                _ => game.players[0].graveyard.push(card),
            }
        }
    }
    let frog = game
        .put_onto_battlefield(PlayerId::One, cards::PSYCHIC_FROG)
        .expect("cataloged");
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [2, 1];
    drain_pending(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, frog)
}

fn settle(game: &mut Game) {
    for _ in 0..24 {
        if !game.pending_decisions.is_empty() {
            return;
        }
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
    game.check_state_based_actions();
}

/// Every activation of the Frog on offer right now.
fn activations(game: &Game, frog: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == frog),
        )
        .collect()
}

/// The activation whose ability is the one at `index` on the Frog.
fn activation(game: &Game, frog: GameObjectId, index: u8) -> Option<Action> {
    activations(game, frog).into_iter().find(|action| {
        matches!(
            action,
            Action::ActivateAbility {
                ability: AbilityOrigin::Printed { ability, .. },
                ..
            } if *ability == AbilityId(index)
        )
    })
}

fn the_frog(game: &Game) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::PSYCHIC_FROG)
        .expect("it is on the battlefield")
}

/// Sends the Frog at `defender` and lets combat damage happen.
fn connect(game: &mut Game, frog: GameObjectId, defender: AttackDefender) {
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == frog)
    {
        permanent.attacking = true;
        permanent.attack_defender = Some(defender);
    }
    game.step = Step::DeclareBlockers;
    game.advance_step();
    pass_priority_pair(game);
    settle(game);
}

/// Discarding a card grows it, and the card actually leaves the hand.
#[test]
fn a_discard_puts_a_counter_on_it() {
    let (mut game, frog) = staged(&[cards::MOUNTAIN], &[]);

    let action = activation(&game, frog, 1).expect("a card in hand pays for it");
    game.apply(PlayerId::One, action).expect("it activates");
    settle(&mut game);

    assert_eq!(
        the_frog(&game).counters(CounterKind::PlusOnePlusOne),
        1,
        "one counter",
    );
    assert_eq!(game.power(the_frog(&game)), Some(2), "a 2/3 now");
    assert!(game.players[0].hand.is_empty(), "the card was discarded");
    assert_eq!(
        game.players[0].graveyard.len(),
        1,
        "into its owner's graveyard",
    );
}

/// No mana and no tap: with a hand to spend it grows as often as it likes.
#[test]
fn an_empty_hand_cannot_pay_for_the_counter() {
    let (game, frog) = staged(&[], &[]);

    assert!(
        activation(&game, frog, 1).is_none(),
        "there is nothing to discard",
    );
}

/// Three cards out of the graveyard buy flying for the turn.
#[test]
fn three_graveyard_cards_buy_flying() {
    let (mut game, frog) = staged(&[], &[cards::MOUNTAIN, cards::ISLAND, cards::SWAMP]);
    assert!(
        !game.permanent_has_executable_keyword(the_frog(&game), KeywordAbility::Flying),
        "a Frog does not fly on its own",
    );

    let action = activation(&game, frog, 2).expect("three cards pay for it");
    game.apply(PlayerId::One, action).expect("it activates");
    settle(&mut game);

    assert!(
        game.permanent_has_executable_keyword(the_frog(&game), KeywordAbility::Flying),
        "and now it does",
    );
    assert!(
        game.players[0].graveyard.is_empty(),
        "the three left the graveyard",
    );
    assert_eq!(
        game.players[0].exile.len(),
        3,
        "exiled rather than shuffled away",
    );
}

/// Two is not three: the cost is paid in full or not at all.
#[test]
fn two_graveyard_cards_are_not_enough() {
    let (game, frog) = staged(&[], &[cards::MOUNTAIN, cards::ISLAND]);

    assert!(
        activation(&game, frog, 2).is_none(),
        "a partial payment buys nothing",
    );
}

/// Connecting with a player draws a card.
#[test]
fn combat_damage_to_a_player_draws_a_card() {
    let (mut game, frog) = staged(&[], &[]);
    let before = game.players[0].hand.len();

    connect(&mut game, frog, AttackDefender::Player(PlayerId::Two));

    assert_eq!(game.players[1].life, 19, "the Frog got there");
    assert_eq!(
        game.players[0].hand.len(),
        before + 1,
        "and drew a card for it",
    );
}

/// "Or planeswalker": hitting one draws just the same, which is what makes
/// the clause worth writing as one trigger rather than two.
#[test]
fn combat_damage_to_a_planeswalker_draws_a_card() {
    let (mut game, frog) = staged(&[], &[]);
    let mut walker = creature(10_500, cards::DOMRI_RADE, PlayerId::Two);
    walker.set_counters(CounterKind::Loyalty, 3);
    let walker_id = walker.card.id;
    game.battlefield.push(walker);
    let before = game.players[0].hand.len();

    connect(&mut game, frog, AttackDefender::Planeswalker(walker_id));

    assert_eq!(game.players[1].life, 20, "no player was damaged");
    assert_eq!(
        game.battlefield
            .iter()
            .find(|permanent| permanent.card.id == walker_id)
            .map(|walker| walker.counters(CounterKind::Loyalty)),
        Some(2),
        "the loyalty took it instead",
    );
    assert_eq!(
        game.players[0].hand.len(),
        before + 1,
        "and the Frog drew all the same",
    );
}

/// A creature is neither a player nor a planeswalker: a Frog that gets
/// blocked deals its damage and draws nothing.
#[test]
fn damage_to_a_blocking_creature_draws_nothing() {
    let (mut game, frog) = staged(&[], &[]);
    let mut blocker = creature(10_500, cards::GRIZZLY_BEARS, PlayerId::Two);
    blocker.blocking = vec![frog];
    game.battlefield.push(blocker);
    let before = game.players[0].hand.len();

    connect(&mut game, frog, AttackDefender::Player(PlayerId::Two));

    assert_eq!(game.players[1].life, 20, "nothing got through");
    assert_eq!(
        game.players[0].hand.len(),
        before,
        "and a blocked Frog draws nothing",
    );
}
