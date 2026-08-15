//! Mana abilities that cost mana.
//!
//! The mana such an ability costs comes out of the pool and nowhere else:
//! the point of a filter is that you tap something first and convert what it
//! made. So the ability is offered only while the pool already covers it,
//! and the automatic payment planner leaves it alone entirely -- a planner
//! that counted the production without the cost would think a filter made
//! mana out of nothing.

use super::*;
use crate::ImplementationStatus;

/// The activations this player is offered from that source.
fn mana_actions(
    game: &Game,
    player: PlayerId,
    source: GameObjectId,
) -> Vec<(AbilityOrigin, ManaColor)> {
    game.legal_actions(player)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateManaAbility {
                source: from,
                ability,
                color,
            } if from == source => Some((ability, color)),
            _ => None,
        })
        .collect()
}

fn activate(game: &mut Game, player: PlayerId, source: GameObjectId) {
    let (ability, color) = mana_actions(game, player, source)
        .pop()
        .expect("the ability is offered");
    game.apply(
        player,
        Action::ActivateManaAbility {
            source,
            ability,
            color,
        },
    )
    .expect("the mana ability activates");
}

#[test]
fn a_filter_is_offered_only_once_its_cost_is_floating() {
    let mut game = ready_game();
    let sprites = creature(10_000, cards::FIRE_SPRITES, PlayerId::One);
    let sprites_id = sprites.card.id;
    game.battlefield.push(sprites);

    assert!(
        mana_actions(&game, PlayerId::One, sprites_id).is_empty(),
        "an empty pool cannot pay the {{G}}"
    );

    game.players[PlayerId::One.index()].mana_pool.green = 1;
    let offered = mana_actions(&game, PlayerId::One, sprites_id);
    assert_eq!(
        offered.iter().map(|(_, color)| *color).collect::<Vec<_>>(),
        vec![ManaColor::Red],
        "with the green floating it can be filtered"
    );
}

#[test]
fn filtering_spends_the_cost_and_taps_the_source() {
    let mut game = ready_game();
    let sprites = creature(10_000, cards::FIRE_SPRITES, PlayerId::One);
    let sprites_id = sprites.card.id;
    game.battlefield.push(sprites);
    game.players[PlayerId::One.index()].mana_pool.green = 1;

    activate(&mut game, PlayerId::One, sprites_id);

    let pool = game.players[PlayerId::One.index()].mana_pool;
    assert_eq!(pool.green, 0, "the green paid for it");
    assert_eq!(pool.red, 1, "and red came back");
    assert!(
        game.battlefield
            .iter()
            .find(|permanent| permanent.card.id == sprites_id)
            .expect("still there")
            .tapped,
        "the tap was part of the cost"
    );
}

/// Three colourless for one blue, so the amount is not tied to the cost.
#[test]
fn apprentice_wizard_makes_three_for_one() {
    let mut game = ready_game();
    let wizard = creature(10_000, cards::APPRENTICE_WIZARD, PlayerId::One);
    let wizard_id = wizard.card.id;
    game.battlefield.push(wizard);
    game.players[PlayerId::One.index()].mana_pool.blue = 1;

    activate(&mut game, PlayerId::One, wizard_id);

    let pool = game.players[PlayerId::One.index()].mana_pool;
    assert_eq!((pool.blue, pool.colorless), (0, 3));
}

/// The source is spent as well as the mana, and the mana still arrives.
#[test]
fn coal_golem_sacrifices_itself_for_three_red() {
    let mut game = ready_game();
    let golem = creature(10_000, cards::COAL_GOLEM, PlayerId::One);
    let golem_id = golem.card.id;
    game.battlefield.push(golem);
    game.players[PlayerId::One.index()].mana_pool.colorless = 3;

    activate(&mut game, PlayerId::One, golem_id);
    drain_pending(&mut game);

    let pool = game.players[PlayerId::One.index()].mana_pool;
    assert_eq!((pool.colorless, pool.red), (0, 3), "three in, three out");
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == golem_id),
        "it sacrificed itself to pay"
    );
}

/// "Two mana of any one color" offers one activation per colour, and each
/// makes two of that colour.
#[test]
fn implements_of_sacrifice_offers_each_color_twice_over() {
    let mut game = ready_game();
    let implements = creature(10_000, cards::IMPLEMENTS_OF_SACRIFICE, PlayerId::One);
    let implements_id = implements.card.id;
    game.battlefield.push(implements);
    game.players[PlayerId::One.index()].mana_pool.colorless = 1;

    let offered = mana_actions(&game, PlayerId::One, implements_id);
    assert_eq!(offered.len(), 5, "one activation per colour");

    game.apply(
        PlayerId::One,
        Action::ActivateManaAbility {
            source: implements_id,
            ability: offered[0].0,
            color: ManaColor::Black,
        },
    )
    .expect("the mana ability activates");
    drain_pending(&mut game);

    let pool = game.players[PlayerId::One.index()].mana_pool;
    assert_eq!((pool.colorless, pool.black), (0, 2));
}

/// The planner stays out of it. Fire Sprites could turn a Forest's green
/// into red, but working that out is the player's job: a plan that counted
/// the red without the green would be counting mana that does not exist.
#[test]
fn the_payment_planner_does_not_reach_through_a_filter() {
    let mut game = ready_game();
    let sprites = creature(10_000, cards::FIRE_SPRITES, PlayerId::One);
    game.battlefield.push(sprites);
    let forest = creature(10_001, cards::FOREST, PlayerId::One);
    game.battlefield.push(forest);

    assert!(
        game.can_pay_cost(PlayerId::One, mana_cost!("{G}"), 0),
        "the Forest is planned as usual"
    );
    assert!(
        !game.can_pay_cost(PlayerId::One, mana_cost!("{R}"), 0),
        "but the filter is not planned through"
    );
}

#[test]
fn every_filtering_identity_reports_complete_coverage() {
    let catalog = poc::catalog().expect("catalog builds");
    for definition in [
        cards::FIRE_SPRITES,
        cards::APPRENTICE_WIZARD,
        cards::COAL_GOLEM,
        cards::IMPLEMENTS_OF_SACRIFICE,
    ] {
        let card = catalog.get(definition).expect("the card is cataloged");
        assert_eq!(
            card.rules.implementation_status(),
            ImplementationStatus::Complete,
            "{} should be fully executable",
            card.name,
        );
    }
}
