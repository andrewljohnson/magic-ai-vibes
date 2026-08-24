//! Loot, the Pathfinder: a hasty double striker whose three exhaust
//! abilities each fire once and never again.

use super::*;

fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    for index in 0..6 {
        game.players[0]
            .library
            .push(card(100_000 + index, cards::GRIZZLY_BEARS, PlayerId::One));
    }
    let loot = game
        .put_onto_battlefield(PlayerId::One, cards::LOOT_THE_PATHFINDER)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, loot)
}

/// The activations Loot is offering right now, by the mana each one costs.
fn offered(game: &Game, loot: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| {
            matches!(
                action,
                Action::ActivateAbility { source, .. } | Action::ActivateManaAbility { source, .. }
                    if *source == loot
            )
        })
        .collect()
}

fn resolve(game: &mut Game) {
    for _ in 0..12 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .iter()
                .map(|option| option.id)
                .take(decision.minimum.max(1).min(decision.maximum))
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
}

/// The keywords are all three.
#[test]
fn it_has_its_three_keywords() {
    let (game, loot) = staged();
    let beast = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == loot)
        .expect("it is there");

    assert!(game.permanent_has_executable_keyword(beast, KeywordAbility::DoubleStrike));
    assert!(game.permanent_has_executable_keyword(beast, KeywordAbility::Vigilance));
    assert!(game.permanent_has_executable_keyword(beast, KeywordAbility::Haste));
}

/// The blue exhaust draws three, once. Untapping does not give it back.
#[test]
fn the_blue_exhaust_fires_once_and_never_again() {
    let (mut game, loot) = staged();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);

    let draw = offered(&game, loot)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateAbility { .. }))
        .expect("a blue mana buys the draw");
    game.apply(PlayerId::One, draw).expect("it activates");
    resolve(&mut game);
    assert_eq!(game.players[0].hand.len(), 3);

    // Untap it and try again: the ability is spent for good.
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == loot)
    {
        permanent.tapped = false;
    }
    game.cleanup();
    game.turns_started = [6, 6];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);

    assert!(
        !offered(&game, loot)
            .iter()
            .any(|action| matches!(action, Action::ActivateAbility { .. })),
        "exhaust is spent for as long as the creature is there",
    );
}

/// Spending one leaves the others: they are three abilities, not one.
#[test]
fn the_other_exhausts_are_untouched() {
    let (mut game, loot) = staged();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 1);
    let draw = offered(&game, loot)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateAbility { .. }))
        .expect("a blue mana buys the draw");
    game.apply(PlayerId::One, draw).expect("it activates");
    resolve(&mut game);

    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == loot)
    {
        permanent.tapped = false;
    }
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);

    assert!(
        offered(&game, loot)
            .iter()
            .any(|action| matches!(action, Action::ActivateAbility { .. })),
        "the red half was never activated",
    );
}

/// The green exhaust is a mana ability: three mana of one colour.
#[test]
fn the_green_exhaust_makes_three_mana() {
    let (mut game, loot) = staged();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 1);

    let mana = offered(&game, loot)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateManaAbility { .. }))
        .expect("a green mana buys the mana ability");
    game.apply(PlayerId::One, mana).expect("it activates");
    resolve(&mut game);

    let pool = &game.players[0].mana_pool;
    assert_eq!(
        pool.white + pool.blue + pool.black + pool.red + pool.green + pool.colorless,
        3,
        "three mana of one colour, and the green that paid is spent",
    );
}
