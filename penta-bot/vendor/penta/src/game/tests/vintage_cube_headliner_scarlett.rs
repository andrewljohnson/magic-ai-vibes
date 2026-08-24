//! Headliner Scarlett: a hasty body that walks past the board it lands
//! into, and a card each upkeep that nobody else gets to see.

use super::*;

/// Scarlett entering against a board with a blocker on it.
fn staged() -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].library.clear();
    game.players[0].hand.clear();
    for index in 0..4 {
        game.players[0]
            .library
            .push(card(95_000 + index, cards::LIGHTNING_BOLT, PlayerId::One));
    }
    let blocker = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);
    let scarlett = game
        .put_onto_battlefield(PlayerId::One, cards::HEADLINER_SCARLETT)
        .expect("cataloged");
    game.finish_rules_procedure();
    (game, scarlett, blocker)
}

/// Answers the enter trigger's target, naming the opponent.
fn aim_at_opponent(game: &mut Game) {
    let decision = game
        .observe(PlayerId::One)
        .decision
        .expect("the enter trigger asks which player");
    let chosen = decision
        .options
        .iter()
        .find(|option| option.label.contains("Two") || option.label.contains("Opponent"))
        .map_or(decision.options[1].id, |option| option.id);
    game.apply(
        PlayerId::One,
        Action::ChooseDecision {
            decision: decision.id,
            options: vec![chosen],
        },
    )
    .expect("naming a player is legal");
    drain_pending(game);
}

/// She has haste, so the body attacks the turn it lands.
#[test]
fn she_has_haste() {
    let (game, scarlett, _blocker) = staged();
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == scarlett)
        .expect("she is there");

    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Haste));
}

/// Their creatures cannot block for the turn.
#[test]
fn their_creatures_cannot_block() {
    let (mut game, scarlett, blocker) = staged();
    aim_at_opponent(&mut game);

    game.active_player = PlayerId::One;
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.priority = PlayerId::One;
    game.apply(
        PlayerId::One,
        Action::DeclareAttacker {
            attacker: scarlett,
            defender: AttackDefender::Player(PlayerId::Two),
        },
    )
    .expect("she attacks");
    game.apply(PlayerId::One, Action::FinishDeclaringAttackers)
        .expect("the declaration finishes");
    drain_pending(&mut game);

    assert!(
        game.legal_actions(PlayerId::Two).into_iter().all(
            |action| !matches!(action, Action::DeclareBlocker { blocker: b, .. } if b == blocker)
        ),
        "the bear was told it cannot block",
    );
}

/// The upkeep clause exiles one card face down, which only she can read.
#[test]
fn the_upkeep_exiles_one_card_face_down() {
    let (mut game, _scarlett, _blocker) = staged();
    aim_at_opponent(&mut game);
    let library = game.players[0].library.len();

    game.active_player = PlayerId::One;
    game.step = Step::Upkeep;
    game.handle_upkeep_triggers();
    drain_pending(&mut game);

    assert_eq!(game.players[0].library.len(), library - 1);
    assert_eq!(game.players[0].exile.len(), 1);
    assert!(
        game.observe(PlayerId::Two).exiles[0].is_empty(),
        "the opponent sees a count and not a card",
    );
    assert_eq!(
        game.observe(PlayerId::Two).face_down_exile_sizes[0],
        1,
        "and they may count it",
    );
    assert_eq!(
        game.observe(PlayerId::One).exiles[0].len(),
        1,
        "she may look at it",
    );
}

/// It is playable that turn, and it still costs what it costs.
#[test]
fn the_exiled_card_is_playable_for_its_own_cost() {
    let (mut game, _scarlett, _blocker) = staged();
    aim_at_opponent(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::Upkeep;
    game.handle_upkeep_triggers();
    drain_pending(&mut game);
    let exiled = game.players[0].exile[0].id;
    game.step = Step::PrecombatMain;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;

    assert!(
        game.legal_actions(PlayerId::One)
            .into_iter()
            .all(|action| !matches!(action, Action::CastSpell { card, .. } if card == exiled)),
        "with no red mana there is nothing to cast it with",
    );

    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);

    assert!(
        game.legal_actions(PlayerId::One)
            .into_iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if card == exiled)),
        "one red mana plays it",
    );
}
