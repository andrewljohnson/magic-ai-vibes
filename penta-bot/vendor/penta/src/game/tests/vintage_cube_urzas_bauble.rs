//! Urza's Bauble: a free artifact that replaces itself a turn later.

use super::*;

/// The Bauble on the battlefield, with a card to draw and a hand to peek at.
fn staged(their_hand: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[1].hand.clear();
    game.players[0].library.clear();
    let drawn = game
        .build_zone(PlayerId::One, &[cards::SERRA_ANGEL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[0].library.push(drawn);
    for (index, definition) in their_hand.iter().enumerate() {
        game.players[1].hand.push(card(
            310_000 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::Two,
        ));
    }
    let bauble = game
        .put_onto_battlefield(PlayerId::One, cards::URZAS_BAUBLE)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, bauble)
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

fn crack_it(game: &mut Game, bauble: GameObjectId, at: PlayerId) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == bauble
                    && targets
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|target| *target == Target::Player(at))
            }
            _ => false,
        })
        .expect("either player is a legal target");
    game.apply(PlayerId::One, action).expect("it activates");
    settle(game);
}

/// It costs nothing, sacrifices itself, and shows you exactly one card of
/// their hand.
#[test]
fn it_shows_one_card_of_their_hand() {
    let (mut game, bauble) = staged(&[
        cards::LIGHTNING_BOLT,
        cards::GRIZZLY_BEARS,
        cards::SERRA_ANGEL,
    ]);

    crack_it(&mut game, bauble, PlayerId::Two);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == bauble),
        "it sacrificed itself",
    );
    let seen = game
        .observe(PlayerId::One)
        .last_seen_hand
        .expect("you looked at something");
    assert_eq!(seen.0, PlayerId::Two);
    assert_eq!(seen.1.len(), 1, "one card, not the hand");
    assert!(
        [
            cards::LIGHTNING_BOLT,
            cards::GRIZZLY_BEARS,
            cards::SERRA_ANGEL
        ]
        .contains(&seen.1[0].1),
        "and it is one of the cards they were holding",
    );
}

/// The draw waits for the next upkeep.
#[test]
fn the_card_comes_at_the_next_upkeep() {
    let (mut game, bauble) = staged(&[cards::LIGHTNING_BOLT]);

    crack_it(&mut game, bauble, PlayerId::Two);
    assert!(
        game.players[0].hand.is_empty(),
        "nothing is drawn on the spot",
    );

    game.commit_next_turn(PlayerId::Two, Vec::new());
    game.step = Step::Upkeep;
    game.priority = PlayerId::Two;
    game.handle_upkeep_triggers();
    settle(&mut game);

    assert_eq!(
        game.players[0]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::SERRA_ANGEL],
        "the next turn's upkeep is when it arrives",
    );
}

/// An empty hand shows nothing, and the draw still comes.
#[test]
fn an_empty_hand_still_pays_the_card() {
    let (mut game, bauble) = staged(&[]);

    crack_it(&mut game, bauble, PlayerId::Two);

    assert!(game.observe(PlayerId::One).last_seen_hand.is_none());

    game.commit_next_turn(PlayerId::Two, Vec::new());
    game.step = Step::Upkeep;
    game.priority = PlayerId::Two;
    game.handle_upkeep_triggers();
    settle(&mut game);

    assert_eq!(game.players[0].hand.len(), 1);
}
