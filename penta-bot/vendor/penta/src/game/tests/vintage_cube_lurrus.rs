//! Lurrus of the Dream-Den: one cheap permanent back out of the graveyard
//! every turn, and only on your own.

use super::*;

/// Lurrus out since last turn, with `graveyard` in Player One's graveyard.
fn staged(graveyard: &[CardDefinitionId]) -> Game {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].graveyard.clear();
    for (index, definition) in graveyard.iter().enumerate() {
        game.players[0].graveyard.push(card(
            230_000 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::One,
        ));
    }
    game.put_onto_battlefield(PlayerId::One, cards::LURRUS_OF_THE_DREAM_DEN)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game
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

fn castable_from_graveyard(game: &Game, definition: CardDefinitionId) -> Option<Action> {
    let card = game.players[0]
        .graveyard
        .iter()
        .find(|card| card.definition == definition)?
        .id;
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card: id, .. } if *id == card))
}

/// He is lifelinking, which is half of why the deck plays him.
#[test]
fn he_has_lifelink() {
    let game = staged(&[]);
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::LURRUS_OF_THE_DREAM_DEN)
        .expect("he is there");

    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Lifelink));
}

/// A cheap permanent in the graveyard is castable; an expensive one and a
/// noncreature spell are not.
#[test]
fn only_cheap_permanents_are_castable() {
    let mut game = staged(&[
        cards::GRIZZLY_BEARS,
        cards::SERRA_ANGEL,
        cards::LIGHTNING_BOLT,
    ]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 6);

    assert!(
        castable_from_graveyard(&game, cards::GRIZZLY_BEARS).is_some(),
        "a two-mana creature is what he is for",
    );
    assert!(
        castable_from_graveyard(&game, cards::SERRA_ANGEL).is_none(),
        "a five-mana one is not",
    );
    assert!(
        castable_from_graveyard(&game, cards::LIGHTNING_BOLT).is_none(),
        "and an instant is not a permanent spell",
    );
}

/// Once each turn: the second cheap permanent has to wait.
#[test]
fn the_permission_is_spent_by_the_first_cast() {
    let mut game = staged(&[cards::GRIZZLY_BEARS, cards::MANIFOLD_KEY]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 6);

    let cast = castable_from_graveyard(&game, cards::GRIZZLY_BEARS).expect("the first is on offer");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::GRIZZLY_BEARS),
        "the first one resolved",
    );
    assert!(
        castable_from_graveyard(&game, cards::MANIFOLD_KEY).is_none(),
        "and the permission is spent for the turn",
    );

    game.complete_cleanup();
    game.commit_next_turn(PlayerId::Two, Vec::new());
    game.complete_cleanup();
    game.commit_next_turn(PlayerId::One, Vec::new());
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 6);

    assert!(
        castable_from_graveyard(&game, cards::MANIFOLD_KEY).is_some(),
        "a fresh turn opens it again",
    );
}

/// "During each of your turns": not on theirs, even at instant speed.
#[test]
fn it_is_closed_on_their_turn() {
    let mut game = staged(&[cards::MANIFOLD_KEY]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 6);
    game.active_player = PlayerId::Two;
    game.step = Step::End;
    game.priority = PlayerId::One;

    assert!(castable_from_graveyard(&game, cards::MANIFOLD_KEY).is_none());
}

/// When he leaves, the permission goes with him.
#[test]
fn the_permission_belongs_to_him() {
    let mut game = staged(&[cards::GRIZZLY_BEARS]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 6);
    let lurrus = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::LURRUS_OF_THE_DREAM_DEN)
        .expect("he is there")
        .card
        .id;

    game.destroy_permanent(lurrus);
    settle(&mut game);

    assert!(castable_from_graveyard(&game, cards::GRIZZLY_BEARS).is_none());
}
