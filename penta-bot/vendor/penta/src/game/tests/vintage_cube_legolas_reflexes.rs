//! Legolas's Quick Reflexes: an untap nobody can answer, and a blocker that
//! shoots whatever taps it afterwards.

use super::*;

/// The spell in hand with a bear of player one's on the battlefield.
fn staged() -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[PlayerId::One.index()].hand.clear();
    let bears = creature(93_000, cards::GRIZZLY_BEARS, PlayerId::One);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    let reflexes = card(93_001, cards::LEGOLASS_QUICK_REFLEXES, PlayerId::One);
    let reflexes_id = reflexes.id;
    game.players[PlayerId::One.index()].hand.push(reflexes);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 1);
    game.turns_started[PlayerId::One.index()] = 5;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, reflexes_id, bears_id)
}

fn cast_at(game: &mut Game, spell: GameObjectId, target: GameObjectId) {
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == spell
                    && choices
                        .targets()
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|chosen| *chosen == Target::Permanent(target))
            }
            _ => false,
        })
        .expect("one green mana buys it");
    game.apply(PlayerId::One, cast).expect("it is cast");
}

fn resolve(game: &mut Game) {
    for _ in 0..12 {
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
}

fn permanent(game: &Game, id: GameObjectId) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("it is on the battlefield")
}

/// It untaps and hands out both keywords.
#[test]
fn it_untaps_and_grants_reach_and_hexproof() {
    let (mut game, reflexes, bears) = staged();
    game.battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == bears)
        .expect("it is there")
        .tapped = true;

    cast_at(&mut game, reflexes, bears);
    resolve(&mut game);

    let creature = permanent(&game, bears);
    assert!(!creature.tapped, "it is untapped");
    assert!(game.permanent_has_executable_keyword(creature, KeywordAbility::Reach));
    assert!(game.permanent_has_executable_keyword(creature, KeywordAbility::Hexproof));
}

/// Tapping it afterwards shoots something for its power.
#[test]
fn tapping_it_afterwards_shoots_for_its_power() {
    let (mut game, reflexes, bears) = staged();
    let target = creature(93_100, cards::SERRA_ANGEL, PlayerId::Two);
    let target_id = target.card.id;
    game.battlefield.push(target);

    cast_at(&mut game, reflexes, bears);
    resolve(&mut game);

    game.tap_permanent(bears);
    // The shot picks its own target; point it at the Angel rather than at
    // the bear the trigger also offers.
    for _ in 0..12 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = decision
                .options
                .iter()
                .filter(|option| option.card.is_some_and(|(object, _)| object == target_id))
                .map(|option| option.id)
                .take(1)
                .collect();
            game.apply(
                decision.player,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .expect("the Angel is one of the offered targets");
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
        permanent(&game, target_id).damage,
        2,
        "a 2/2 shoots for two",
    );
}

/// Split second: while it is on the stack nobody may cast anything.
#[test]
fn nothing_may_be_cast_while_it_is_on_the_stack() {
    let (mut game, reflexes, bears) = staged();
    game.players[PlayerId::One.index()].hand.push(card(
        93_200,
        cards::LIGHTNING_BOLT,
        PlayerId::One,
    ));
    game.players[PlayerId::Two.index()].hand.clear();
    game.players[PlayerId::Two.index()].hand.push(card(
        93_201,
        cards::LIGHTNING_BOLT,
        PlayerId::Two,
    ));
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::Red, 1);
    assert!(
        game.legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { .. })),
        "a Bolt is castable before the Reflexes goes on the stack",
    );

    cast_at(&mut game, reflexes, bears);

    assert!(
        !game
            .legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { .. })),
        "not even its own controller may answer it",
    );
    assert!(
        !game
            .legal_actions(PlayerId::Two)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { .. })),
    );

    resolve(&mut game);
    assert!(
        game.legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { .. })),
        "once it has resolved the window reopens",
    );
}

/// Mana abilities are exempt, which is what makes split second a restriction
/// on answers rather than on paying for them.
#[test]
fn mana_abilities_still_work_under_it() {
    let (mut game, reflexes, bears) = staged();
    let mountain = creature(93_300, cards::MOUNTAIN, PlayerId::One);
    let mountain_id = mountain.card.id;
    game.battlefield.push(mountain);

    cast_at(&mut game, reflexes, bears);

    assert!(
        game.legal_actions(PlayerId::One).iter().any(
            |action| matches!(action, Action::ActivateManaAbility { source, .. }
                if *source == mountain_id)
        ),
        "a land still taps for mana",
    );
}
