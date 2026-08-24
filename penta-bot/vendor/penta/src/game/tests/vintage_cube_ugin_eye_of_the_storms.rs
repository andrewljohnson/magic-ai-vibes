//! Ugin, Eye of the Storms: seven mana that answers something as it is
//! cast, again for every colorless spell after it, and pays for the next
//! one himself.

use super::*;

fn staged() -> Game {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game
}

/// Answers every pending decision, naming `wanted` where it is offered and
/// otherwise taking the smallest legal answer.
fn settle_naming(game: &mut Game, wanted: Option<GameObjectId>) {
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let options = match wanted {
                Some(wanted) => decision
                    .options
                    .iter()
                    .filter(|option| option.card.is_some_and(|(object, _)| object == wanted))
                    .map(|option| option.id)
                    .take(1)
                    .collect(),
                None => decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum)
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
    game.check_state_based_actions();
}

fn ugin(game: &Game) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::UGIN_EYE_OF_THE_STORMS)
        .expect("he is on the battlefield")
}

fn loyalty_action(game: &Game, source: GameObjectId, ability: u8) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source: activated,
                ability: AbilityOrigin::Printed { ability: id, .. },
                ..
            } => *activated == source && *id == AbilityId(ability),
            _ => false,
        })
}

fn on_battlefield(game: &Game, definition: CardDefinitionId) -> bool {
    game.battlefield
        .iter()
        .any(|permanent| permanent.card.definition == definition)
}

/// Casting him exiles a colored permanent before he even arrives.
#[test]
fn casting_him_exiles_a_colored_permanent() {
    let mut game = staged();
    let bears = creature(150_000, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    let ugin = card(150_001, cards::UGIN_EYE_OF_THE_STORMS, PlayerId::One);
    let ugin_id = ugin.id;
    game.players[0].hand.push(ugin);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 7);

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == ugin_id))
        .expect("seven mana pays for him");
    game.apply(PlayerId::One, cast).expect("he is cast");
    settle_naming(&mut game, Some(bears_id));

    assert!(
        !on_battlefield(&game, cards::GRIZZLY_BEARS),
        "the cast trigger answered it on the way in",
    );
    assert!(on_battlefield(&game, cards::UGIN_EYE_OF_THE_STORMS));
}

/// A colorless spell cast afterwards does it again; a colored one does not,
/// and a colorless permanent is never a legal target.
#[test]
fn every_colorless_spell_answers_something() {
    let mut game = staged();
    game.put_onto_battlefield(PlayerId::One, cards::UGIN_EYE_OF_THE_STORMS)
        .expect("cataloged");
    drain_pending(&mut game);
    let bears = creature(150_100, cards::GRIZZLY_BEARS, PlayerId::Two);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    let ornithopter = card(150_101, cards::ORNITHOPTER, PlayerId::One);
    let ornithopter_id = ornithopter.id;
    game.players[0].hand.push(ornithopter);

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == ornithopter_id))
        .expect("a free artifact is castable");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle_naming(&mut game, Some(bears_id));

    assert!(
        !on_battlefield(&game, cards::GRIZZLY_BEARS),
        "the colorless spell triggered him",
    );
    assert!(on_battlefield(&game, cards::ORNITHOPTER));
}

/// The plus gains three and draws.
#[test]
fn the_plus_gains_three_and_draws() {
    let mut game = staged();
    let source = game
        .put_onto_battlefield(PlayerId::One, cards::UGIN_EYE_OF_THE_STORMS)
        .expect("cataloged");
    drain_pending(&mut game);
    let card = game
        .build_zone(PlayerId::One, &[cards::MOUNTAIN])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    game.players[0].library.push(card);
    game.players[0].life = 20;

    let plus = loyalty_action(&game, source, 2).expect("the plus is activatable");
    game.apply(PlayerId::One, plus).expect("it activates");
    settle_naming(&mut game, None);

    assert_eq!(game.players[0].life, 23);
    assert_eq!(game.players[0].hand.len(), 1);
    assert_eq!(ugin(&game).counters(CounterKind::Loyalty), 9);
}

/// The zero is a mana ability: it never uses the stack, and it is still the
/// one loyalty ability he may use this turn.
#[test]
fn the_zero_makes_three_colorless_without_the_stack() {
    let mut game = staged();
    let source = game
        .put_onto_battlefield(PlayerId::One, cards::UGIN_EYE_OF_THE_STORMS)
        .expect("cataloged");
    drain_pending(&mut game);

    let mana = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(
                action,
                Action::ActivateManaAbility { source: activated, .. } if *activated == source
            )
        })
        .expect("the mana ability is activatable");
    game.apply(PlayerId::One, mana).expect("it activates");

    assert!(
        game.stack.is_empty(),
        "a mana ability does not use the stack"
    );
    assert_eq!(game.players[0].mana.len(), 3);
    assert_eq!(
        ugin(&game).counters(CounterKind::Loyalty),
        7,
        "zero loyalty"
    );
    assert!(
        loyalty_action(&game, source, 2).is_none(),
        "and it was his one loyalty ability for the turn",
    );
}

/// The ultimate empties the library of colorless nonland cards into exile
/// and lets them be cast for free for the rest of the turn.
#[test]
fn the_ultimate_exiles_the_library_and_frees_it() {
    let mut game = staged();
    let source = game
        .put_onto_battlefield(PlayerId::One, cards::UGIN_EYE_OF_THE_STORMS)
        .expect("cataloged");
    if let Some(permanent) = game
        .battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == source)
    {
        permanent.set_counters(CounterKind::Loyalty, 11);
    }
    drain_pending(&mut game);
    let mut thopter = None;
    for definition in [cards::ORNITHOPTER, cards::MOUNTAIN, cards::GRIZZLY_BEARS] {
        let card = game
            .build_zone(PlayerId::One, &[definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        if definition == cards::ORNITHOPTER {
            thopter = Some(card.id);
        }
        game.players[0].library.push(card);
    }

    let ultimate = loyalty_action(&game, source, 4).expect("eleven loyalty pays for it");
    game.apply(PlayerId::One, ultimate).expect("it activates");
    settle_naming(&mut game, thopter);

    assert_eq!(
        game.players[0]
            .exile
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::ORNITHOPTER],
        "only the colorless nonland card was a legal choice",
    );
    let exiled = game.players[0].exile[0].id;
    assert!(
        game.legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if *card == exiled)),
        "and it is castable from exile with no mana at all",
    );
}
