//! Concealing Curtains: a one-mana wall that later turns over and takes the
//! best card out of the hand it was hiding from.

use super::*;

/// The Curtains on the battlefield with three mana up, and `hand` in player
/// two's hand.
fn staged(hand: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[PlayerId::Two.index()].hand.clear();
    for (index, definition) in hand.iter().enumerate() {
        let instance = card(
            84_000 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::Two,
        );
        game.players[PlayerId::Two.index()].hand.push(instance);
    }
    let curtains = game
        .put_onto_battlefield(PlayerId::One, cards::CONCEALING_CURTAINS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 3);
    game.step = Step::PrecombatMain;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    (game, curtains)
}

fn permanent(game: &Game, id: GameObjectId) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("it is on the battlefield")
}

fn transform(game: &Game, curtains: GameObjectId) -> Option<Action> {
    game.legal_actions(PlayerId::One).into_iter().find(
        |action| matches!(action, Action::ActivateAbility { source, .. } if *source == curtains),
    )
}

/// Answers whatever the transform trigger asks, taking `wanted` from the
/// revealed hand when it is offered and declining when it is not.
fn settle(game: &mut Game, wanted: Option<CardDefinitionId>) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = match (decision.minimum, wanted) {
                (0, Some(definition)) => decision
                    .options
                    .iter()
                    .filter(|option| {
                        matches!(
                            option.card,
                            Some((_, ObjectCharacteristics::Card { definition: shown, .. }))
                                if shown == definition
                        )
                    })
                    .map(|option| option.id)
                    .take(1)
                    .collect(),
                (0, None) => Vec::new(),
                _ => decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum.min(decision.maximum))
                    .collect(),
            };
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
}

/// The front face is a 0/4 that cannot attack.
#[test]
fn the_front_face_is_a_wall() {
    let (game, curtains) = staged(&[]);
    let front = permanent(&game, curtains);

    assert_eq!(
        (game.power(front), game.toughness(front)),
        (Some(0), Some(4))
    );
    assert!(game.permanent_has_executable_keyword(front, KeywordAbility::Defender));
}

/// Sorcery speed: not while something is on the stack.
#[test]
fn it_only_turns_over_as_a_sorcery() {
    let (mut game, curtains) = staged(&[]);
    assert!(transform(&game, curtains).is_some());

    game.step = Step::Upkeep;
    assert!(
        transform(&game, curtains).is_none(),
        "an upkeep is not a main phase",
    );
}

/// Turned over, it is a 3/4 with menace and the hand is one card lighter --
/// and then one card heavier, because the opponent draws.
#[test]
fn turning_over_takes_a_card_and_replaces_it() {
    let (mut game, curtains) = staged(&[cards::LIGHTNING_BOLT, cards::MOX_JET]);
    game.players[PlayerId::Two.index()].library.clear();
    game.players[PlayerId::Two.index()].library.push(card(
        84_500,
        cards::GIANT_GROWTH,
        PlayerId::Two,
    ));
    let action = transform(&game, curtains).expect("three mana pays for it");
    game.apply(PlayerId::One, action).expect("it activates");

    settle(&mut game, Some(cards::MOX_JET));

    let back = permanent(&game, curtains);
    assert_eq!((game.power(back), game.toughness(back)), (Some(3), Some(4)));
    assert!(game.permanent_has_executable_keyword(back, KeywordAbility::Menace));

    let hand = game.players[PlayerId::Two.index()]
        .hand
        .iter()
        .map(|card| card.definition)
        .collect::<Vec<_>>();
    assert_eq!(
        hand,
        vec![cards::LIGHTNING_BOLT, cards::GIANT_GROWTH],
        "the Mox is discarded and the replacement is drawn",
    );
    assert_eq!(
        game.players[PlayerId::Two.index()].graveyard.len(),
        1,
        "the card it took is in the graveyard",
    );
}

/// "You may": declining takes nothing, and then nothing is drawn either.
#[test]
fn declining_leaves_the_hand_alone() {
    let (mut game, curtains) = staged(&[cards::LIGHTNING_BOLT]);
    game.players[PlayerId::Two.index()].library.clear();
    game.players[PlayerId::Two.index()].library.push(card(
        84_600,
        cards::GIANT_GROWTH,
        PlayerId::Two,
    ));
    let action = transform(&game, curtains).expect("three mana pays for it");
    game.apply(PlayerId::One, action).expect("it activates");

    settle(&mut game, None);

    assert_eq!(
        game.players[PlayerId::Two.index()]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::LIGHTNING_BOLT],
        "no discard means no draw",
    );
    assert_eq!(game.players[PlayerId::Two.index()].library.len(), 1);
}

/// Lands are not choosable, so a hand of nothing but lands survives whole.
#[test]
fn a_hand_of_lands_gives_up_nothing() {
    let (mut game, curtains) = staged(&[cards::FOREST, cards::ISLAND]);
    game.players[PlayerId::Two.index()].library.clear();
    game.players[PlayerId::Two.index()].library.push(card(
        84_700,
        cards::GIANT_GROWTH,
        PlayerId::Two,
    ));
    let action = transform(&game, curtains).expect("three mana pays for it");
    game.apply(PlayerId::One, action).expect("it activates");

    settle(&mut game, Some(cards::FOREST));

    assert_eq!(
        game.players[PlayerId::Two.index()].hand.len(),
        2,
        "a land is not a nonland card",
    );
    assert!(game.players[PlayerId::Two.index()].graveyard.is_empty());
}
