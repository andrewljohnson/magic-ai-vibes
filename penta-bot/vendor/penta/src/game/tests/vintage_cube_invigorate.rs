//! Invigorate: four power for nothing at all, if the other player is
//! willing to be three life further away from dead.

use super::*;

/// Player One holding Invigorate, with a creature out and `lands` in play.
fn staged(lands: &[CardDefinitionId]) -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    for definition in lands {
        game.put_onto_battlefield(PlayerId::One, *definition)
            .expect("cataloged");
    }
    let bears = creature(270_000, cards::GRIZZLY_BEARS, PlayerId::One);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    drain_pending(&mut game);
    for permanent in &mut game.battlefield {
        permanent.tapped = true;
    }
    let spell = game
        .build_zone(PlayerId::One, &[cards::INVIGORATE])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let spell_id = spell.id;
    game.players[0].hand.push(spell);
    game.players[0].life = 20;
    game.players[1].life = 20;
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, spell_id, bears_id)
}

fn settle(game: &mut Game) {
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .iter()
                .map(|option| option.id)
                .take(decision.minimum.max(1))
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
    game.check_state_based_actions();
}

fn casts(game: &Game, spell: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::CastSpell { card, .. } if *card == spell))
        .collect()
}

/// With a Forest out and no mana at all, it is still castable: the other
/// player gains three and the creature gets +4/+4.
#[test]
fn a_forest_makes_it_free() {
    let (mut game, spell, bears) = staged(&[cards::FOREST]);

    let cast = casts(&game, spell)
        .into_iter()
        .next()
        .expect("the free cast is on offer");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);

    assert_eq!(game.players[1].life, 23, "they gained three");
    assert_eq!(game.players[0].life, 20, "and you paid nothing");
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == bears)
        .expect("the creature is there");
    assert_eq!(game.power(permanent), Some(6));
    assert_eq!(game.toughness(permanent), Some(6));
}

/// Without a Forest the alternative is not offered, and with no mana there
/// is no other way to cast it.
#[test]
fn without_a_forest_it_costs_mana() {
    let (mut game, spell, _bears) = staged(&[cards::MOUNTAIN]);

    assert!(
        casts(&game, spell).is_empty(),
        "no Forest and no mana is no cast",
    );

    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 3);
    let cast = casts(&game, spell)
        .into_iter()
        .next()
        .expect("three mana pays for it the ordinary way");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);

    assert_eq!(game.players[1].life, 20, "the mana cast gives them nothing");
}

/// The gain is a cost: it happens as the spell is cast, whatever becomes of
/// the spell afterwards.
#[test]
fn the_life_is_paid_on_casting() {
    let (mut game, spell, _bears) = staged(&[cards::FOREST]);

    let cast = casts(&game, spell)
        .into_iter()
        .next()
        .expect("the free cast is on offer");
    game.apply(PlayerId::One, cast).expect("it is cast");

    assert_eq!(
        game.players[1].life, 23,
        "paid while the spell is still on the stack"
    );
    assert_eq!(game.stack.len(), 1);
}
