//! Mox Opal: free, and worth nothing until the board has caught up with it.

use super::*;

/// The Mox on the battlefield, with `others` more artifacts beside it.
fn staged(others: usize) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let opal = game
        .put_onto_battlefield(PlayerId::One, cards::MOX_OPAL)
        .expect("cataloged");
    for _ in 0..others {
        game.put_onto_battlefield(PlayerId::One, cards::MANIFOLD_KEY)
            .expect("cataloged");
    }
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, opal)
}

fn mana_action(game: &Game, source: GameObjectId, color: ManaColor) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateManaAbility {
                source: activated,
                color: made,
                ..
            } => *activated == source && *made == color,
            _ => false,
        })
}

/// Two artifacts is not metalcraft, whichever colour is asked for.
#[test]
fn two_artifacts_are_not_enough() {
    let (game, opal) = staged(1);

    for color in ManaColor::COLORS {
        assert!(
            mana_action(&game, opal, color).is_none(),
            "{color:?} is not on offer below three artifacts",
        );
    }
}

/// It counts itself: two others beside it is three.
#[test]
fn it_counts_itself_toward_the_three() {
    let (mut game, opal) = staged(2);

    let blue = mana_action(&game, opal, ManaColor::Blue).expect("metalcraft is on");
    game.apply(PlayerId::One, blue).expect("it activates");

    assert_eq!(game.players[0].mana.len(), 1);
    assert!(
        game.battlefield
            .iter()
            .find(|permanent| permanent.card.id == opal)
            .expect("still there")
            .tapped,
        "and it tapped to do it",
    );
}

/// Any colour means any of the five.
#[test]
fn it_makes_any_of_the_five_colors() {
    for color in ManaColor::COLORS {
        let (mut game, opal) = staged(2);

        let action = mana_action(&game, opal, color).unwrap_or_else(|| {
            panic!("{color:?} is one of the five");
        });
        game.apply(PlayerId::One, action).expect("it activates");

        assert_eq!(game.players[0].mana.len(), 1);
    }
}

/// Metalcraft is read live: an artifact leaving takes the ability with it.
#[test]
fn losing_an_artifact_turns_it_off() {
    let (mut game, opal) = staged(2);
    assert!(mana_action(&game, opal, ManaColor::Red).is_some());

    let other = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == ObjectKind::Card(cards::MANIFOLD_KEY))
        .map(|permanent| permanent.card.id)
        .expect("one of the two");
    game.battlefield
        .retain(|permanent| permanent.card.id != other);

    assert!(
        mana_action(&game, opal, ManaColor::Red).is_none(),
        "back down to two artifacts, and back to doing nothing",
    );
}

/// Legendary: the second copy is put into the graveyard by state-based
/// actions, and the one that stays is its controller's choice.
#[test]
fn a_second_copy_cannot_stay() {
    let (mut game, _opal) = staged(0);
    game.put_onto_battlefield(PlayerId::One, cards::MOX_OPAL)
        .expect("cataloged");
    drain_pending(&mut game);

    game.check_state_based_actions();
    for _ in 0..8 {
        let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        else {
            break;
        };
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
        game.check_state_based_actions();
    }

    assert_eq!(
        game.battlefield
            .iter()
            .filter(|permanent| permanent.card.definition == ObjectKind::Card(cards::MOX_OPAL))
            .count(),
        1,
        "the legend rule keeps one",
    );
    assert_eq!(
        game.players[0]
            .graveyard
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::MOX_OPAL],
    );
}
