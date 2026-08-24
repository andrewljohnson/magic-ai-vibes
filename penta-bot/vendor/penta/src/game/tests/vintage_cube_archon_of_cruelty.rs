//! Archon of Cruelty: eight mana nobody pays, and a six-point swing every
//! time he arrives or attacks.

use super::*;

/// The Archon on the battlefield beside whatever `board` names, with the
/// other player holding a card and a creature.
fn staged() -> Game {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[1].hand.clear();
    game.players[0].library.clear();
    game.players[1].graveyard.clear();
    for definition in [cards::LIGHTNING_BOLT, cards::MOUNTAIN] {
        let card = game
            .build_zone(PlayerId::One, &[definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].library.push(card);
    }
    let their_card = game
        .build_zone(PlayerId::Two, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[1].hand.push(their_card);
    game.players[0].life = 20;
    game.players[1].life = 20;
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game
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

/// Arriving takes a creature, a card, and three life, and pays you a card
/// and three life back.
#[test]
fn arriving_collects_the_whole_toll() {
    let mut game = staged();
    let bears = creature(190_000, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);

    game.put_onto_battlefield(PlayerId::One, cards::ARCHON_OF_CRUELTY)
        .expect("cataloged");
    settle(&mut game);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == bears_id),
        "they gave up a creature",
    );
    assert_eq!(
        game.players[1].graveyard.len(),
        2,
        "the creature and a card"
    );
    assert!(game.players[1].hand.is_empty());
    assert_eq!(game.players[1].life, 17);
    assert_eq!(game.players[0].life, 23);
    assert_eq!(game.players[0].hand.len(), 1, "and you drew");
}

/// He is a flier, and attacking collects the toll again.
#[test]
fn attacking_collects_it_again() {
    let mut game = staged();
    let archon = game
        .put_onto_battlefield(PlayerId::One, cards::ARCHON_OF_CRUELTY)
        .expect("cataloged");
    settle(&mut game);
    game.players[1].life = 20;
    game.players[0].life = 20;
    let their_card = game
        .build_zone(PlayerId::Two, &[cards::MOUNTAIN])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[1].hand.push(their_card);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }

    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == archon)
        .expect("he is there");
    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Flying));

    game.step = Step::DeclareAttackers;
    game.declare_attacker(archon, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    settle(&mut game);

    assert_eq!(
        game.players[1].life, 17,
        "the attack trigger, not the damage"
    );
    assert_eq!(game.players[0].life, 23);
    assert!(game.players[1].hand.is_empty());
}

/// A player with nothing to sacrifice still pays the rest of it.
#[test]
fn an_empty_board_still_pays_the_rest() {
    let mut game = staged();

    game.put_onto_battlefield(PlayerId::One, cards::ARCHON_OF_CRUELTY)
        .expect("cataloged");
    settle(&mut game);

    assert_eq!(game.players[1].life, 17);
    assert!(
        game.players[1].hand.is_empty(),
        "the discard still happened"
    );
    assert_eq!(game.players[0].life, 23);
}
