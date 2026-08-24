//! The horizon lands: two colours that cost a life apiece, and a land that
//! turns into a card once it has nothing left to pay for. Sunbaked Canyon
//! stands for the cycle; the others differ only in which two colours.

use super::*;

fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let canyon = game
        .put_onto_battlefield(PlayerId::One, cards::SUNBAKED_CANYON)
        .expect("cataloged");
    drain_pending(&mut game);
    game.priority = PlayerId::One;
    (game, canyon)
}

fn mana_colors(game: &Game, source: GameObjectId) -> Vec<ManaColor> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateManaAbility {
                source: id, color, ..
            } if id == source => Some(color),
            _ => None,
        })
        .collect()
}

/// Red and white, and nothing else -- a horizon land makes no colourless.
#[test]
fn it_offers_only_its_two_colours() {
    let (game, canyon) = staged();
    let colors = mana_colors(&game, canyon);

    assert!(colors.contains(&ManaColor::Red));
    assert!(colors.contains(&ManaColor::White));
    assert_eq!(colors.len(), 2, "no colourless and no third colour");
}

/// Tapping it costs a life, which is the whole reason it draws a card later.
#[test]
fn making_mana_costs_a_life() {
    let (mut game, canyon) = staged();
    let ability = mana_ability_for(&game, canyon, ManaColor::Red);

    game.apply(
        PlayerId::One,
        Action::ActivateManaAbility {
            source: canyon,
            ability,
            color: ManaColor::Red,
            counters_removed: None,
            cost_object: None,
            combination: None,
        },
    )
    .expect("a land with a life to spare taps for red");

    assert_eq!(game.players[0].mana_pool.red, 1);
    assert_eq!(game.players[0].life, 19, "one life for one mana");
}

/// A player at one life may still pay it: life is payable down to zero.
#[test]
fn one_life_is_still_enough() {
    let (mut game, canyon) = staged();
    game.players[0].life = 1;

    assert!(
        !mana_colors(&game, canyon).is_empty(),
        "paying to zero is legal (CR 118.4)",
    );
}

/// A player at zero cannot pay it, so the land makes no mana at all.
#[test]
fn no_life_means_no_mana() {
    let (mut game, canyon) = staged();
    game.players[0].life = 0;

    assert!(
        mana_colors(&game, canyon).is_empty(),
        "there is no life left to spend",
    );
}

/// Cashing it in sacrifices the land and draws.
#[test]
fn it_can_be_cashed_in_for_a_card() {
    let (mut game, canyon) = staged();
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);
    let before = game.players[0].hand.len();
    // Taken from what the land is offering rather than by index: the mana
    // ability and this one are not numbered in the same sequence.
    let cash_in = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == canyon),
        )
        .expect("one mana and a tap buys a card");
    game.apply(PlayerId::One, cash_in)
        .expect("the ability activates");
    drain_pending(&mut game);

    assert!(
        game.battlefield.is_empty(),
        "the land sacrificed itself as a cost",
    );
    assert_eq!(game.players[0].hand.len(), before + 1);
    assert_eq!(game.players[0].life, 20, "and cost no life to do it");
}

/// Horizon Canopy is the original of the cycle and the same card: only the
/// pair of colours is different.
#[test]
fn the_canopy_offers_its_own_two_colours() {
    let mut game = ready_game();
    game.battlefield.clear();
    let canopy = game
        .put_onto_battlefield(PlayerId::One, cards::HORIZON_CANOPY)
        .expect("cataloged");
    drain_pending(&mut game);
    game.priority = PlayerId::One;
    let colors = mana_colors(&game, canopy);

    assert!(colors.contains(&ManaColor::Green), "Forest half");
    assert!(colors.contains(&ManaColor::White), "Plains half");
    assert_eq!(colors.len(), 2, "no colourless and no third colour");
}

/// And it cashes itself in the same way, which is what makes the shared
/// clause shared rather than copied.
#[test]
fn the_canopy_cashes_itself_in_too() {
    let mut game = ready_game();
    game.battlefield.clear();
    let canopy = game
        .put_onto_battlefield(PlayerId::One, cards::HORIZON_CANOPY)
        .expect("cataloged");
    drain_pending(&mut game);
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);
    let before = game.players[0].hand.len();

    let cash_in = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == canopy),
        )
        .expect("one mana and a tap buys a card");
    game.apply(PlayerId::One, cash_in)
        .expect("the ability activates");
    drain_pending(&mut game);

    assert!(game.battlefield.is_empty(), "the land sacrificed itself");
    assert_eq!(game.players[0].hand.len(), before + 1);
    assert_eq!(game.players[0].life, 20, "and cost no life to do it");
}
