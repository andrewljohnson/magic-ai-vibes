//! Tendrils of Agony: two life a copy, and a copy for every spell that came
//! before it.

use super::*;

/// Casts `count` cantrips for Player One so the Tendrils that follows arrives
/// with that many spells behind it this turn.
fn cast_cantrips(game: &mut Game, count: u32) {
    for index in 0..count {
        let spell = card(20_000 + index, cards::OPT, PlayerId::One);
        let spell_id = spell.id;
        game.players[PlayerId::One.index()].hand.push(spell);
        game.players[PlayerId::One.index()].mana_pool.blue = 1;
        game.priority = PlayerId::One;
        game.apply(
            PlayerId::One,
            cast_action(spell_id, Vec::new(), Vec::new(), 0),
        )
        .expect("a cantrip is castable");
        drain_pending(game);
    }
}

/// Answers every retarget offer with `keep`, then lets the stack empty. The
/// offer stands once per copy, and a copy that keeps its targets drains the
/// same player the original did.
fn settle(game: &mut Game, keep: bool) {
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let wanted = if keep {
                "Keep original targets"
            } else {
                "Copy with targets you"
            };
            let options = decision
                .options
                .iter()
                .find(|option| option.label == wanted)
                .map(|option| vec![option.id])
                .expect("the copy is offered both answers");
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("the decision accepts what it offered");
            continue;
        }
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            return;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            return;
        }
    }
}

/// Casts a Tendrils at Player Two, leaving the stack for the caller.
fn cast_tendrils(game: &mut Game) {
    let tendrils = card(10_000, cards::TENDRILS_OF_AGONY, PlayerId::One);
    let tendrils_id = tendrils.id;
    game.players[PlayerId::One.index()].hand.push(tendrils);
    let pool = &mut game.players[PlayerId::One.index()].mana_pool;
    pool.black = 2;
    pool.colorless = 2;
    game.priority = PlayerId::One;
    game.apply(
        PlayerId::One,
        cast_action(
            tendrils_id,
            vec![Target::Player(PlayerId::Two)],
            Vec::new(),
            0,
        ),
    )
    .expect("four mana buys a Tendrils");
}

/// On its own it is four life swung and nothing more.
#[test]
fn a_lone_tendrils_drains_two() {
    let mut game = ready_game();
    cast_tendrils(&mut game);
    settle(&mut game, true);

    assert_eq!(game.players[1].life, 18, "two lost");
    assert_eq!(game.players[0].life, 22, "and two gained");
}

/// Storm counts what came before it, not itself: two cantrips make two
/// copies, so three drains land in total.
#[test]
fn storm_copies_it_once_per_earlier_spell() {
    let mut game = ready_game();
    cast_cantrips(&mut game, 2);
    cast_tendrils(&mut game);
    settle(&mut game, true);

    assert_eq!(
        game.players[1].life,
        20 - 6,
        "the original and its two copies",
    );
    assert_eq!(game.players[0].life, 20 + 6);
}

/// "You may choose new targets for the copies." Pointed back at yourself, a
/// copy costs you the two it gives you.
#[test]
fn a_copy_may_be_pointed_somewhere_else() {
    let mut game = ready_game();
    cast_cantrips(&mut game, 1);
    cast_tendrils(&mut game);
    settle(&mut game, false);

    assert_eq!(
        game.players[1].life, 18,
        "only the original still points across the table",
    );
    assert_eq!(
        game.players[0].life, 22,
        "the copy took two from you and gave two back",
    );
}

/// The life gained is a flat two a copy however little the target had left.
#[test]
fn the_gain_does_not_follow_what_was_actually_lost() {
    let mut game = ready_game();
    game.players[1].life = 1;
    cast_tendrils(&mut game);
    settle(&mut game, true);

    assert_eq!(game.players[1].life, -1, "life loss goes past zero");
    assert_eq!(
        game.players[0].life, 22,
        "and you still gain the printed two",
    );
}

/// Life loss is not damage, so nothing watching for damage sees it.
#[test]
fn the_loss_is_not_damage() {
    let mut game = ready_game();
    cast_tendrils(&mut game);
    settle(&mut game, true);

    assert!(
        !game
            .events
            .iter()
            .any(|event| matches!(event, GameEvent::DamageDealt { .. })),
        "a drain that dealt damage would be a different card",
    );
}
