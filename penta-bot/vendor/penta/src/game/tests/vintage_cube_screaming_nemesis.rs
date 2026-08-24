//! Screaming Nemesis: damage it takes goes somewhere else, and a player who
//! catches it is out of lifegain for good.

use super::*;

/// The Spirit on the battlefield under Player One.
fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let nemesis = game
        .put_onto_battlefield(PlayerId::One, cards::SCREAMING_NEMESIS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.players[0].life = 20;
    game.players[1].life = 20;
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, nemesis)
}

/// Answers the trigger, sending its damage at `wanted`.
fn settle(game: &mut Game, wanted: Target) {
    for _ in 0..32 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options: Vec<_> = decision
                .options
                .iter()
                .filter(|option| match wanted {
                    Target::Player(player) => option.label.contains(match player {
                        PlayerId::One => "you",
                        PlayerId::Two => "opponent",
                    }),
                    Target::Permanent(id) => option.card.is_some_and(|(object, _)| object == id),
                    Target::Card(_) | Target::Spell(_) => false,
                })
                .map(|option| option.id)
                .take(1)
                .collect();
            let options = if options.len() < decision.minimum.max(1) {
                decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum.max(1))
                    .collect()
            } else {
                options
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
    game.check_state_based_actions();
}

/// The damage it takes is passed on, and its size is what it took.
#[test]
fn what_it_takes_it_passes_on() {
    let (mut game, nemesis) = staged();

    game.damage_target_from(None, Some(Target::Permanent(nemesis)), 2);
    settle(&mut game, Target::Player(PlayerId::Two));

    assert_eq!(game.players[1].life, 18, "two dealt to them");
}

/// A player who takes it cannot gain life afterwards, ever.
#[test]
fn a_damaged_player_stops_gaining_life() {
    let (mut game, nemesis) = staged();

    game.damage_target_from(None, Some(Target::Permanent(nemesis)), 1);
    settle(&mut game, Target::Player(PlayerId::Two));
    assert_eq!(game.players[1].life, 19);

    game.gain_life(PlayerId::Two, 5);
    assert_eq!(game.players[1].life, 19, "the gain never arrives");
    game.gain_life(PlayerId::One, 5);
    assert_eq!(
        game.players[0].life, 25,
        "and the other player is untouched by it",
    );
}

/// Sending it at a creature leaves both players able to gain life.
#[test]
fn damaging_a_creature_bars_nobody() {
    let (mut game, nemesis) = staged();
    let bears = game
        .put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);

    game.damage_target_from(None, Some(Target::Permanent(nemesis)), 2);
    settle(&mut game, Target::Permanent(bears));

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == bears),
        "a 2/2 does not survive two",
    );
    game.gain_life(PlayerId::Two, 3);
    assert_eq!(game.players[1].life, 23, "nobody was told to stop");
}

/// "Any other target": the Spirit is not among its own choices.
#[test]
fn it_cannot_answer_itself() {
    let (mut game, nemesis) = staged();

    game.damage_target_from(None, Some(Target::Permanent(nemesis)), 1);
    for _ in 0..8 {
        if !game.pending_decisions.is_empty() {
            break;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            break;
        }
    }

    let decision = game
        .pending_decisions
        .first()
        .map(|pending| pending.observation.clone())
        .expect("the trigger asks for a target");
    assert!(
        !decision
            .options
            .iter()
            .any(|option| option.card.is_some_and(|(object, _)| object == nemesis)),
        "any other target leaves it out",
    );
    assert!(
        decision.options.len() >= 2,
        "both players are still on offer",
    );
}

/// Haste is printed on it.
#[test]
fn it_has_haste() {
    let (game, nemesis) = staged();
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == nemesis)
        .expect("it is there");

    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Haste));
    assert_eq!(game.power(permanent), Some(3));
    assert_eq!(game.toughness(permanent), Some(3));
}
