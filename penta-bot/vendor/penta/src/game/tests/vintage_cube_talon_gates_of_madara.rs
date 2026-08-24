//! Talon Gates of Madara: a land that answers a creature on the way in, and
//! four mana that puts it there without a land drop.

use super::*;

/// The Gates in hand, with `theirs` on the battlefield under player Two.
fn staged(theirs: &[CardDefinitionId]) -> (Game, GameObjectId, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let mut ids = Vec::new();
    for definition in theirs {
        ids.push(
            game.put_onto_battlefield(PlayerId::Two, *definition)
                .expect("cataloged"),
        );
    }
    drain_pending(&mut game);
    let card = game
        .build_zone(PlayerId::One, &[cards::TALON_GATES_OF_MADARA])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let card_id = card.id;
    game.players[0].hand.push(card);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.players[0].lands_played_this_turn = 0;
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, card_id, ids)
}

/// Answers whatever the enter trigger asks, naming `wanted` when it can.
fn settle_naming(game: &mut Game, wanted: Option<GameObjectId>) {
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options: Vec<_> = decision
                .options
                .iter()
                .filter(|option| {
                    wanted.is_some_and(|wanted| {
                        option.card.is_some_and(|(object, _)| object == wanted)
                    })
                })
                .map(|option| option.id)
                .take(1)
                .collect();
            let options = if options.len() < decision.minimum {
                decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum)
                    .collect()
            } else {
                options
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
    game.check_state_based_actions();
}

fn the_gates(game: &Game) -> Option<&Permanent> {
    game.battlefield.iter().find(|permanent| {
        permanent.card.definition == ObjectKind::Card(cards::TALON_GATES_OF_MADARA)
    })
}

/// Played as a land it phases a creature out.
#[test]
fn it_phases_a_creature_out_as_it_enters() {
    let (mut game, card, theirs) = staged(&[cards::SERRA_ANGEL]);
    let angel = theirs[0];

    let play = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::PlayLand { card: played, .. } if *played == card))
        .expect("a land drop is available");
    game.apply(PlayerId::One, play).expect("it is played");
    settle_naming(&mut game, Some(angel));

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == angel),
        "a phased-out permanent is off the battlefield",
    );
    assert!(
        game.phased_out
            .iter()
            .any(|permanent| permanent.card.id == angel),
        "it phased out rather than leaving the game",
    );
}

/// "Up to one": it arrives whether or not anything is named.
#[test]
fn it_may_name_nothing() {
    let (mut game, card, _) = staged(&[]);

    let play = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::PlayLand { card: played, .. } if *played == card))
        .expect("a land drop is available");
    game.apply(PlayerId::One, play).expect("it is played");
    settle_naming(&mut game, None);

    assert!(the_gates(&game).is_some());
}

/// Four mana puts it onto the battlefield straight out of hand, land drop
/// spent or not -- and the enter trigger fires the same way.
#[test]
fn four_mana_puts_it_in_from_hand() {
    let (mut game, card, theirs) = staged(&[cards::SERRA_ANGEL]);
    let angel = theirs[0];
    game.players[0].lands_played_this_turn = 1;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 4);

    let crash = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::ActivateAbility { source, .. } if *source == card))
        .expect("the four-mana ability is activatable from hand");
    game.apply(PlayerId::One, crash).expect("it activates");
    settle_naming(&mut game, Some(angel));

    assert!(the_gates(&game).is_some(), "the land arrived");
    assert!(
        game.phased_out
            .iter()
            .any(|permanent| permanent.card.id == angel),
        "and its trigger fired",
    );
    assert_eq!(
        game.players[0].lands_played_this_turn, 1,
        "no land drop was spent",
    );
}

/// Without the mana it is not on offer.
#[test]
fn it_needs_the_four_mana() {
    let (game, card, _) = staged(&[]);

    assert!(
        !game.legal_actions(PlayerId::One).into_iter().any(
            |action| matches!(action, Action::ActivateAbility { source, .. } if source == card)
        ),
        "four mana is four mana",
    );
}

/// It taps for colourless, and for any colour with a mana behind it.
#[test]
fn it_taps_for_colorless_or_for_anything() {
    let (mut game, card, _) = staged(&[]);
    let play = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::PlayLand { card: played, .. } if *played == card))
        .expect("a land drop is available");
    game.apply(PlayerId::One, play).expect("it is played");
    settle_naming(&mut game, None);
    let gates = the_gates(&game).expect("it is on the battlefield").card.id;

    let colorless = game.legal_actions(PlayerId::One).into_iter().any(|action| {
        matches!(
            action,
            Action::ActivateManaAbility { source, color: ManaColor::Colorless, .. }
                if source == gates
        )
    });
    assert!(colorless, "the free half makes {{C}}");

    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);
    let colors = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter_map(|action| match action {
            Action::ActivateManaAbility { source, color, .. } if source == gates => Some(color),
            _ => None,
        })
        .collect::<Vec<_>>();
    for color in ManaColor::COLORS {
        assert!(colors.contains(&color), "{color:?} is on offer for {{1}}");
    }
}

/// Phasing out is not removal: it comes back at its controller's next untap
/// step, which is the whole difference between this and exile.
#[test]
fn what_phased_out_comes_back() {
    let (mut game, card, theirs) = staged(&[cards::SERRA_ANGEL]);
    let angel = theirs[0];
    let play = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::PlayLand { card: played, .. } if *played == card))
        .expect("a land drop is available");
    game.apply(PlayerId::One, play).expect("it is played");
    settle_naming(&mut game, Some(angel));
    assert!(!game.phased_out.is_empty(), "it is away for now");

    for _ in 0..40 {
        // Untap is not a step objects wait in, so the first thing seen on
        // the other player's turn is their upkeep -- by which point the
        // phase-in has already happened.
        if game.turn > 9 && game.active_player == PlayerId::Two && game.step == Step::Upkeep {
            break;
        }
        game.advance_step();
        settle_naming(&mut game, None);
    }

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == angel),
        "back on its controller's untap step",
    );
    assert!(game.phased_out.is_empty());
}
