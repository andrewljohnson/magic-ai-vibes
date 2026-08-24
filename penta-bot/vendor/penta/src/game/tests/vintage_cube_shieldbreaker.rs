//! Embereth Shieldbreaker: an artifact answered now and a body kept for
//! later, off one card.

use super::*;

/// Resolves whatever is on the stack, answering nothing.
fn resolve(game: &mut Game) {
    for _ in 0..8 {
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
}

fn cast_with(game: &Game, card: GameObjectId, option: PlayOptionId) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell {
                card: id, choices, ..
            } => *id == card && choices.play_option() == option,
            _ => false,
        })
}

/// The Shieldbreaker in hand, an artifact on their side, and five mana.
fn staged() -> (Game, CardInstanceId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let lotus = game
        .put_onto_battlefield(PlayerId::Two, cards::BLACK_LOTUS)
        .expect("cataloged");
    drain_pending(&mut game);
    let knight = card(83_000, cards::EMBERETH_SHIELDBREAKER, PlayerId::One);
    let knight_id = knight.id;
    game.players[0].hand.push(knight);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 3);
    (game, knight_id, lotus)
}

/// Both halves are on offer from hand.
#[test]
fn both_halves_are_castable_from_hand() {
    let (game, knight, _lotus) = staged();

    assert!(
        cast_with(&game, knight, PlayOptionId::DEFAULT).is_some(),
        "the creature",
    );
    assert!(
        cast_with(&game, knight, PlayOptionId(1)).is_some(),
        "and the adventure",
    );
}

/// The adventure destroys the artifact and leaves the card in exile rather
/// than the graveyard.
#[test]
fn the_adventure_destroys_an_artifact_and_exiles_itself() {
    let (mut game, knight, lotus) = staged();

    let cast = cast_with(&game, knight, PlayOptionId(1)).expect("one red casts it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    resolve(&mut game);
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != lotus),
        "the artifact is destroyed",
    );
    assert!(
        game.players[0]
            .exile
            .iter()
            .any(|card| card.definition == cards::EMBERETH_SHIELDBREAKER),
        "and the card waits in exile",
    );
    assert!(
        game.players[0]
            .graveyard
            .iter()
            .all(|card| card.definition != cards::EMBERETH_SHIELDBREAKER),
        "rather than in the graveyard",
    );
}

/// From exile only the creature half may be cast, and casting it puts a
/// 2/1 Knight onto the battlefield.
#[test]
fn the_creature_can_be_cast_from_exile_afterwards() {
    let (mut game, knight, _lotus) = staged();
    let cast = cast_with(&game, knight, PlayOptionId(1)).expect("one red casts it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    resolve(&mut game);
    drain_pending(&mut game);

    let exiled = game.players[0]
        .exile
        .iter()
        .find(|card| card.definition == cards::EMBERETH_SHIELDBREAKER)
        .map(|card| card.id)
        .expect("it is in exile");
    assert!(
        cast_with(&game, exiled, PlayOptionId(1)).is_none(),
        "the adventure is spent",
    );
    let recast = cast_with(&game, exiled, PlayOptionId::DEFAULT)
        .expect("the creature is castable from exile");
    game.apply(PlayerId::One, recast).expect("it is cast");
    resolve(&mut game);
    drain_pending(&mut game);

    let body = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::EMBERETH_SHIELDBREAKER)
        .expect("the Knight arrived");
    assert_eq!((game.power(body), game.toughness(body)), (Some(2), Some(1)));
}

/// With no artifact anywhere the adventure has no legal target and is not
/// offered; the creature still is.
#[test]
fn the_adventure_needs_an_artifact_to_point_at() {
    let mut game = ready_game();
    game.battlefield.clear();
    let knight = card(83_010, cards::EMBERETH_SHIELDBREAKER, PlayerId::One);
    let knight_id = knight.id;
    game.players[0].hand.push(knight);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 2);

    assert!(
        cast_with(&game, knight_id, PlayOptionId(1)).is_none(),
        "nothing to destroy",
    );
    assert!(
        cast_with(&game, knight_id, PlayOptionId::DEFAULT).is_some(),
        "but the body is always castable",
    );
}
