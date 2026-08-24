//! Thundering Falls: a tapped dual that looks at the top of your library on
//! the way in and lets you bin what you find.

use super::*;

/// Player One with a Thundering Falls in hand and a known card on top.
fn staged(top: CardDefinitionId) -> (Game, GameObjectId) {
    staged_with(cards::THUNDERING_FALLS, top)
}

/// The same, for any land in the cycle.
fn staged_with(land: CardDefinitionId, top: CardDefinitionId) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].library.clear();
    let card = game
        .build_zone(PlayerId::One, &[top])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[0].library.push(card);
    let land = game
        .build_zone(PlayerId::One, &[land])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let land_id = land.id;
    game.players[0].hand.push(land);
    (game, land_id)
}

/// Plays the land and answers the surveil, keeping the card on top when
/// `bin` is false.
fn play_and_surveil(game: &mut Game, land: GameObjectId, bin: bool) {
    game.priority = PlayerId::One;
    let play = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::PlayLand { card, .. } if *card == land))
        .expect("a land drop is available");
    game.apply(PlayerId::One, play)
        .expect("the land is playable");

    for _ in 0..12 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            // Choosing the card puts it in the graveyard; choosing nothing
            // leaves it where it was.
            let options = if bin {
                decision
                    .options
                    .first()
                    .map(|option| vec![option.id])
                    .unwrap_or_default()
            } else {
                Vec::new()
            };
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("surveil accepts either answer");
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

fn the_land(game: &Game) -> Option<&Permanent> {
    the_land_named(game, cards::THUNDERING_FALLS)
}

fn the_land_named(game: &Game, definition: CardDefinitionId) -> Option<&Permanent> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == definition)
}

/// It arrives tapped, whatever you do with the surveil.
#[test]
fn it_enters_tapped() {
    let (mut game, land) = staged(cards::LIGHTNING_BOLT);
    play_and_surveil(&mut game, land, false);

    assert!(the_land(&game).is_some_and(|permanent| permanent.tapped));
}

/// Binning the card puts it in the graveyard and empties the library.
#[test]
fn surveil_may_put_the_card_in_the_graveyard() {
    let (mut game, land) = staged(cards::LIGHTNING_BOLT);
    play_and_surveil(&mut game, land, true);

    assert!(game.players[0].library.is_empty(), "it left the top");
    assert_eq!(
        game.players[0]
            .graveyard
            .iter()
            .filter(|card| card.definition == cards::LIGHTNING_BOLT)
            .count(),
        1,
    );
}

/// Declining leaves it on top, which is the other half of the choice.
#[test]
fn surveil_may_leave_the_card_on_top() {
    let (mut game, land) = staged(cards::LIGHTNING_BOLT);
    play_and_surveil(&mut game, land, false);

    assert_eq!(game.players[0].library.len(), 1, "still on top");
    assert!(
        game.players[0].graveyard.is_empty(),
        "and nothing was binned",
    );
}

/// Every land in the cycle is the same card with different types, so one
/// more of them needs only its colours checked.
#[test]
fn another_land_in_the_cycle_taps_for_its_own_two() {
    let (mut game, land) = staged_with(cards::LUSH_PORTICO, cards::LIGHTNING_BOLT);
    play_and_surveil(&mut game, land, false);
    let id = the_land_named(&game, cards::LUSH_PORTICO)
        .expect("it is on the battlefield")
        .card
        .id;
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == id)
    {
        permanent.tapped = false;
    }

    let colors = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateManaAbility { source, color, .. } if source == id => Some(color),
            _ => None,
        })
        .collect::<Vec<_>>();

    assert!(colors.contains(&ManaColor::Green), "Forest");
    assert!(colors.contains(&ManaColor::White), "Plains");
    assert_eq!(colors.len(), 2, "and nothing else");
}

/// And the red-green one, which completes the half of the cycle the cube
/// wants.
#[test]
fn the_red_green_land_taps_for_its_own_two() {
    let (mut game, land) = staged_with(cards::COMMERCIAL_DISTRICT, cards::LIGHTNING_BOLT);
    play_and_surveil(&mut game, land, false);
    let id = the_land_named(&game, cards::COMMERCIAL_DISTRICT)
        .expect("it is on the battlefield")
        .card
        .id;
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == id)
    {
        permanent.tapped = false;
    }

    let colors = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateManaAbility { source, color, .. } if source == id => Some(color),
            _ => None,
        })
        .collect::<Vec<_>>();

    assert!(colors.contains(&ManaColor::Red), "Mountain");
    assert!(colors.contains(&ManaColor::Green), "Forest");
    assert_eq!(colors.len(), 2, "and nothing else");
}

/// The mana abilities come from the basic land types rather than a printed
/// clause, so both colours are on offer.
#[test]
fn it_taps_for_either_colour() {
    let (mut game, land) = staged(cards::LIGHTNING_BOLT);
    play_and_surveil(&mut game, land, false);
    let id = the_land(&game).expect("it is on the battlefield").card.id;
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == id)
    {
        permanent.tapped = false;
    }

    let colors = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateManaAbility { source, color, .. } if source == id => Some(color),
            _ => None,
        })
        .collect::<Vec<_>>();

    assert!(colors.contains(&ManaColor::Blue), "Island");
    assert!(colors.contains(&ManaColor::Red), "Mountain");
}

/// And the white-blue one, which completes the half of the cycle the cube
/// wants. The cycle is one card with three pairs of colours, so what is left
/// to check on each is only which two.
#[test]
fn the_white_blue_land_taps_for_its_own_two() {
    let (mut game, land) = staged_with(cards::METICULOUS_ARCHIVE, cards::LIGHTNING_BOLT);
    play_and_surveil(&mut game, land, false);
    let id = the_land_named(&game, cards::METICULOUS_ARCHIVE)
        .expect("it is on the battlefield")
        .card
        .id;
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == id)
    {
        permanent.tapped = false;
    }

    let colors = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateManaAbility { source, color, .. } if source == id => Some(color),
            _ => None,
        })
        .collect::<Vec<_>>();

    assert!(colors.contains(&ManaColor::White), "Plains");
    assert!(colors.contains(&ManaColor::Blue), "Island");
    assert_eq!(colors.len(), 2, "and nothing else");
}

/// Undercity Sewers is the blue-black member: the same land, its own two
/// basic types.
#[test]
fn the_sewers_bring_their_own_two_basic_types() {
    let (mut game, land) = staged_with(cards::UNDERCITY_SEWERS, cards::MOUNTAIN);
    play_and_surveil(&mut game, land, false);

    let sewers = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::UNDERCITY_SEWERS)
        .expect("it is on the battlefield");
    let subtypes = game.effective_subtypes(sewers);

    assert!(subtypes.contains(&"Island"));
    assert!(subtypes.contains(&"Swamp"));
    assert!(sewers.tapped, "and it still arrives tapped");
}

/// Hedge Maze is the green-blue member, and the last of the cycle the cube
/// wants. The surveil is the same surveil; what is left to check is which
/// two colours it makes.
#[test]
fn the_hedge_maze_taps_for_its_own_two() {
    let (mut game, land) = staged_with(cards::HEDGE_MAZE, cards::LIGHTNING_BOLT);
    play_and_surveil(&mut game, land, false);
    let id = the_land_named(&game, cards::HEDGE_MAZE)
        .expect("it is on the battlefield")
        .card
        .id;
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == id)
    {
        permanent.tapped = false;
    }

    let colors = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateManaAbility { source, color, .. } if source == id => Some(color),
            _ => None,
        })
        .collect::<Vec<_>>();

    assert!(colors.contains(&ManaColor::Green), "Forest");
    assert!(colors.contains(&ManaColor::Blue), "Island");
    assert_eq!(colors.len(), 2, "and nothing else");
}

/// It surveils like the rest of them, which is the half worth checking once
/// per member rather than trusting the shared helper alone.
#[test]
fn the_hedge_maze_surveils_on_the_way_in() {
    let (mut game, land) = staged_with(cards::HEDGE_MAZE, cards::LIGHTNING_BOLT);
    play_and_surveil(&mut game, land, true);

    assert!(
        the_land_named(&game, cards::HEDGE_MAZE).is_some_and(|permanent| permanent.tapped),
        "tapped on arrival",
    );
    assert!(game.players[0].library.is_empty());
    assert_eq!(
        game.players[0]
            .graveyard
            .iter()
            .filter(|card| card.definition == cards::LIGHTNING_BOLT)
            .count(),
        1,
    );
}
