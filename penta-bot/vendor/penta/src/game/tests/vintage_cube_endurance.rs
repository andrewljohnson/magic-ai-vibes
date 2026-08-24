//! Endurance: a flash blocker that puts a graveyard back where it came from,
//! shuffled into the dark.

use super::*;

/// Endurance on the battlefield, with `graveyard` cards already in player
/// two's graveyard and `library` cards under their library.
fn staged(graveyard: &[CardDefinitionId], library: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[PlayerId::Two.index()].graveyard.clear();
    game.players[PlayerId::Two.index()].library.clear();
    for (index, definition) in library.iter().enumerate() {
        let instance = card(
            80_000 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::Two,
        );
        game.players[PlayerId::Two.index()].library.push(instance);
    }
    for (index, definition) in graveyard.iter().enumerate() {
        let instance = card(
            80_100 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::Two,
        );
        game.players[PlayerId::Two.index()].graveyard.push(instance);
    }
    let endurance = game
        .put_onto_battlefield(PlayerId::One, cards::ENDURANCE)
        .expect("cataloged");
    (game, endurance)
}

/// Answers the enter-the-battlefield trigger by naming `target`, or by
/// naming nobody when it is `None`, and lets everything else take its
/// default. The trigger offers "you" and "your opponent" rather than a
/// seat, so the wanted option is read off the label.
fn answer_trigger(game: &mut Game, target: Option<PlayerId>) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = match (decision.kind, target) {
                (DecisionKind::TriggerPlacement, None) => Vec::new(),
                (DecisionKind::TriggerPlacement, Some(player)) => {
                    let wanted = if player == decision.player {
                        "you"
                    } else {
                        "your opponent"
                    };
                    decision
                        .options
                        .iter()
                        .filter(|option| option.label == wanted)
                        .map(|option| option.id)
                        .take(1)
                        .collect()
                }
                _ => decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum.max(1).min(decision.maximum))
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

fn library_definitions(game: &Game, player: PlayerId) -> Vec<CardDefinitionId> {
    game.players[player.index()]
        .library
        .iter()
        .map(|card| card.definition)
        .collect()
}

/// It has reach.
#[test]
fn it_has_reach() {
    let (game, endurance) = staged(&[], &[]);
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == endurance)
        .expect("it is there");

    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Reach));
}

/// Flash, read the only way that matters: it is castable in the opponent's
/// turn, which is when a graveyard is worth answering.
#[test]
fn it_can_be_cast_on_their_turn() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[PlayerId::One.index()].hand.clear();
    let endurance = card(82_000, cards::ENDURANCE, PlayerId::One);
    let endurance_id = endurance.id;
    game.players[PlayerId::One.index()].hand.push(endurance);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 3);
    game.active_player = PlayerId::Two;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;

    assert!(
        game.legal_actions(PlayerId::One).iter().any(
            |action| matches!(action, Action::CastSpell { card, .. } if *card == endurance_id)
        ),
    );
}

/// The whole graveyard goes under the library, and the cards that were
/// already there keep their order above it.
#[test]
fn it_puts_a_graveyard_under_a_library() {
    let (mut game, _) = staged(
        &[
            cards::LIGHTNING_BOLT,
            cards::GRIZZLY_BEARS,
            cards::DARK_RITUAL,
        ],
        &[cards::SAVANNAH_LIONS, cards::GIANT_GROWTH],
    );

    answer_trigger(&mut game, Some(PlayerId::Two));

    assert!(
        game.players[PlayerId::Two.index()].graveyard.is_empty(),
        "all of it, not some of it",
    );
    let library = library_definitions(&game, PlayerId::Two);
    assert_eq!(library.len(), 5);
    assert_eq!(
        &library[3..],
        &[cards::SAVANNAH_LIONS, cards::GIANT_GROWTH],
        "the library it lands under is not disturbed",
    );
    let mut buried = library[..3].to_vec();
    buried.sort_unstable();
    let mut expected = vec![
        cards::LIGHTNING_BOLT,
        cards::GRIZZLY_BEARS,
        cards::DARK_RITUAL,
    ];
    expected.sort_unstable();
    assert_eq!(buried, expected, "the same three cards, in some order");
}

/// "Up to one target player" is satisfied by naming nobody.
#[test]
fn it_can_name_nobody() {
    let (mut game, _) = staged(&[cards::LIGHTNING_BOLT], &[cards::SAVANNAH_LIONS]);

    answer_trigger(&mut game, None);

    assert_eq!(
        game.players[PlayerId::Two.index()].graveyard.len(),
        1,
        "a graveyard nobody named is left alone",
    );
}

/// Evoked, it exiles a green card, buries the graveyard, and then goes to
/// the graveyard itself.
#[test]
fn evoking_it_still_buries_the_graveyard() {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[PlayerId::One.index()].hand.clear();
    game.players[PlayerId::Two.index()].graveyard.clear();
    game.players[PlayerId::Two.index()].graveyard.push(card(
        81_000,
        cards::LIGHTNING_BOLT,
        PlayerId::Two,
    ));

    let endurance = card(81_001, cards::ENDURANCE, PlayerId::One);
    let endurance_id = endurance.id;
    game.players[PlayerId::One.index()].hand.push(endurance);
    // A green card in hand is the whole cost.
    game.players[PlayerId::One.index()]
        .hand
        .push(card(81_002, cards::GIANT_GROWTH, PlayerId::One));

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(action, Action::CastSpell { card, choices, .. }
                if *card == endurance_id && choices.costs().alternative().is_some())
        })
        .expect("evoke is offered with a green card in hand and no mana");
    game.apply(PlayerId::One, cast).expect("it is cast");

    answer_trigger(&mut game, Some(PlayerId::Two));

    assert!(
        game.players[PlayerId::Two.index()].graveyard.is_empty(),
        "the trigger happens even though the body does not stay",
    );
    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.definition != cards::ENDURANCE),
        "evoke sacrifices it",
    );
}
