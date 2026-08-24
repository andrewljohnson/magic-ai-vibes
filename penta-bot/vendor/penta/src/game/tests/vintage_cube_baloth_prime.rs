//! Baloth Prime: a 10/10 for four that owes six untaps, and the lands you
//! feed him are what pay them off.

use super::*;

/// Player One with lands in play and the Baloth in hand.
fn staged(lands: usize) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    for index in 0..lands {
        game.put_onto_battlefield(PlayerId::One, cards::FOREST)
            .expect("cataloged");
        let _ = index;
    }
    drain_pending(&mut game);
    let baloth = game
        .build_zone(PlayerId::One, &[cards::BALOTH_PRIME])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let id = baloth.id;
    game.players[0].hand.push(baloth);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, id)
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

/// Casts him and resolves, returning the permanent's id.
fn resolve_him(game: &mut Game, card: GameObjectId) -> GameObjectId {
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 4);
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card: id, .. } if *id == card))
        .expect("four mana pays for him");
    game.apply(PlayerId::One, cast).expect("he is cast");
    settle(game);
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::BALOTH_PRIME)
        .expect("he resolved")
        .card
        .id
}

fn permanent(game: &Game, id: GameObjectId) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("it is on the battlefield")
}

/// He arrives tapped, carrying six stun counters.
#[test]
fn he_enters_tapped_and_stunned() {
    let (mut game, card) = staged(0);

    let baloth = resolve_him(&mut game, card);

    assert!(permanent(&game, baloth).tapped);
    assert_eq!(permanent(&game, baloth).counters(CounterKind::Stun), 6);
}

/// The untap step takes a counter off instead of untapping him, which is
/// what a stun counter is.
#[test]
fn the_untap_step_spends_a_stun_counter() {
    let (mut game, card) = staged(0);
    let baloth = resolve_him(&mut game, card);

    game.commit_next_turn(PlayerId::One, Vec::new());

    assert!(permanent(&game, baloth).tapped, "he is still tapped");
    assert_eq!(permanent(&game, baloth).counters(CounterKind::Stun), 5);
}

/// Sacrificing a land makes a tapped 4/4 and spends one more counter, since
/// "untap this creature" is an untap like any other.
#[test]
fn sacrificing_a_land_pays_one_off_and_leaves_a_beast() {
    let (mut game, card) = staged(1);
    let baloth = resolve_him(&mut game, card);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 4);

    let drain = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == baloth),
        )
        .expect("four mana and a land pay for it");
    game.apply(PlayerId::One, drain).expect("it activates");
    settle(&mut game);

    assert_eq!(game.players[0].life, 22, "the ability itself gained two");
    // He is a Beast himself, so the token is the other one.
    let beast = game
        .battlefield
        .iter()
        .find(|permanent| {
            permanent.card.id != baloth && game.effective_subtypes(permanent).contains(&"Beast")
        })
        .expect("the trigger made a token");
    assert!(beast.tapped, "the token arrives tapped");
    assert_eq!(game.power(beast), Some(4));
    assert_eq!(
        permanent(&game, baloth).counters(CounterKind::Stun),
        5,
        "the untap was replaced by removing a counter",
    );
    assert!(permanent(&game, baloth).tapped);
}

/// Once the counters are gone an untap actually untaps him.
#[test]
fn the_last_counter_lets_him_untap() {
    let (mut game, card) = staged(0);
    let baloth = resolve_him(&mut game, card);
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == baloth)
    {
        permanent.set_counters(CounterKind::Stun, 1);
    }

    game.commit_next_turn(PlayerId::One, Vec::new());
    assert!(
        permanent(&game, baloth).tapped,
        "the last counter comes off"
    );
    assert_eq!(permanent(&game, baloth).counters(CounterKind::Stun), 0);

    game.commit_next_turn(PlayerId::One, Vec::new());
    assert!(!permanent(&game, baloth).tapped, "and then he untaps");
}
