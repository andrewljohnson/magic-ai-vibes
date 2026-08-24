//! Memory Lapse: two mana that buys a turn rather than a card.

use super::*;

/// Player Two holding a spell to cast, Player One holding the Lapse.
fn staged(theirs: CardDefinitionId) -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[1].hand.clear();
    game.players[1].library.clear();
    let lapse = game
        .build_zone(PlayerId::One, &[cards::MEMORY_LAPSE])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let lapse_id = lapse.id;
    game.players[0].hand.push(lapse);
    let spell = game
        .build_zone(PlayerId::Two, &[theirs])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let spell_id = spell.id;
    game.players[1].hand.push(spell);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::Two;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::Two;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);
    for color in ManaColor::COLORS {
        game.add_unrestricted_mana(PlayerId::Two, color, 4);
    }
    (game, lapse_id, spell_id)
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

/// Player Two casts their spell; Player One answers it with the Lapse.
fn cast_and_answer(game: &mut Game, lapse: GameObjectId, spell: GameObjectId) {
    let cast = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == spell))
        .expect("they have the mana");
    game.apply(PlayerId::Two, cast).expect("it is cast");

    // The spell is on the stack; the window to answer it opens once
    // priority reaches the other player.
    for _ in 0..4 {
        if game.priority == PlayerId::One {
            break;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            break;
        }
    }
    let answer = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == lapse))
        .expect("the Lapse can answer it");
    game.apply(PlayerId::One, answer).expect("it is cast");
    settle(game);
}

/// The spell is countered, and its card goes to the top of its owner's
/// library rather than to their graveyard.
#[test]
fn the_countered_card_goes_on_top_of_the_library() {
    let (mut game, lapse, spell) = staged(cards::SERRA_ANGEL);

    cast_and_answer(&mut game, lapse, spell);

    assert!(game.battlefield.is_empty(), "the creature never resolved");
    assert_eq!(
        game.players[1].library.last().map(|card| card.definition),
        Some(cards::SERRA_ANGEL),
        "on top of their library",
    );
    assert!(
        game.players[1].graveyard.is_empty(),
        "and not in their graveyard",
    );
}

/// The Lapse itself is an ordinary spell and goes to its own graveyard.
#[test]
fn the_lapse_goes_to_its_own_graveyard() {
    let (mut game, lapse, spell) = staged(cards::SERRA_ANGEL);

    cast_and_answer(&mut game, lapse, spell);

    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::MEMORY_LAPSE),
    );
}

/// It counters an instant the same way: what it names is any spell.
#[test]
fn it_counters_an_instant_too() {
    let (mut game, lapse, spell) = staged(cards::ANCESTRAL_RECALL);
    let hand = game.players[0].hand.len();

    cast_and_answer(&mut game, lapse, spell);

    assert_eq!(
        game.players[1].library.last().map(|card| card.definition),
        Some(cards::ANCESTRAL_RECALL),
    );
    assert_eq!(
        game.players[0].hand.len(),
        hand - 1,
        "and nobody drew from it",
    );
}

/// They draw it again next turn, which is the whole shape of the card: a
/// turn bought rather than a card taken.
#[test]
fn they_draw_it_again() {
    let (mut game, lapse, spell) = staged(cards::SERRA_ANGEL);
    cast_and_answer(&mut game, lapse, spell);
    assert!(game.players[1].hand.is_empty());

    game.draw_cards(PlayerId::Two, 1);

    assert!(
        game.players[1]
            .hand
            .iter()
            .any(|card| card.definition == cards::SERRA_ANGEL),
        "the same card, one turn later",
    );
}
