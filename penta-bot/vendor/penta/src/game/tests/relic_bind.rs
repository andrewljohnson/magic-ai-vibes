//! Relic Bind: an Aura on somebody else's artifact that asks a question
//! every time they tap it.

use super::*;

/// The Aura attached to an opponent's Mox, with player one holding it.
fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let mox = creature(91_000, cards::MOX_JET, PlayerId::Two);
    let mox_id = mox.card.id;
    game.battlefield.push(mox);
    game.players[PlayerId::One.index()].hand.clear();
    let bind = card(91_001, cards::RELIC_BIND, PlayerId::One);
    let bind_id = bind.id;
    game.players[PlayerId::One.index()].hand.push(bind);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 3);
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == bind_id))
        .expect("three mana buys it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    drain_pending(&mut game);
    (game, mox_id)
}

/// Taps the artifact, takes `mode`, and points that mode at the opponent.
fn tap_and_settle(game: &mut Game, artifact: GameObjectId, mode: usize) {
    game.tap_permanent(artifact);
    let mut taken = false;
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = if taken {
                // The target, which both modes offer as "you" or "your
                // opponent". Everything interesting is pointed the other way.
                decision
                    .options
                    .iter()
                    .filter(|option| option.label == "your opponent")
                    .map(|option| option.id)
                    .take(1)
                    .collect()
            } else {
                taken = true;
                decision
                    .options
                    .iter()
                    .skip(mode)
                    .map(|option| option.id)
                    .take(1)
                    .collect()
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
}

/// It attaches to the opponent's artifact.
#[test]
fn it_enchants_the_artifact() {
    let (game, mox) = staged();

    assert!(
        game.battlefield.iter().any(|permanent| {
            permanent.card.definition == cards::RELIC_BIND && permanent.attached_to == Some(mox)
        }),
        "the Aura is on the Mox",
    );
}

/// The first mode burns for one.
#[test]
fn the_first_mode_deals_a_point() {
    let (mut game, mox) = staged();
    let before = game.players[PlayerId::Two.index()].life;

    tap_and_settle(&mut game, mox, 0);

    assert_eq!(
        game.players[PlayerId::Two.index()].life,
        before - 1,
        "one damage, wherever the mode is pointed",
    );
}

/// The second mode gains one instead.
#[test]
fn the_second_mode_gains_a_life() {
    let (mut game, mox) = staged();
    let before = [
        game.players[PlayerId::One.index()].life,
        game.players[PlayerId::Two.index()].life,
    ];

    tap_and_settle(&mut game, mox, 1);

    assert_eq!(
        [
            game.players[PlayerId::One.index()].life,
            game.players[PlayerId::Two.index()].life,
        ],
        [before[0], before[1] + 1],
        "the life goes wherever it is pointed, and nothing is damaged",
    );
}
