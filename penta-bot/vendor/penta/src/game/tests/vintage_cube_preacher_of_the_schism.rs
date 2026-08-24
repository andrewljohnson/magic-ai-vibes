//! Preacher of the Schism: two clauses about one attack, each asking who is
//! ahead on life.

use super::*;

/// Her on the battlefield since last turn, with the two life totals set.
fn staged(yours: i16, theirs: i16) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    for (index, definition) in [cards::MOUNTAIN, cards::FOREST].into_iter().enumerate() {
        let id = 273_000 + u32::try_from(index).expect("two cards");
        game.players[0]
            .library
            .push(card(id, definition, PlayerId::One));
    }
    let preacher = game
        .put_onto_battlefield(PlayerId::One, cards::PREACHER_OF_THE_SCHISM)
        .expect("cataloged");
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.players[0].life = yours;
    game.players[1].life = theirs;
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    (game, preacher)
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

/// She attacks the other player, and whatever triggers is carried through.
fn attack(game: &mut Game, preacher: GameObjectId) {
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.declare_attacker(preacher, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    settle(game);
}

fn tokens(game: &Game) -> Vec<(i16, i16)> {
    game.battlefield
        .iter()
        .filter(|permanent| !matches!(permanent.card.definition, ObjectKind::Card(_)))
        .map(|permanent| {
            (
                game.power(permanent).unwrap_or_default(),
                game.toughness(permanent).unwrap_or_default(),
            )
        })
        .collect()
}

/// Deathtouch is printed on her, and it is hers whatever the life totals do.
#[test]
fn she_has_deathtouch() {
    let (game, preacher) = staged(20, 20);
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == preacher)
        .expect("she is there");

    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Deathtouch));
    assert_eq!(game.power(permanent), Some(2));
    assert_eq!(game.toughness(permanent), Some(4));
}

/// Attacking the player who is ahead makes the token. Being behind on life
/// yourself means the second clause says nothing.
#[test]
fn attacking_the_leader_makes_a_lifelinking_vampire() {
    let (mut game, preacher) = staged(10, 20);

    attack(&mut game, preacher);

    assert_eq!(tokens(&game), vec![(1, 1)], "one 1/1 arrived");
    let token = game
        .battlefield
        .iter()
        .find(|permanent| !matches!(permanent.card.definition, ObjectKind::Card(_)))
        .expect("the token is there");
    assert!(
        game.permanent_has_executable_keyword(token, KeywordAbility::Lifelink),
        "and it has lifelink",
    );
    assert_eq!(
        game.players[0].hand.len(),
        0,
        "you are not ahead, so no card"
    );
    assert_eq!(game.players[0].life, 10, "and nothing was paid");
}

/// Being ahead yourself draws a card and costs a life, and the player you
/// attacked is behind, so no token.
#[test]
fn attacking_while_ahead_draws_and_costs_a_life() {
    let (mut game, preacher) = staged(20, 10);

    attack(&mut game, preacher);

    assert!(tokens(&game).is_empty(), "they are not the leader");
    assert_eq!(game.players[0].hand.len(), 1, "you drew");
    assert_eq!(game.players[0].life, 19, "and paid a life for it");
}

/// Tied for most life counts for both clauses: one attack, two triggers.
#[test]
fn a_tie_counts_for_both_clauses() {
    let (mut game, preacher) = staged(20, 20);

    attack(&mut game, preacher);

    assert_eq!(tokens(&game), vec![(1, 1)]);
    assert_eq!(game.players[0].hand.len(), 1);
    assert_eq!(game.players[0].life, 19);
}

/// The condition belongs to the attack rather than being an intervening if:
/// life that moves while the trigger waits on the stack does not undo it.
#[test]
fn the_condition_is_read_where_the_attack_happened() {
    let (mut game, preacher) = staged(20, 10);
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.declare_attacker(preacher, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    // Both triggers are waiting; the lead changes hands before either one
    // resolves.
    game.players[0].life = 5;
    game.players[1].life = 30;
    settle(&mut game);

    assert_eq!(
        game.players[0].hand.len(),
        1,
        "she attacked while ahead, and that is settled",
    );
    assert!(
        tokens(&game).is_empty(),
        "and they were behind when she attacked",
    );
}
