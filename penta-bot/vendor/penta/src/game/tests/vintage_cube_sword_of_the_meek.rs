//! Sword of the Meek: an Equipment that climbs out of the graveyard and
//! straps itself to whatever 1/1 just showed up.

use super::*;

/// Player One with a Sword in the graveyard and nothing on the battlefield.
fn staged() -> Game {
    let mut game = ready_game();
    game.battlefield.clear();
    let sword = game
        .build_zone(PlayerId::One, &[cards::SWORD_OF_THE_MEEK])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[0].graveyard.push(sword);
    game
}

/// Answers every pending decision, saying yes to any "may", then resolves
/// whatever is left on the stack.
fn settle(game: &mut Game, accept: bool) {
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let wanted = if accept {
                decision
                    .options
                    .iter()
                    .find(|option| option.label != "Decline")
            } else {
                decision
                    .options
                    .iter()
                    .find(|option| option.label == "Decline")
            };
            let options = wanted
                .or_else(|| decision.options.first())
                .map(|option| vec![option.id])
                .unwrap_or_default();
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("the decision accepts what it offered");
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

fn sword_on_battlefield(game: &Game) -> Option<&Permanent> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::SWORD_OF_THE_MEEK)
}

fn power_toughness(game: &Game, id: GameObjectId) -> (Option<i16>, Option<i16>) {
    let Some(permanent) = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
    else {
        return (None, None);
    };
    (game.power(permanent), game.toughness(permanent))
}

/// A 1/1 arriving brings the Sword back and wears it.
#[test]
fn a_one_one_pulls_the_sword_out_of_the_graveyard() {
    let mut game = staged();
    let servant = game
        .put_onto_battlefield(PlayerId::One, cards::MERFOLK_OF_THE_PEARL_TRIDENT)
        .expect("cataloged");
    settle(&mut game, true);

    let sword = sword_on_battlefield(&game).expect("the Sword came back");
    assert_eq!(
        sword.attached_to,
        Some(servant),
        "and attached itself to what brought it",
    );
    assert!(
        game.players[0].graveyard.is_empty(),
        "it left the graveyard behind",
    );
    assert_eq!(
        power_toughness(&game, servant),
        (Some(2), Some(3)),
        "a 1/1 wearing +1/+2",
    );
}

/// Declining leaves it where it was.
#[test]
fn declining_leaves_the_sword_in_the_graveyard() {
    let mut game = staged();
    game.put_onto_battlefield(PlayerId::One, cards::MERFOLK_OF_THE_PEARL_TRIDENT)
        .expect("cataloged");
    settle(&mut game, false);

    assert!(sword_on_battlefield(&game).is_none());
    assert_eq!(game.players[0].graveyard.len(), 1);
}

/// A creature that is not a 1/1 does not wake it up.
#[test]
fn a_bigger_creature_does_not_wake_it() {
    let mut game = staged();
    game.put_onto_battlefield(PlayerId::One, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    settle(&mut game, true);

    assert!(sword_on_battlefield(&game).is_none(), "a 2/2 is not a 1/1");
    assert_eq!(game.players[0].graveyard.len(), 1);
}

/// Nor does an opponent's 1/1: the clause says a creature you control.
#[test]
fn an_opponents_one_one_does_not_wake_it() {
    let mut game = staged();
    game.put_onto_battlefield(PlayerId::Two, cards::MERFOLK_OF_THE_PEARL_TRIDENT)
        .expect("cataloged");
    settle(&mut game, true);

    assert!(sword_on_battlefield(&game).is_none());
    assert_eq!(game.players[0].graveyard.len(), 1);
}
