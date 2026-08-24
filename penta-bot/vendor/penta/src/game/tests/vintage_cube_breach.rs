//! Through the Breach: a creature put onto the battlefield for one turn.

use super::*;

/// Answers every pending decision with the last option it offered, then
/// resolves whatever is left on the stack.
fn settle(game: &mut Game) {
    for _ in 0..16 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .last()
                .map(|option| vec![option.id])
                .unwrap_or_default();
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("the decision accepts what it offered");
            continue;
        }
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
}

/// Casts the Breach from hand with the mana already available.
fn cast_breach(game: &mut Game, breach: CardInstanceId) {
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == breach))
        .expect("five mana casts it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(game);
    drain_pending(game);
}

fn breach_with(hand: &[CardDefinitionId]) -> (Game, CardInstanceId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    for (offset, definition) in hand.iter().enumerate() {
        let id = 97_100 + u32::try_from(offset).expect("a short test hand");
        game.players[0]
            .hand
            .push(card(id, *definition, PlayerId::One));
    }
    let breach = card(97_000, cards::THROUGH_THE_BREACH, PlayerId::One);
    let breach_id = breach.id;
    game.players[0].hand.push(breach);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 4);
    (game, breach_id)
}

/// The whole point: a creature that never had to be cast, on the battlefield
/// and able to attack the turn it arrives.
#[test]
fn the_breach_puts_a_creature_down_hasty_without_casting_it() {
    let (mut game, breach_id) = breach_with(&[cards::SERRA_ANGEL]);

    cast_breach(&mut game, breach_id);

    let angel = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::SERRA_ANGEL)
        .expect("the creature is on the battlefield");
    assert!(
        game.permanent_has_executable_keyword(angel, KeywordAbility::Haste),
        "carrying haste, which is what makes the turn worth anything",
    );
    assert!(
        game.stack.is_empty(),
        "it was put onto the battlefield rather than cast",
    );
    assert!(
        game.players[0]
            .hand
            .iter()
            .all(|card| card.definition != cards::SERRA_ANGEL),
        "and it left the hand",
    );
}

/// The rent comes due at the next end step, and the creature is sacrificed
/// by a clause it carries rather than by anything that still names it.
#[test]
fn the_creature_sacrifices_itself_at_the_next_end_step() {
    let (mut game, breach_id) = breach_with(&[cards::SERRA_ANGEL]);
    cast_breach(&mut game, breach_id);
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::SERRA_ANGEL),
        "it is there to begin with",
    );

    game.capture_battlefield_triggers(&CommittedTriggerEvent::StepBegins {
        step: TurnStepDef::End,
        player: PlayerId::One,
    });
    game.finish_rules_procedure();
    settle(&mut game);
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.definition != cards::SERRA_ANGEL),
        "and gone by the end step",
    );
    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::SERRA_ANGEL),
        "sacrificed rather than exiled",
    );
}

/// A hand with nothing to put down is not offered a choice at all, and the
/// spell still resolves.
#[test]
fn a_hand_without_a_creature_is_never_asked() {
    let (mut game, breach_id) = breach_with(&[cards::LIGHTNING_BOLT]);

    cast_breach(&mut game, breach_id);

    assert!(game.pending_decisions.is_empty(), "nothing to choose from");
    assert!(game.battlefield.is_empty(), "and nothing arrived");
    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::THROUGH_THE_BREACH),
        "the spell still resolved and was spent",
    );
}

/// The offer names creature cards, not the whole hand.
#[test]
fn only_creature_cards_are_offered() {
    let (mut game, breach_id) = breach_with(&[cards::LIGHTNING_BOLT, cards::SERRA_ANGEL]);
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == breach_id))
        .expect("five mana casts it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    pass_until_decision(&mut game);

    let decision = game.pending_decisions.first().expect("a choice is offered");
    let offered = decision
        .observation
        .options
        .iter()
        .map(|option| option.label.as_str())
        .collect::<Vec<_>>();
    assert_eq!(offered, vec!["Serra Angel"]);
}
