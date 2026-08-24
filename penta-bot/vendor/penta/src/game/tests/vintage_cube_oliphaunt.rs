//! Oliphaunt: a six-mana trampler nobody casts for six mana, and the
//! Mountain it becomes for one.

use super::*;

/// Oliphaunt in hand with a library of Mountains, or on the battlefield
/// beside a bear when `deployed`.
fn staged(deployed: bool) -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[PlayerId::One.index()].hand.clear();
    game.players[PlayerId::One.index()].library.clear();
    for index in 0..3 {
        game.players[PlayerId::One.index()].library.push(card(
            88_000 + index,
            cards::MOUNTAIN,
            PlayerId::One,
        ));
    }
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    let bears = game
        .put_onto_battlefield(PlayerId::One, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    let oliphaunt = if deployed {
        game.put_onto_battlefield(PlayerId::One, cards::OLIPHAUNT)
            .expect("cataloged")
    } else {
        let instance = card(88_100, cards::OLIPHAUNT, PlayerId::One);
        let id = instance.id;
        game.players[PlayerId::One.index()].hand.push(instance);
        id
    };
    drain_pending(&mut game);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);
    (game, oliphaunt, bears)
}

fn permanent(game: &Game, id: GameObjectId) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("it is on the battlefield")
}

/// A 6/4 with trample.
#[test]
fn it_tramples() {
    let (game, oliphaunt, _) = staged(true);
    let beast = permanent(&game, oliphaunt);

    assert_eq!(
        (game.power(beast), game.toughness(beast)),
        (Some(6), Some(4))
    );
    assert!(game.permanent_has_executable_keyword(beast, KeywordAbility::Trample));
}

/// Attacking lends another creature two power and its trample.
#[test]
fn attacking_charges_something_else() {
    let (mut game, oliphaunt, bears) = staged(true);
    assert!(
        !game.permanent_has_executable_keyword(permanent(&game, bears), KeywordAbility::Trample)
    );

    game.step = Step::DeclareAttackers;
    game.declare_attacker(oliphaunt, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    drain_pending(&mut game);

    let charged = permanent(&game, bears);
    assert_eq!(game.power(charged), Some(4));
    assert_eq!(game.toughness(charged), Some(2), "only the power moves");
    assert!(game.permanent_has_executable_keyword(charged, KeywordAbility::Trample));
}

/// "Another": the Oliphaunt is not a legal target for its own trigger, so
/// its power is untouched.
#[test]
fn it_cannot_charge_itself() {
    let (mut game, oliphaunt, bears) = staged(true);
    game.battlefield
        .retain(|permanent| permanent.card.id != bears);

    game.step = Step::DeclareAttackers;
    game.declare_attacker(oliphaunt, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    drain_pending(&mut game);

    assert_eq!(game.power(permanent(&game, oliphaunt)), Some(6));
}

/// One mana and the card itself buys a Mountain from the library.
#[test]
fn mountaincycling_fetches_a_mountain() {
    let (mut game, oliphaunt, _) = staged(false);

    let cycle = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateAbility { source, .. } if *source == oliphaunt))
        .expect("mountaincycling is offered from hand");
    game.apply(PlayerId::One, cycle).expect("it activates");
    for _ in 0..10 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .iter()
                .map(|option| option.id)
                .take(decision.minimum.max(1).min(decision.maximum))
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

    assert_eq!(
        game.players[PlayerId::One.index()]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::MOUNTAIN],
        "the Elephant is spent and a land arrives instead",
    );
    assert_eq!(game.players[PlayerId::One.index()].library.len(), 2);
    assert!(
        game.players[PlayerId::One.index()]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::OLIPHAUNT),
    );
}
