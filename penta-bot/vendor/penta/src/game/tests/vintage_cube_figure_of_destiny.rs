//! Figure of Destiny: a one-drop that keeps eating mana, and the two gates
//! that say the steps have to be climbed in order.

use super::*;

/// Player One with a Figure that has been out a turn and `mana` hybrid-payable
/// red available.
fn staged(mana: u16) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let figure = game
        .put_onto_battlefield(PlayerId::One, cards::FIGURE_OF_DESTINY)
        .expect("cataloged");
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [1, 1];
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, mana);
    drain_pending(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, figure)
}

fn settle(game: &mut Game) {
    for _ in 0..16 {
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
    game.check_state_based_actions();
}

fn permanent(game: &Game, id: GameObjectId) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("it is on the battlefield")
}

/// Activates the ability whose printed text starts with `prefix`, or returns
/// false when it is not offered.
fn activate(game: &mut Game, figure: GameObjectId, prefix: &str) -> bool {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, ability, ..
            } => *source == figure && ability_text(game, figure, *ability).starts_with(prefix),
            _ => false,
        });
    let Some(action) = action else {
        return false;
    };
    game.apply(PlayerId::One, action).expect("it activates");
    settle(game);
    true
}

fn ability_text(game: &Game, source: GameObjectId, origin: AbilityOrigin) -> &'static str {
    let mut text = "";
    let _ = game.visit_effective_abilities(permanent(game, source), |effective| {
        if effective.origin == origin {
            text = effective.ability.text;
            return std::ops::ControlFlow::Break(());
        }
        std::ops::ControlFlow::Continue(())
    });
    text
}

fn subtypes(game: &Game, figure: GameObjectId) -> Vec<String> {
    game.effective_subtypes(permanent(game, figure))
        .iter()
        .map(|subtype| (*subtype).to_string())
        .collect()
}

/// One hybrid mana turns a 1/1 Kithkin into a 2/2 Kithkin Spirit, and it
/// stays that way: nothing about the step ends.
#[test]
fn the_first_step_makes_a_two_two_spirit_for_good() {
    let (mut game, figure) = staged(1);
    assert_eq!(
        game.power(permanent(&game, figure)),
        Some(1),
        "a 1/1 to start"
    );

    assert!(activate(&mut game, figure, "{R/W}:"), "one mana is enough");

    assert_eq!(game.power(permanent(&game, figure)), Some(2), "now a 2/2");
    assert_eq!(
        subtypes(&game, figure),
        vec!["Kithkin".to_string(), "Spirit".to_string()],
        "a Kithkin Spirit, which is what \"becomes\" repaints rather than adds",
    );

    game.finish_cleanup();
    assert_eq!(
        game.power(permanent(&game, figure)),
        Some(2),
        "and it is still a 2/2 once the turn it was made in is over",
    );
}

/// Hybrid mana is paid with either half: a mono-white board climbs the same
/// ladder a mono-red one does.
#[test]
fn white_mana_pays_the_hybrid_too() {
    let (mut game, figure) = staged(0);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 1);

    assert!(activate(&mut game, figure, "{R/W}:"), "white pays {{R/W}}");
    assert_eq!(game.power(permanent(&game, figure)), Some(2), "a 2/2");
}

/// The second step reads the board: a Figure that is still a plain Kithkin
/// pays three mana and gets nothing.
#[test]
fn the_second_step_does_nothing_to_a_figure_that_is_not_a_spirit() {
    let (mut game, figure) = staged(3);

    assert!(
        activate(&mut game, figure, "{R/W}{R/W}{R/W}:"),
        "the ability is activatable whether or not it will do anything",
    );

    assert_eq!(
        game.power(permanent(&game, figure)),
        Some(1),
        "still a 1/1: it was not a Spirit",
    );
    assert_eq!(
        subtypes(&game, figure),
        vec!["Kithkin".to_string()],
        "and still only a Kithkin",
    );
}

/// Climbed in order, each step lands.
#[test]
fn the_whole_ladder_ends_at_an_eight_eight_flier() {
    let (mut game, figure) = staged(10);

    assert!(activate(&mut game, figure, "{R/W}:"), "step one");
    assert!(activate(&mut game, figure, "{R/W}{R/W}{R/W}:"), "step two");
    assert_eq!(game.power(permanent(&game, figure)), Some(4), "a 4/4");
    assert!(
        activate(&mut game, figure, "{R/W}{R/W}{R/W}{R/W}{R/W}{R/W}:"),
        "step three",
    );

    let figure_permanent = permanent(&game, figure);
    assert_eq!(game.power(figure_permanent), Some(8), "an 8/8");
    assert!(
        game.permanent_has_executable_keyword(figure_permanent, KeywordAbility::Flying),
        "with flying",
    );
    assert!(
        game.permanent_has_executable_keyword(figure_permanent, KeywordAbility::FirstStrike),
        "and first strike",
    );
    assert_eq!(
        subtypes(&game, figure),
        vec![
            "Kithkin".to_string(),
            "Spirit".to_string(),
            "Warrior".to_string(),
            "Avatar".to_string(),
        ],
        "all four types, in the order the card prints them",
    );
}

/// The third step is gated on the second, not on the first: a Spirit that
/// never became a Warrior does not skip a rung.
#[test]
fn the_third_step_will_not_skip_the_second() {
    let (mut game, figure) = staged(7);
    assert!(activate(&mut game, figure, "{R/W}:"), "step one");

    assert!(
        activate(&mut game, figure, "{R/W}{R/W}{R/W}{R/W}{R/W}{R/W}:"),
        "six mana is payable",
    );

    assert_eq!(
        game.power(permanent(&game, figure)),
        Some(2),
        "still the 2/2 Spirit: it was never a Warrior",
    );
    assert!(
        !game.permanent_has_executable_keyword(permanent(&game, figure), KeywordAbility::Flying),
        "and it does not fly",
    );
}
