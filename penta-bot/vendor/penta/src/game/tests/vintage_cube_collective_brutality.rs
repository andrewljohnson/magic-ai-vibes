//! Collective Brutality: one mode is free and every one after it costs a
//! card out of your own hand.

use super::*;

/// Player One holding a Brutality plus `spare` other cards, Player Two
/// holding a Lightning Bolt and running a Grizzly Bears out. All three modes
/// need something to point at before any of them is on offer.
fn staged(spare: usize) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.put_onto_battlefield(PlayerId::Two, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.players[0].hand.clear();
    game.players[1].hand.clear();
    let bolt = game
        .build_zone(PlayerId::Two, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[1].hand.push(bolt);

    let brutality = game
        .build_zone(PlayerId::One, &[cards::COLLECTIVE_BRUTALITY])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let id = brutality.id;
    game.players[0].hand.push(brutality);
    for _ in 0..spare {
        let card = game
            .build_zone(PlayerId::One, &[cards::MOUNTAIN])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].hand.push(card);
    }
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 2);
    game.priority = PlayerId::One;
    (game, id)
}

/// How many modes each offered cast of the Brutality takes, paired with how
/// many cards it discards to do it.
fn offers(game: &Game, brutality: GameObjectId) -> Vec<(usize, usize)> {
    let mut seen = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::CastSpell {
                card,
                choices,
                sacrifices,
            } if card == brutality => Some((choices.modes().len(), sacrifices.len())),
            _ => None,
        })
        .collect::<Vec<_>>();
    seen.sort_unstable();
    seen.dedup();
    seen
}

fn resolve(game: &mut Game) {
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .iter()
                .take(decision.minimum.max(1))
                .map(|option| option.id)
                .collect();
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

/// One mode is free, two cost a card, three cost two.
#[test]
fn each_mode_past_the_first_costs_a_card() {
    let (game, brutality) = staged(2);
    let counts = offers(&game, brutality);

    assert!(counts.contains(&(1, 0)), "one mode discards nothing");
    assert!(counts.contains(&(2, 1)), "two modes discard one");
    assert!(counts.contains(&(3, 2)), "three modes discard two");
    assert!(
        counts
            .iter()
            .all(|(modes, discards)| *discards == modes - 1),
        "every offer pays one card per mode past the first",
    );
}

/// With nothing to spare, only the free mode is on offer.
#[test]
fn an_empty_hand_can_only_afford_one_mode() {
    let (game, brutality) = staged(0);
    let counts = offers(&game, brutality);

    assert!(!counts.is_empty(), "one mode is always castable");
    assert!(
        counts.iter().all(|(modes, _)| *modes == 1),
        "there is nothing left to escalate with",
    );
}

/// One spare card reaches two modes and no further.
#[test]
fn one_spare_card_buys_exactly_one_extra_mode() {
    let (game, brutality) = staged(1);
    let counts = offers(&game, brutality);

    assert!(counts.iter().any(|(modes, _)| *modes == 2));
    assert!(
        counts.iter().all(|(modes, _)| *modes <= 2),
        "three modes wants two cards",
    );
}

/// The drain mode does what it says, and the escalate cost is really paid.
#[test]
fn escalating_discards_the_card_and_both_modes_resolve() {
    let (mut game, brutality) = staged(1);
    let drain = ModeId::from_index(2).expect("the third mode");
    let shrink = ModeId::from_index(1).expect("the second mode");
    let bears = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::GRIZZLY_BEARS)
        .expect("staged")
        .card
        .id;

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == brutality && choices.modes() == [shrink, drain]
            }
            _ => false,
        })
        .expect("shrink and drain together");
    game.apply(PlayerId::One, cast).expect("it is castable");
    resolve(&mut game);
    game.check_state_based_actions();

    assert_eq!(game.players[1].life, 18, "two lost");
    assert_eq!(game.players[0].life, 22, "and two gained");
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == bears),
        "a 2/2 given -2/-2 is a 0/0",
    );
    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::MOUNTAIN),
        "and the escalate cost was really discarded",
    );
}
