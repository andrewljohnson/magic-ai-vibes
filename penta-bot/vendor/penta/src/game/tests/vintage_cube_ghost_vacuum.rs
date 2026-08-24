//! Ghost Vacuum: one mana of graveyard hate that the deck playing it can
//! cash in for a board of fliers.

use super::*;

/// Player One with a Vacuum out since last turn, and `mine`/`theirs` in the
/// two graveyards.
fn staged(mine: &[CardDefinitionId], theirs: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    for seat in [PlayerId::One, PlayerId::Two] {
        game.players[seat.index()].graveyard.clear();
    }
    for (seat, definitions) in [(PlayerId::One, mine), (PlayerId::Two, theirs)] {
        for definition in definitions {
            let card = game
                .build_zone(seat, &[*definition])
                .expect("cataloged")
                .into_iter()
                .next()
                .expect("one card");
            game.players[seat.index()].graveyard.push(card);
        }
    }
    let vacuum = game
        .put_onto_battlefield(PlayerId::One, cards::GHOST_VACUUM)
        .expect("cataloged");
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [2, 1];
    drain_pending(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, vacuum)
}

fn settle(game: &mut Game) {
    for _ in 0..24 {
        if !game.pending_decisions.is_empty() {
            return;
        }
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

/// Untaps the Vacuum so it can eat again this turn.
fn untap(game: &mut Game, vacuum: GameObjectId) {
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == vacuum)
    {
        permanent.tapped = false;
    }
}

/// Eats the graveyard card of `definition` from `owner`'s graveyard.
fn eat(game: &mut Game, vacuum: GameObjectId, owner: PlayerId, definition: CardDefinitionId) {
    untap(game, vacuum);
    let card = game.players[owner.index()]
        .graveyard
        .iter()
        .find(|card| card.definition == definition)
        .unwrap_or_else(|| panic!("{definition:?} is in that graveyard"))
        .id;
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == vacuum
                    && targets
                        .iter()
                        .any(|selection| selection.targets().contains(&Target::Card(card)))
            }
            _ => false,
        })
        .unwrap_or_else(|| panic!("the Vacuum can point at {definition:?}"));
    game.apply(PlayerId::One, action).expect("it activates");
    settle(game);
}

/// The cash-in activation, if it is on offer.
fn cash_in(game: &Game, vacuum: GameObjectId) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source,
                ability: AbilityOrigin::Printed { ability, .. },
                ..
            } => *source == vacuum && *ability == AbilityId(1),
            _ => false,
        })
}

fn on_battlefield(game: &Game, definition: CardDefinitionId) -> Option<&Permanent> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == definition)
}

/// A tap takes a card out of either graveyard and puts it in exile.
#[test]
fn it_exiles_a_card_from_a_graveyard() {
    let (mut game, vacuum) = staged(&[], &[cards::SERRA_ANGEL]);

    eat(&mut game, vacuum, PlayerId::Two, cards::SERRA_ANGEL);

    assert!(
        game.players[1].graveyard.is_empty(),
        "the Angel left their graveyard",
    );
    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::SERRA_ANGEL),
        "and is in its owner's exile",
    );
}

/// Cashing in brings back what it ate, under you, as a 1/1 flier.
#[test]
fn cashing_in_returns_the_creature_cards_as_flying_spirits() {
    let (mut game, vacuum) = staged(&[], &[cards::SERRA_ANGEL]);
    eat(&mut game, vacuum, PlayerId::Two, cards::SERRA_ANGEL);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 6);
    untap(&mut game, vacuum);

    let action = cash_in(&game, vacuum).expect("six mana and a tap buys the board");
    game.apply(PlayerId::One, action).expect("it activates");
    settle(&mut game);

    let angel = on_battlefield(&game, cards::SERRA_ANGEL).expect("the Angel came back");
    assert_eq!(
        angel.controller,
        PlayerId::One,
        "under your control, not its owner's",
    );
    assert_eq!(
        angel.counters(CounterKind::Flying),
        1,
        "with a flying counter on it",
    );
    assert_eq!(
        (game.power(angel), game.toughness(angel)),
        (Some(1), Some(1)),
        "a 1/1 rather than a 4/4",
    );
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == vacuum),
        "and the Vacuum sacrificed itself to do it",
    );
}

/// The flying counter is what grants the flying, so a creature that had none
/// of its own has it now.
#[test]
fn the_flying_counter_grants_flying() {
    let (mut game, vacuum) = staged(&[], &[cards::GRIZZLY_BEARS]);
    eat(&mut game, vacuum, PlayerId::Two, cards::GRIZZLY_BEARS);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 6);
    untap(&mut game, vacuum);
    let action = cash_in(&game, vacuum).expect("it is activatable");
    game.apply(PlayerId::One, action).expect("it activates");
    settle(&mut game);

    let bears = on_battlefield(&game, cards::GRIZZLY_BEARS).expect("the Bears came back");
    assert!(
        game.permanent_has_executable_keyword(bears, KeywordAbility::Flying),
        "a Grizzly Bears with a flying counter flies",
    );
}

/// "In addition to its other types": it keeps the subtypes it printed.
#[test]
fn it_keeps_its_own_creature_types() {
    let (mut game, vacuum) = staged(&[], &[cards::SERRA_ANGEL]);
    eat(&mut game, vacuum, PlayerId::Two, cards::SERRA_ANGEL);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 6);
    untap(&mut game, vacuum);
    let action = cash_in(&game, vacuum).expect("it is activatable");
    game.apply(PlayerId::One, action).expect("it activates");
    settle(&mut game);

    let angel = on_battlefield(&game, cards::SERRA_ANGEL).expect("the Angel came back");
    let subtypes = game.effective_subtypes(angel);
    assert!(subtypes.contains(&"Angel"), "still an Angel: {subtypes:?}");
    assert!(subtypes.contains(&"Spirit"), "and a Spirit as well");
}

/// "Each creature card": a noncreature card it ate stays in exile.
#[test]
fn a_noncreature_card_it_ate_stays_exiled() {
    let (mut game, vacuum) = staged(&[], &[cards::SERRA_ANGEL, cards::LIGHTNING_BOLT]);
    eat(&mut game, vacuum, PlayerId::Two, cards::SERRA_ANGEL);
    eat(&mut game, vacuum, PlayerId::Two, cards::LIGHTNING_BOLT);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 6);
    untap(&mut game, vacuum);
    let action = cash_in(&game, vacuum).expect("it is activatable");
    game.apply(PlayerId::One, action).expect("it activates");
    settle(&mut game);

    assert!(
        on_battlefield(&game, cards::SERRA_ANGEL).is_some(),
        "the creature card came back",
    );
    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::LIGHTNING_BOLT),
        "and the Bolt stayed where it was",
    );
}

/// "Exiled with this artifact": a card that got to exile some other way is
/// not one of them.
#[test]
fn it_does_not_return_a_card_it_did_not_eat() {
    let (mut game, vacuum) = staged(&[], &[cards::SERRA_ANGEL]);
    let stranger = game
        .build_zone(PlayerId::Two, &[cards::GRIZZLY_BEARS])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[1].exile.push(stranger);
    eat(&mut game, vacuum, PlayerId::Two, cards::SERRA_ANGEL);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 6);
    untap(&mut game, vacuum);
    let action = cash_in(&game, vacuum).expect("it is activatable");
    game.apply(PlayerId::One, action).expect("it activates");
    settle(&mut game);

    assert!(
        on_battlefield(&game, cards::GRIZZLY_BEARS).is_none(),
        "the Vacuum never ate that one",
    );
}

/// "Activate only as a sorcery": not on their turn.
#[test]
fn the_cash_in_is_sorcery_speed() {
    let (mut game, vacuum) = staged(&[], &[cards::SERRA_ANGEL]);
    eat(&mut game, vacuum, PlayerId::Two, cards::SERRA_ANGEL);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 6);
    untap(&mut game, vacuum);
    assert!(cash_in(&game, vacuum).is_some(), "your own main phase");

    game.active_player = PlayerId::Two;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 6);

    assert!(cash_in(&game, vacuum).is_none(), "and nowhere else");
}
