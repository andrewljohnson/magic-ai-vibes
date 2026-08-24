//! Unearth: one black mana for a small creature you already paid for, and a
//! cycling cost for the games where the graveyard has nothing worth raising.

use super::*;

/// Unearth in hand with `graveyard` under player one's graveyard, three
/// black mana up, and a library to cycle into.
fn staged(graveyard: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.turns_started[PlayerId::One.index()] = 5;
    game.players[PlayerId::One.index()].hand.clear();
    game.players[PlayerId::One.index()].graveyard.clear();
    game.players[PlayerId::One.index()].library.clear();
    game.players[PlayerId::One.index()].library.push(card(
        85_000,
        cards::GIANT_GROWTH,
        PlayerId::One,
    ));
    for (index, definition) in graveyard.iter().enumerate() {
        game.players[PlayerId::One.index()].graveyard.push(card(
            85_100 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::One,
        ));
    }
    let unearth = card(85_200, cards::UNEARTH, PlayerId::One);
    let unearth_id = unearth.id;
    game.players[PlayerId::One.index()].hand.push(unearth);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 3);
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, unearth_id)
}

/// The cast that names `target`, if one is offered.
fn cast_at(game: &Game, unearth: GameObjectId, target: GameObjectId) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == unearth
                    && choices
                        .targets()
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|chosen| *chosen == Target::Card(target))
            }
            _ => false,
        })
}

fn graveyard_card(game: &Game, definition: CardDefinitionId) -> GameObjectId {
    game.players[PlayerId::One.index()]
        .graveyard
        .iter()
        .find(|card| card.definition == definition)
        .expect("it is in the graveyard")
        .id
}

fn resolve(game: &mut Game) {
    for _ in 0..8 {
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let priority = game.priority;
        if game.apply(priority, Action::PassPriority).is_err() {
            break;
        }
    }
}

/// A two-mana creature comes back onto the battlefield.
#[test]
fn it_returns_a_small_creature() {
    let (mut game, unearth) = staged(&[cards::GRIZZLY_BEARS]);
    let bears = graveyard_card(&game, cards::GRIZZLY_BEARS);

    let cast = cast_at(&game, unearth, bears).expect("a 2/2 is small enough");
    game.apply(PlayerId::One, cast).expect("it is cast");
    resolve(&mut game);

    assert!(
        game.battlefield.iter().any(
            |permanent| permanent.card.definition == cards::GRIZZLY_BEARS
                && permanent.controller == PlayerId::One
        ),
        "it comes back under your own control",
    );
    assert_eq!(
        game.players[PlayerId::One.index()]
            .graveyard
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::UNEARTH],
        "the creature left the graveyard and the sorcery took its place",
    );
}

/// Mana value four is one too many.
#[test]
fn it_will_not_reach_a_big_creature() {
    let (game, unearth) = staged(&[cards::SERRA_ANGEL]);
    let angel = graveyard_card(&game, cards::SERRA_ANGEL);

    assert!(
        cast_at(&game, unearth, angel).is_none(),
        "a five-mana Angel is past the bound",
    );
}

/// Exactly three is inside the bound, not outside it.
#[test]
fn three_is_small_enough() {
    let (mut game, unearth) = staged(&[cards::PHYREXIAN_ARENA, cards::HYPNOTIC_SPECTER]);
    let specter = graveyard_card(&game, cards::HYPNOTIC_SPECTER);
    assert!(cast_at(&game, unearth, specter).is_some());

    let arena = graveyard_card(&game, cards::PHYREXIAN_ARENA);
    assert!(
        cast_at(&game, unearth, arena).is_none(),
        "an enchantment is not a creature card whatever it costs",
    );

    let cast = cast_at(&game, unearth, specter).expect("offered above");
    game.apply(PlayerId::One, cast).expect("it is cast");
    resolve(&mut game);
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::HYPNOTIC_SPECTER),
    );
}

/// With nothing worth raising, it cycles instead.
#[test]
fn it_cycles_when_the_graveyard_is_empty() {
    let (mut game, unearth) = staged(&[]);
    let hand_before = game.players[PlayerId::One.index()].hand.len();

    let cycle = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == unearth),
        )
        .expect("cycling is offered from hand");
    game.apply(PlayerId::One, cycle).expect("it activates");
    resolve(&mut game);

    assert_eq!(
        game.players[PlayerId::One.index()]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::GIANT_GROWTH],
        "the card is discarded as a cost and one is drawn",
    );
    assert_eq!(hand_before, 1);
    assert!(
        game.players[PlayerId::One.index()]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::UNEARTH),
    );
}
