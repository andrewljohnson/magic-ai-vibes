//! Bonecrusher Giant, and the three things an Adventure card needs: a spell
//! that exiles itself for later, a permission to cast the other half from
//! there, and a turn in which damage cannot be prevented.

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

/// The one cast of `card` using the named play option, if it is on offer.
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

/// Both halves are castable from hand, and only from hand: the creature is
/// not yet anywhere else.
#[test]
fn bonecrusher_offers_both_halves_from_hand() {
    let mut game = ready_game();
    game.battlefield.clear();
    let giant = card(80_000, cards::BONECRUSHER_GIANT, PlayerId::One);
    let giant_id = giant.id;
    game.players[0].hand.push(giant);
    game.battlefield
        .push(creature(80_001, cards::GRIZZLY_BEARS, PlayerId::Two));
    game.players[0].mana_pool.red = 1;
    game.players[0].mana_pool.colorless = 2;

    assert!(
        cast_with(&game, giant_id, PlayOptionId::DEFAULT).is_some(),
        "the Giant is castable",
    );
    assert!(
        cast_with(&game, giant_id, PlayOptionId(1)).is_some(),
        "and so is Stomp",
    );
}

/// Stomp exiles itself on an adventure, and its owner may then cast the
/// creature from exile -- as the creature, never as Stomp again.
#[test]
fn stomp_goes_on_an_adventure_and_the_giant_comes_back_from_exile() {
    let mut game = ready_game();
    game.battlefield.clear();
    let giant = card(80_010, cards::BONECRUSHER_GIANT, PlayerId::One);
    let giant_id = giant.id;
    game.players[0].hand.push(giant);
    let bears = creature(80_011, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    game.players[0].mana_pool.red = 1;
    game.players[0].mana_pool.colorless = 1;

    let stomp = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == giant_id
                    && choices.play_option() == PlayOptionId(1)
                    && choices
                        .iter_targets()
                        .any(|target| *target == Target::Permanent(bears_id))
            }
            _ => false,
        })
        .expect("Stomp can point at the Bears");
    game.apply(PlayerId::One, stomp).expect("it is cast");
    resolve(&mut game);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == bears_id),
        "two damage kills a 2/2",
    );
    let exiled = game.players[0]
        .exile
        .iter()
        .find(|card| card.definition == cards::BONECRUSHER_GIANT)
        .expect("the card is exiled rather than put in the graveyard");
    let exiled_id = exiled.id;

    game.players[0].mana_pool.red = 1;
    game.players[0].mana_pool.colorless = 2;
    assert!(
        cast_with(&game, exiled_id, PlayOptionId(1)).is_none(),
        "the adventure cannot be taken twice",
    );
    let giant = cast_with(&game, exiled_id, PlayOptionId::DEFAULT)
        .expect("the creature may be cast from exile");
    game.apply(PlayerId::One, giant).expect("it is cast");
    resolve(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::BONECRUSHER_GIANT),
        "and it arrives as a creature",
    );
    assert!(game.players[0].exile.is_empty(), "leaving exile behind it");
}

/// Targeting the Giant costs two life whether or not the spell works.
#[test]
fn bonecrusher_burns_whoever_targets_it() {
    let mut game = ready_game();
    game.battlefield.clear();
    let giant = creature(80_020, cards::BONECRUSHER_GIANT, PlayerId::One);
    let giant_id = giant.card.id;
    game.battlefield.push(giant);
    let bolt = card(80_021, cards::LIGHTNING_BOLT, PlayerId::Two);
    let bolt_id = bolt.id;
    game.players[1].hand.push(bolt);
    game.players[1].mana_pool.red = 1;
    game.priority = PlayerId::Two;
    let before = game.players[1].life;

    let action = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == bolt_id
                    && choices
                        .iter_targets()
                        .any(|target| *target == Target::Permanent(giant_id))
            }
            _ => false,
        })
        .expect("the Bolt can point at the Giant");
    game.apply(PlayerId::Two, action).expect("it is cast");
    resolve(&mut game);

    assert_eq!(
        game.players[1].life,
        before - 2,
        "the trigger burns the Bolt's controller",
    );
}

/// "Damage can't be prevented this turn" turns off what protection prevents
/// (CR 702.16e), so Stomp reaches a creature with protection from red.
#[test]
fn stomp_reaches_through_protection() {
    let mut game = ready_game();
    game.battlefield.clear();
    let giant = card(80_030, cards::BONECRUSHER_GIANT, PlayerId::One);
    let giant_id = giant.id;
    game.players[0].hand.push(giant);
    game.players[0].mana_pool.red = 1;
    game.players[0].mana_pool.colorless = 1;
    game.players[1].life = 20;

    let stomp = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == giant_id
                    && choices.play_option() == PlayOptionId(1)
                    && choices
                        .iter_targets()
                        .any(|target| *target == Target::Player(PlayerId::Two))
            }
            _ => false,
        })
        .expect("Stomp can point at a player");
    game.apply(PlayerId::One, stomp).expect("it is cast");
    resolve(&mut game);

    assert_eq!(game.players[1].life, 18);
    assert!(
        game.damage_cannot_be_prevented_this_turn,
        "and the rule is in force for the rest of the turn",
    );
}
