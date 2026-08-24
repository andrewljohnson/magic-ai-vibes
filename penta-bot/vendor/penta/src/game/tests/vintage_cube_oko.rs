//! Oko, Thief of Crowns: a three-mana planeswalker that turns anything into
//! a 3/3 Elk and then trades it for something better.

use super::*;

fn staged(board: &[(CardDefinitionId, PlayerId)]) -> (Game, GameObjectId, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    let mut ids = Vec::new();
    for (index, (definition, controller)) in board.iter().enumerate() {
        let permanent = creature(
            97_000 + u32::try_from(index).expect("few permanents"),
            *definition,
            *controller,
        );
        ids.push(permanent.card.id);
        game.battlefield.push(permanent);
    }
    let oko = game
        .put_onto_battlefield(PlayerId::One, cards::OKO_THIEF_OF_CROWNS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, oko, ids)
}

fn loyalty_action(game: &Game, oko: GameObjectId, wanted: &[GameObjectId]) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == oko
                    && wanted.iter().all(|wanted| {
                        targets
                            .iter()
                            .flat_map(crate::casting::TargetSelection::targets)
                            .any(|chosen| *chosen == Target::Permanent(*wanted))
                    })
                    && targets
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .count()
                        == wanted.len()
            }
            _ => false,
        })
}

fn permanent(game: &Game, id: GameObjectId) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("it is on the battlefield")
}

/// The Elk maker: a Mox becomes a 3/3 green creature with nothing printed
/// on it, and stays one.
#[test]
fn it_turns_an_artifact_into_an_elk() {
    let (mut game, oko, ids) = staged(&[(cards::MOX_JET, PlayerId::One)]);
    let mox = ids[0];

    let elkify = loyalty_action(&game, oko, &[mox]).expect("+1 names it");
    game.apply(PlayerId::One, elkify).expect("it activates");
    drain_pending(&mut game);

    let elk = permanent(&game, mox);
    assert_eq!((game.power(elk), game.toughness(elk)), (Some(3), Some(3)));
    assert!(game.effective_subtypes(elk).contains(&"Elk"));
    assert!(
        game.permanent_types(elk)
            .is_some_and(|types| types.contains(CardType::Creature)),
    );
    assert!(
        game.mana_ability_activations(elk).is_empty(),
        "the Mox has lost the ability that made it worth playing",
    );

    // It is not an until-end-of-turn effect: the Elk is still an Elk after
    // cleanup.
    game.cleanup();
    let elk = permanent(&game, mox);
    assert_eq!((game.power(elk), game.toughness(elk)), (Some(3), Some(3)));
}

/// The +2 makes Food.
#[test]
fn it_makes_food() {
    let (mut game, oko, _) = staged(&[]);

    let food = loyalty_action(&game, oko, &[]).expect("+2 needs no target");
    game.apply(PlayerId::One, food).expect("it activates");
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| game.effective_subtypes(permanent).contains(&"Food")),
    );
}

/// The ultimate swaps something of yours for something small of theirs.
#[test]
fn the_ultimate_exchanges_control() {
    let (mut game, oko, ids) = staged(&[
        (cards::MOX_JET, PlayerId::One),
        (cards::GRIZZLY_BEARS, PlayerId::Two),
        (cards::SERRA_ANGEL, PlayerId::Two),
    ]);
    let (mox, bears, angel) = (ids[0], ids[1], ids[2]);
    if let Some(walker) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == oko)
    {
        walker.set_counters(CounterKind::Loyalty, 5);
    }

    assert!(
        loyalty_action(&game, oko, &[mox, angel]).is_none(),
        "a four-power Angel is out of reach",
    );
    let exchange = loyalty_action(&game, oko, &[mox, bears]).expect("the bears are small enough");
    game.apply(PlayerId::One, exchange).expect("it activates");
    drain_pending(&mut game);

    assert_eq!(permanent(&game, mox).controller, PlayerId::Two);
    assert_eq!(permanent(&game, bears).controller, PlayerId::One);
}
