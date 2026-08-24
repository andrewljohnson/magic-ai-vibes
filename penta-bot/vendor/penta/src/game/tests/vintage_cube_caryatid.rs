//! Sylvan Caryatid: a wall that makes any colour and cannot be pointed at.

use super::*;

fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let caryatid = game
        .put_onto_battlefield(PlayerId::One, cards::SYLVAN_CARYATID)
        .expect("cataloged");
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == caryatid)
    {
        permanent.entered_controller_turn = 0;
    }
    drain_pending(&mut game);
    (game, caryatid)
}

/// Every colour it will tap for.
fn colors_offered(game: &Game, source: GameObjectId) -> Vec<ManaColor> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateManaAbility {
                source: id, color, ..
            } if id == source => Some(color),
            _ => None,
        })
        .collect()
}

/// All five, and nothing colourless.
#[test]
fn it_taps_for_any_colour() {
    let (game, caryatid) = staged();
    let colors = colors_offered(&game, caryatid);

    for color in [
        ManaColor::White,
        ManaColor::Blue,
        ManaColor::Black,
        ManaColor::Red,
        ManaColor::Green,
    ] {
        assert!(colors.contains(&color), "{color:?} is a colour");
    }
    assert!(
        !colors.contains(&ManaColor::Colorless),
        "colourless is not one",
    );
}

/// Defender keeps it home however long it has been around.
#[test]
fn defender_keeps_it_out_of_combat() {
    let (mut game, caryatid) = staged();
    game.active_player = PlayerId::One;
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;

    assert!(
        game.legal_actions(PlayerId::One)
            .iter()
            .all(|action| !matches!(
                action,
                Action::DeclareAttacker { attacker, .. } if *attacker == caryatid
            )),
        "a 0/3 with defender never attacks",
    );
}

/// Hexproof is what the card is for: an opponent's removal cannot name it.
#[test]
fn an_opponent_cannot_target_it() {
    let (mut game, caryatid) = staged();
    let bolt = game
        .build_zone(PlayerId::Two, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[1].hand.push(bolt);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::Red, 1);
    game.priority = PlayerId::Two;

    let casts = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .filter(|action| matches!(action, Action::CastSpell { .. }))
        .collect::<Vec<_>>();
    assert!(!casts.is_empty(), "the Bolt is castable at something");
    assert!(
        casts.iter().all(|action| match action {
            Action::CastSpell { choices, .. } => choices
                .iter_targets()
                .all(|target| *target != Target::Permanent(caryatid)),
            _ => true,
        }),
        "but never at the Caryatid",
    );
}

/// Hexproof stops opponents, not you.
#[test]
fn its_own_controller_may_target_it() {
    let (mut game, caryatid) = staged();
    let bolt = game
        .build_zone(PlayerId::One, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[0].hand.push(bolt);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);
    game.priority = PlayerId::One;

    assert!(
        game.legal_actions(PlayerId::One)
            .iter()
            .any(|action| match action {
                Action::CastSpell { choices, .. } => choices
                    .iter_targets()
                    .any(|target| *target == Target::Permanent(caryatid)),
                _ => false,
            }),
        "you may always point your own spells at it",
    );
}
