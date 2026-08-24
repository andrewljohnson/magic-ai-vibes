//! Tamiyo, Collector of Tales: two prohibitions in one sentence, and a dig
//! that sorts by a name chosen before the cards are seen.

use super::*;

/// Tamiyo, with the loyalty she would have entered carrying. A permanent
/// built by hand has none, and a planeswalker at zero is binned by
/// state-based actions before anything can read its static ability.
fn tamiyo(id: u32) -> Permanent {
    let mut permanent = creature(id, cards::TAMIYO_COLLECTOR_OF_TALES, PlayerId::One);
    permanent.add_counters(CounterKind::Loyalty, 5);
    permanent
}

/// Resolves whatever is on the stack, answering nothing.
fn resolve(game: &mut Game) {
    for _ in 0..12 {
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
}

/// An opponent's discard spell takes nothing; the same spell cast by
/// Tamiyo's own controller still discards.
#[test]
fn tamiyo_stops_an_opponents_discard_but_not_your_own() {
    for caster in [PlayerId::Two, PlayerId::One] {
        let mut game = ready_game();
        game.battlefield.clear();
        game.battlefield.push(tamiyo(98_000));
        game.players[0].hand.clear();
        for id in 98_001..98_005 {
            game.players[0]
                .hand
                .push(card(id, cards::GRIZZLY_BEARS, PlayerId::One));
        }
        let hymn = card(98_010, cards::HYMN_TO_TOURACH, caster);
        let hymn_id = hymn.id;
        game.players[caster.index()].hand.push(hymn);
        game.add_unrestricted_mana(caster, ManaColor::Black, 2);
        game.priority = caster;
        game.active_player = caster;
        let held = game.players[0].hand.len();

        let cast = game
            .legal_actions(caster)
            .into_iter()
            .find(|action| match action {
                Action::CastSpell { card, choices, .. } => {
                    *card == hymn_id
                        && choices
                            .iter_targets()
                            .any(|target| *target == Target::Player(PlayerId::One))
                }
                _ => false,
            })
            .expect("the Hymn can point at Tamiyo's controller");
        game.apply(caster, cast).expect("it is cast");
        resolve(&mut game);
        drain_pending(&mut game);

        let taken = held - game.players[0].hand.len();
        if caster == PlayerId::One {
            // The Hymn itself left their hand too, so two discards plus the
            // spell is three cards gone.
            assert_eq!(taken, 3, "your own spell still discards");
        } else {
            assert_eq!(taken, 0, "an opponent's spell takes nothing");
        }
    }
}

/// An opponent cannot make Tamiyo's controller sacrifice either.
#[test]
fn tamiyo_stops_an_opponents_sacrifice() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.battlefield.push(tamiyo(98_010));
    game.battlefield
        .push(creature(98_011, cards::GRIZZLY_BEARS, PlayerId::One));

    assert!(
        !game.can_be_forced_to_sacrifice(PlayerId::One, PlayerId::Two),
        "an opponent cannot",
    );
    assert!(
        game.can_be_forced_to_sacrifice(PlayerId::One, PlayerId::One),
        "and their own spell still can",
    );
}

/// The +1 sorts the top four by a name chosen before they are seen: matches
/// to hand, everything else to the graveyard.
#[test]
fn the_plus_one_sorts_the_top_four_by_the_chosen_name() {
    let mut game = ready_game();
    game.battlefield.clear();
    let planeswalker = tamiyo(98_020);
    let tamiyo_id = planeswalker.card.id;
    game.battlefield.push(planeswalker);
    game.players[0].hand.clear();
    game.players[0].library.clear();
    // Top four, pushed last-is-top: two Bolts and two Bears.
    for id in 98_021..98_023 {
        game.players[0]
            .library
            .push(card(id, cards::GRIZZLY_BEARS, PlayerId::One));
    }
    for id in 98_023..98_025 {
        game.players[0]
            .library
            .push(card(id, cards::LIGHTNING_BOLT, PlayerId::One));
    }

    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(action, Action::ActivateAbility { source, .. } if *source == tamiyo_id)
                && matches!(action, Action::ActivateAbility { ability, .. }
                    if game
                        .ability_for_origin(tamiyo_id, *ability)
                        .is_some_and(|ability| ability.text.starts_with("+1")))
        })
        .expect("the +1 is available");
    game.apply(PlayerId::One, action).expect("it is activated");
    resolve(&mut game);

    let naming = game
        .pending_decisions
        .first()
        .map(|pending| pending.observation.clone())
        .expect("the name is chosen first");
    let bolt = naming
        .options
        .iter()
        .find(|option| option.label.contains("Lightning Bolt"))
        .expect("Lightning Bolt is a nonland name that can be chosen");
    game.apply(
        PlayerId::One,
        Action::ChooseDecision {
            decision: naming.id,
            options: vec![bolt.id],
        },
    )
    .expect("naming a card is legal");
    drain_pending(&mut game);

    assert_eq!(
        game.players[0]
            .hand
            .iter()
            .filter(|card| card.definition == cards::LIGHTNING_BOLT)
            .count(),
        2,
        "both cards with the chosen name reach the hand",
    );
    assert_eq!(
        game.players[0]
            .graveyard
            .iter()
            .filter(|card| card.definition == cards::GRIZZLY_BEARS)
            .count(),
        2,
        "and the rest go to the graveyard, not back on the library",
    );
    assert!(
        game.players[0].library.is_empty(),
        "all four left the library",
    );
}
