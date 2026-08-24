//! Brazen Borrower // Petty Theft: bounce something on their turn, then
//! flash in the body it came back on.

use super::*;

/// Player One holding the card, on Player Two's turn.
fn staged(mana: u16) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[1].hand.clear();
    let card = game
        .build_zone(PlayerId::One, &[cards::BRAZEN_BORROWER])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let card_id = card.id;
    game.players[0].hand.push(card);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::Two;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, mana);
    (game, card_id)
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

fn cast_with(game: &Game, card: GameObjectId, option: PlayOptionId) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell {
                card: id, choices, ..
            } => *id == card && choices.play_option() == option,
            _ => false,
        })
}

/// Flash on the creature half and an instant on the other: both halves reach
/// the other player's turn.
#[test]
fn both_halves_are_castable_on_their_turn() {
    let (mut game, card) = staged(3);
    // Petty Theft has to have something of theirs to name.
    game.put_onto_battlefield(PlayerId::Two, cards::SERRA_ANGEL)
        .expect("cataloged");
    drain_pending(&mut game);

    assert!(
        cast_with(&game, card, PlayOptionId::DEFAULT).is_some(),
        "flash brings the Faerie",
    );
    assert!(
        cast_with(&game, card, PlayOptionId(1)).is_some(),
        "and the Adventure is an instant",
    );
}

/// Petty Theft returns their permanent and exiles itself for later.
#[test]
fn petty_theft_bounces_and_goes_on_an_adventure() {
    let (mut game, card) = staged(2);
    game.put_onto_battlefield(PlayerId::Two, cards::SERRA_ANGEL)
        .expect("cataloged");
    drain_pending(&mut game);

    let theft = cast_with(&game, card, PlayOptionId(1)).expect("two mana casts it");
    game.apply(PlayerId::One, theft).expect("it is cast");
    settle(&mut game);

    assert!(game.battlefield.is_empty(), "their Angel went home");
    assert_eq!(
        game.players[1]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::SERRA_ANGEL],
    );
    assert_eq!(
        game.players[0]
            .exile
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::BRAZEN_BORROWER],
        "and the card waits in exile",
    );
}

/// It only answers what the other player controls.
#[test]
fn petty_theft_leaves_your_own_permanents_alone() {
    let (mut game, card) = staged(2);
    game.put_onto_battlefield(PlayerId::One, cards::SERRA_ANGEL)
        .expect("cataloged");
    drain_pending(&mut game);

    assert!(
        cast_with(&game, card, PlayOptionId(1)).is_none(),
        "nothing of theirs to name",
    );
}

/// The Faerie follows out of exile, with flying and its 3/1 body.
#[test]
fn the_faerie_follows_from_exile() {
    let (mut game, card) = staged(2);
    game.put_onto_battlefield(PlayerId::Two, cards::SERRA_ANGEL)
        .expect("cataloged");
    drain_pending(&mut game);
    let theft = cast_with(&game, card, PlayOptionId(1)).expect("two mana casts it");
    game.apply(PlayerId::One, theft).expect("it is cast");
    settle(&mut game);
    let exiled = game.players[0]
        .exile
        .first()
        .map(|card| card.id)
        .expect("in exile");

    // Priority went back to the active player as the spell resolved.
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 3);
    assert!(
        cast_with(&game, exiled, PlayOptionId(1)).is_none(),
        "the adventure cannot be taken twice",
    );
    let borrower =
        cast_with(&game, exiled, PlayOptionId::DEFAULT).expect("the Faerie may be cast from exile");
    game.apply(PlayerId::One, borrower).expect("it is cast");
    settle(&mut game);

    let faerie = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == ObjectKind::Card(cards::BRAZEN_BORROWER))
        .expect("it arrived");
    assert_eq!(game.power(faerie), Some(3));
    assert_eq!(game.toughness(faerie), Some(1));
    assert!(game.permanent_has_executable_keyword(faerie, KeywordAbility::Flying));
}

/// It blocks fliers and nothing else.
#[test]
fn it_blocks_only_fliers() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let borrower = game
        .put_onto_battlefield(PlayerId::One, cards::BRAZEN_BORROWER)
        .expect("cataloged");
    let ground = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    let flier = game
        .put_onto_battlefield(PlayerId::Two, cards::SERRA_ANGEL)
        .expect("cataloged");
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [5, 5];
    game.active_player = PlayerId::Two;
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.declare_attacker(ground, AttackDefender::Player(PlayerId::One));
    game.declare_attacker(flier, AttackDefender::Player(PlayerId::One));
    game.finish_declaring_attackers();
    game.step = Step::DeclareBlockers;

    assert!(
        !game.legal_actions(PlayerId::One).iter().any(
            |action| matches!(action, Action::DeclareBlocker { blocker, attacker }
                if *blocker == borrower && *attacker == ground)
        ),
        "the Bears are on the ground",
    );
    assert!(
        game.legal_actions(PlayerId::One).iter().any(
            |action| matches!(action, Action::DeclareBlocker { blocker, attacker }
                if *blocker == borrower && *attacker == flier)
        ),
        "and the Angel is not",
    );
}
