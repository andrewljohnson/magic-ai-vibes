//! Cryptic Command: two of four, never the same one twice, and one of the
//! two is almost always the draw.

use super::*;

const COUNTER: usize = 0;
const BOUNCE: usize = 1;
const TAP: usize = 2;
const DRAW: usize = 3;

fn mode(index: usize) -> ModeId {
    ModeId::from_index(index).expect("one of the four")
}

/// Player One holding a Command with the four mana to cast it.
fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let command = game
        .build_zone(PlayerId::One, &[cards::CRYPTIC_COMMAND])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let id = command.id;
    game.players[0].hand.push(command);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 4);
    game.priority = PlayerId::One;
    (game, id)
}

fn casts(game: &Game, card: GameObjectId) -> Vec<(Vec<ModeId>, Vec<Target>)> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::CastSpell {
                card: id, choices, ..
            } if id == card => Some((
                choices.modes().to_vec(),
                choices.iter_targets().copied().collect(),
            )),
            _ => None,
        })
        .collect()
}

fn cast_with(game: &mut Game, card: GameObjectId, wanted: &[ModeId], targets: &[Target]) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell {
                card: id, choices, ..
            } => {
                *id == card
                    && choices.modes() == wanted
                    && choices.iter_targets().copied().collect::<Vec<_>>() == targets
            }
            _ => false,
        })
        .expect("that combination of modes and targets is on offer");
    game.apply(PlayerId::One, action).expect("it is castable");
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
                .take(decision.minimum.max(1))
                .map(|option| option.id)
                .collect();
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

/// Puts a Bolt on the stack for Player Two, pointed across the table, and
/// hands priority back. A Bolt that resolves shows up in a life total; a Bolt
/// that was countered does not.
fn they_bolt_you(game: &mut Game) -> GameObjectId {
    let bolt = game
        .build_zone(PlayerId::Two, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let bolt_id = bolt.id;
    game.players[1].hand.push(bolt);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::Red, 1);
    game.priority = PlayerId::Two;
    let cast = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == bolt_id
                    && choices
                        .iter_targets()
                        .any(|target| *target == Target::Player(PlayerId::One))
            }
            _ => false,
        })
        .expect("the Bolt is castable at you");
    game.apply(PlayerId::Two, cast)
        .expect("it goes on the stack");
    game.priority = PlayerId::One;
    game.stack.last().expect("the Bolt is there").id
}

fn tapped(game: &Game, permanent: GameObjectId) -> bool {
    game.battlefield
        .iter()
        .find(|found| found.card.id == permanent)
        .expect("it is on the battlefield")
        .tapped
}

/// Every offered selection takes exactly two modes, and two different ones.
#[test]
fn it_always_chooses_two_different_modes() {
    let (game, command) = staged();
    let offered = casts(&game, command);

    assert!(!offered.is_empty(), "it is castable");
    for (modes, _) in &offered {
        assert_eq!(modes.len(), 2, "choose two: {modes:?}");
        assert_ne!(modes[0], modes[1], "and never the same one twice");
    }
}

/// The counter mode is a hard one: nothing is offered to pay through it.
#[test]
fn the_counter_mode_is_not_optional_for_them() {
    let (mut game, command) = staged();
    let bolt = they_bolt_you(&mut game);

    cast_with(
        &mut game,
        command,
        &[mode(COUNTER), mode(DRAW)],
        &[Target::Spell(bolt)],
    );
    settle(&mut game);

    assert_eq!(game.players[0].life, 20, "the Bolt never resolved");
    assert!(
        game.players[1]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::LIGHTNING_BOLT),
        "and went to their graveyard countered",
    );
}

/// "Target permanent", not "target creature": a land goes back to hand the
/// same way anything else does.
#[test]
fn the_bounce_mode_reaches_any_permanent() {
    let (mut game, command) = staged();
    let mountain = game
        .put_onto_battlefield(PlayerId::Two, cards::MOUNTAIN)
        .expect("cataloged");
    drain_pending(&mut game);
    game.priority = PlayerId::One;
    let before = game.players[0].hand.len();

    cast_with(
        &mut game,
        command,
        &[mode(BOUNCE), mode(DRAW)],
        &[Target::Permanent(mountain)],
    );
    settle(&mut game);

    assert!(
        game.battlefield.is_empty(),
        "the Mountain went back to hand",
    );
    assert!(
        game.players[1]
            .hand
            .iter()
            .any(|card| card.definition == cards::MOUNTAIN),
        "to its owner's hand, not yours",
    );
    assert_eq!(
        game.players[0].hand.len(),
        before - 1 + 1,
        "and the Command replaced itself",
    );
}

/// "Creatures your opponents control": yours stay untapped, which is what
/// makes the mode a one-sided Fog rather than a Falter.
#[test]
fn the_tap_mode_leaves_your_own_creatures_alone() {
    let (mut game, command) = staged();
    let theirs = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    let mine = game
        .put_onto_battlefield(PlayerId::One, cards::SERRA_ANGEL)
        .expect("cataloged");
    drain_pending(&mut game);
    game.priority = PlayerId::One;

    cast_with(&mut game, command, &[mode(TAP), mode(DRAW)], &[]);
    settle(&mut game);

    assert!(tapped(&game, theirs), "their Bears is tapped");
    assert!(!tapped(&game, mine), "and your Angel is not");
}

/// The tap and the draw ask for nothing, so with an empty stack and an empty
/// board they are the only two modes that can be chosen.
#[test]
fn with_nothing_to_point_at_only_the_untargeted_modes_are_offered() {
    let (game, command) = staged();
    let offered = casts(&game, command);

    assert_eq!(
        offered.len(),
        1,
        "an empty board leaves one way to choose two: {offered:?}",
    );
    assert_eq!(offered[0].0, vec![mode(TAP), mode(DRAW)]);
    assert!(offered[0].1.is_empty(), "with no targets declared");
}

/// Countering and bouncing at once declares two target slots, one of each
/// kind, and both halves happen.
#[test]
fn countering_and_bouncing_declares_one_target_each() {
    let (mut game, command) = staged();
    let bears = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);
    let bolt = they_bolt_you(&mut game);

    cast_with(
        &mut game,
        command,
        &[mode(COUNTER), mode(BOUNCE)],
        &[Target::Spell(bolt), Target::Permanent(bears)],
    );
    settle(&mut game);

    assert_eq!(game.players[0].life, 20, "the Bolt was countered");
    assert!(game.battlefield.is_empty(), "and the Bears went home");
}
