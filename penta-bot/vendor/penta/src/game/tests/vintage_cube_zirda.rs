//! Zirda, the Dawnwaker: two mana off every activated ability, and a tap
//! that walks something past a blocker.

use super::*;

fn staged(extra: &[CardDefinitionId]) -> (Game, GameObjectId, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    let mut ids = Vec::new();
    for (index, definition) in extra.iter().enumerate() {
        let permanent = creature(
            109_000 + u32::try_from(index).expect("few permanents"),
            *definition,
            PlayerId::One,
        );
        ids.push(permanent.card.id);
        game.battlefield.push(permanent);
    }
    let zirda = game
        .put_onto_battlefield(PlayerId::One, cards::ZIRDA_THE_DAWNWAKER)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, zirda, ids)
}

/// A 3/3 for three, and the discount applies to its own ability: {1} minus
/// {2} floors at one mana, so it still costs {1}.
#[test]
fn its_own_ability_still_costs_one() {
    let (mut game, zirda, _) = staged(&[]);
    let fox = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == zirda)
        .expect("it is there");
    assert_eq!((game.power(fox), game.toughness(fox)), (Some(3), Some(3)));

    assert!(
        game.legal_actions(PlayerId::One).iter().all(
            |action| !matches!(action, Action::ActivateAbility { source, .. } if *source == zirda)
        ),
        "with no mana at all it is not activatable",
    );
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);
    assert!(
        game.legal_actions(PlayerId::One).iter().any(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == zirda)
        ),
        "the floor is one mana, and one mana pays it",
    );
}

/// A four-mana ability elsewhere costs two with Zirda out.
#[test]
fn it_takes_two_off_another_permanents_ability() {
    let (mut game, _, ids) = staged(&[cards::ICY_MANIPULATOR]);
    let icy = ids[0];
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == icy)
    {
        permanent.entered_controller_turn = 0;
    }
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    assert!(
        game.legal_actions(PlayerId::One).iter().any(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == icy)
        ),
        "a one-mana ability after the discount is payable with one mana",
    );
}

/// The tap stops a creature blocking for the turn.
#[test]
fn tapping_it_stops_a_blocker() {
    let (mut game, zirda, _) = staged(&[]);
    let blocker = creature(109_500, cards::SERRA_ANGEL, PlayerId::Two);
    let blocker_id = blocker.card.id;
    game.battlefield.push(blocker);
    let attacker = creature(109_501, cards::GRIZZLY_BEARS, PlayerId::One);
    let attacker_id = attacker.card.id;
    game.battlefield.push(attacker);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    let stop = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == zirda
                    && targets
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|chosen| *chosen == Target::Permanent(blocker_id))
            }
            _ => false,
        })
        .expect("the Angel is a legal target");
    game.apply(PlayerId::One, stop).expect("it activates");
    drain_pending(&mut game);

    game.step = Step::DeclareAttackers;
    game.declare_attacker(attacker_id, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    drain_pending(&mut game);

    assert!(
        !game.legal_actions(PlayerId::Two).iter().any(|action| {
            matches!(action, Action::DeclareBlocker { blocker, .. } if *blocker == blocker_id)
        }),
        "it cannot block this turn",
    );
}
