//! Laelia, the Blade Reforged: a three-mana haste creature whose two
//! triggers feed each other.

use super::*;

/// Player One with Laelia out since last turn and `library` stacked so the
/// last entry is on top.
fn staged(library: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].graveyard.clear();
    if !library.is_empty() {
        game.players[0].library.clear();
        for definition in library {
            let card = game
                .build_zone(PlayerId::One, &[*definition])
                .expect("cataloged")
                .into_iter()
                .next()
                .expect("one card");
            game.players[0].library.push(card);
        }
    }
    let laelia = game
        .put_onto_battlefield(PlayerId::One, cards::LAELIA_THE_BLADE_REFORGED)
        .expect("cataloged");
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [2, 1];
    drain_pending(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.priority = PlayerId::One;
    (game, laelia)
}

fn deciding(game: &Game) -> Option<PlayerId> {
    game.pending_decisions
        .first()
        .map(|pending| pending.observation.player)
}

fn settle(game: &mut Game) {
    for _ in 0..24 {
        if deciding(game).is_some() {
            return;
        }
        if game.stack.is_empty() && game.pending_triggers.is_empty() {
            break;
        }
        let player = game.priority;
        if game.apply(player, Action::PassPriority).is_err() {
            break;
        }
    }
    game.check_state_based_actions();
}

/// Passes and answers whatever is asked until nothing is.
fn resolve_all(game: &mut Game) {
    for _ in 0..16 {
        settle(game);
        let Some(seat) = deciding(game) else { break };
        let decision = game.observe(seat).decision.expect("just checked");
        let options = decision
            .options
            .iter()
            .take(decision.minimum)
            .map(|option| option.id)
            .collect();
        if game
            .apply(
                seat,
                Action::ChooseDecision {
                    decision: decision.id,
                    options,
                },
            )
            .is_err()
        {
            break;
        }
    }
}

fn attack_with(game: &mut Game, laelia: GameObjectId) {
    game.apply(
        PlayerId::One,
        Action::DeclareAttacker {
            attacker: laelia,
            defender: AttackDefender::Player(PlayerId::Two),
        },
    )
    .expect("she attacks");
    game.apply(PlayerId::One, Action::FinishDeclaringAttackers)
        .expect("the declaration finishes");
    resolve_all(game);
}

fn counters(game: &Game, laelia: GameObjectId) -> u16 {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == laelia)
        .expect("she is on the battlefield")
        .counters(CounterKind::PlusOnePlusOne)
}

/// Haste: she attacks the turn she lands.
#[test]
fn she_has_haste() {
    let mut game = ready_game();
    game.battlefield.clear();
    let laelia = game
        .put_onto_battlefield(PlayerId::One, cards::LAELIA_THE_BLADE_REFORGED)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [1, 1];
    game.active_player = PlayerId::One;
    game.step = Step::DeclareAttackers;
    game.attackers_declared = false;
    game.priority = PlayerId::One;

    assert!(
        game.permanent_has_executable_keyword(
            game.battlefield
                .iter()
                .find(|permanent| permanent.card.id == laelia)
                .expect("she is here"),
            KeywordAbility::Haste
        ),
        "the turn she arrived",
    );
}

/// Attacking exiles the top card, and the exile is what grows her.
#[test]
fn attacking_exiles_the_top_card_and_grows_her() {
    let (mut game, laelia) = staged(&[cards::MOUNTAIN, cards::LIGHTNING_BOLT]);
    let library = game.players[0].library.len();
    assert_eq!(counters(&game, laelia), 0, "nothing on her yet");

    attack_with(&mut game, laelia);

    assert_eq!(
        game.players[0].library.len(),
        library - 1,
        "one card off the top",
    );
    assert!(
        game.players[0]
            .exile
            .iter()
            .any(|card| card.definition == cards::LIGHTNING_BOLT),
        "and into exile",
    );
    assert_eq!(
        counters(&game, laelia),
        1,
        "which is one or more cards put into exile from your library",
    );
    assert_eq!(
        game.power(
            game.battlefield
                .iter()
                .find(|permanent| permanent.card.id == laelia)
                .expect("she is here")
        ),
        Some(3),
        "a 3/3 by the time damage is dealt"
    );
}

/// "One or more": a move that takes several cards is still one counter.
#[test]
fn several_cards_at_once_is_one_counter() {
    let (mut game, laelia) = staged(&[]);
    // A main phase rather than a declaration step: the trigger has to reach
    // the stack and resolve, which needs a window where priority passes.
    game.step = Step::PrecombatMain;
    game.players[0].graveyard.clear();
    for _ in 0..3 {
        let card = game
            .build_zone(PlayerId::One, &[cards::LIGHTNING_BOLT])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].graveyard.push(card);
    }
    let buried = game.players[0]
        .graveyard
        .iter()
        .map(|card| card.id)
        .collect::<Vec<_>>();

    game.capture_exile_for_test(&buried, ZoneKind::Graveyard);
    resolve_all(&mut game);

    assert_eq!(
        counters(&game, laelia),
        1,
        "three cards, one move, one counter",
    );
}

/// It is your own zones: a card leaving their graveyard is not one of them.
#[test]
fn their_cards_do_not_grow_her() {
    let (mut game, laelia) = staged(&[]);
    game.step = Step::PrecombatMain;
    let theirs = game
        .build_zone(PlayerId::Two, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let id = theirs.id;
    game.players[1].graveyard.push(theirs);

    game.capture_exile_for_test(&[id], ZoneKind::Graveyard);
    resolve_all(&mut game);

    assert_eq!(counters(&game, laelia), 0, "the clause names your zones");
}

/// A card exiled from a hand is neither a library nor a graveyard.
#[test]
fn a_card_exiled_from_hand_does_not_grow_her() {
    let (mut game, laelia) = staged(&[]);
    game.step = Step::PrecombatMain;
    let held = game
        .build_zone(PlayerId::One, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let id = held.id;
    game.players[0].hand.push(held);

    game.capture_exile_for_test(&[id], ZoneKind::Hand);
    resolve_all(&mut game);

    assert_eq!(
        counters(&game, laelia),
        0,
        "the clause names two zones and a hand is neither",
    );
}

/// The card she exiles is playable that turn.
#[test]
fn the_exiled_card_may_be_played_this_turn() {
    let (mut game, laelia) = staged(&[cards::MOUNTAIN, cards::LIGHTNING_BOLT]);
    attack_with(&mut game, laelia);
    let exiled = game.players[0]
        .exile
        .iter()
        .find(|card| card.definition == cards::LIGHTNING_BOLT)
        .expect("it is in exile")
        .id;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);

    assert!(
        game.legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if *card == exiled)),
        "you may play that card this turn",
    );
}

/// "You may play that card" is not "without paying its mana cost": Laelia
/// finds the card and you still pay for it.
#[test]
fn the_exiled_card_still_costs_its_mana() {
    let (mut game, laelia) = staged(&[cards::MOUNTAIN, cards::LIGHTNING_BOLT]);
    attack_with(&mut game, laelia);
    let exiled = game.players[0]
        .exile
        .iter()
        .find(|card| card.definition == cards::LIGHTNING_BOLT)
        .expect("it is in exile")
        .id;

    assert!(
        game.legal_actions(PlayerId::One)
            .iter()
            .all(|action| !matches!(action, Action::CastSpell { card, .. } if *card == exiled)),
        "with no red mana there is nothing to cast it with",
    );
}
