//! Delayed Blast Fireball: a one-sided sweeper that costs a turn of setup,
//! and the foretell that buys it.

use super::*;

/// Player One holding a Fireball, with `theirs` on the battlefield under
/// Player Two.
fn staged(theirs: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    for definition in theirs {
        game.put_onto_battlefield(PlayerId::Two, *definition)
            .expect("cataloged");
    }
    let card = game
        .build_zone(PlayerId::One, &[cards::DELAYED_BLAST_FIREBALL])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let fireball = card.id;
    game.players[0].hand.push(card);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [2, 1];
    drain_pending(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, fireball)
}

fn settle(game: &mut Game) {
    for _ in 0..24 {
        if !game.pending_decisions.is_empty() {
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

/// The foretell action for `card`, if it is on offer.
fn foretell_action(game: &Game, card: GameObjectId) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::Foretell { card: id } if *id == card))
}

/// Every way Player One could cast `card` right now.
fn casts(game: &Game, card: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::CastSpell { card: id, .. } if *id == card))
        .collect()
}

/// The Fireball as it now sits in exile, if it is there.
fn in_exile(game: &Game) -> Option<GameObjectId> {
    game.players[0]
        .exile
        .iter()
        .find(|card| card.definition == cards::DELAYED_BLAST_FIREBALL)
        .map(|card| card.id)
}

/// Hands the turn to Player One again, one turn later.
fn next_own_turn(game: &mut Game) {
    game.turns_started[PlayerId::One.index()] += 1;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
}

/// Cast from hand, it is two damage to them and to their creatures.
#[test]
fn cast_from_hand_it_deals_two() {
    let (mut game, fireball) = staged(&[cards::SERRA_ANGEL, cards::SAVANNAH_LIONS]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    let cast = casts(&game, fireball)
        .into_iter()
        .next()
        .expect("three mana casts it");
    game.apply(PlayerId::One, cast).expect("it casts");
    settle(&mut game);

    assert_eq!(game.players[1].life, 18, "two to the opponent");
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::SAVANNAH_LIONS),
        "the 2/1 died",
    );
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::SERRA_ANGEL),
        "and the 4/4 lived, which is what the foretold five is for",
    );
}

/// Your own creatures and your own life are untouched: it is one-sided.
#[test]
fn it_leaves_you_and_yours_alone() {
    let (mut game, fireball) = staged(&[]);
    game.put_onto_battlefield(PlayerId::One, cards::SAVANNAH_LIONS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    let cast = casts(&game, fireball)
        .into_iter()
        .next()
        .expect("it is castable");
    game.apply(PlayerId::One, cast).expect("it casts");
    settle(&mut game);

    assert_eq!(game.players[0].life, 20, "your life is your own");
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::SAVANNAH_LIONS),
        "and so is your 2/1",
    );
}

/// Foretelling costs {2} and puts the card in exile.
#[test]
fn foretelling_exiles_it_for_two() {
    let (mut game, fireball) = staged(&[]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);

    let action = foretell_action(&game, fireball).expect("two mana foretells it");
    game.apply(PlayerId::One, action).expect("it foretells");

    assert!(game.players[0].hand.is_empty(), "it left your hand");
    assert!(in_exile(&game).is_some(), "and is in exile");
    assert_eq!(
        game.players[0].mana_pool.total(),
        0,
        "with the two mana spent",
    );
}

/// It is a special action on your own turn only, and only with the mana.
#[test]
fn foretelling_is_your_own_main_phase_with_two_mana() {
    let (mut game, fireball) = staged(&[]);
    assert!(
        foretell_action(&game, fireball).is_none(),
        "not without the mana",
    );

    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);
    assert!(
        foretell_action(&game, fireball).is_some(),
        "your own main phase, two mana up",
    );

    game.active_player = PlayerId::Two;
    assert!(
        foretell_action(&game, fireball).is_none(),
        "and never on theirs",
    );
}

/// "Cast it on a later turn": not the turn it was foretold.
#[test]
fn a_foretold_card_waits_a_turn() {
    let (mut game, fireball) = staged(&[]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);
    let action = foretell_action(&game, fireball).expect("it is foretellable");
    game.apply(PlayerId::One, action).expect("it foretells");
    let exiled = in_exile(&game).expect("it is in exile");
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 4);

    assert!(
        casts(&game, exiled).is_empty(),
        "the turn it was exiled on is not a later turn",
    );

    next_own_turn(&mut game);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 4);
    assert!(!casts(&game, exiled).is_empty(), "and the next one is");
}

/// Cast from exile it deals five instead, which is the whole point of the
/// two mana spent a turn earlier.
#[test]
fn cast_from_exile_it_deals_five() {
    let (mut game, fireball) = staged(&[cards::SERRA_ANGEL]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);
    let action = foretell_action(&game, fireball).expect("it is foretellable");
    game.apply(PlayerId::One, action).expect("it foretells");
    let exiled = in_exile(&game).expect("it is in exile");
    next_own_turn(&mut game);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 4);

    let cast = casts(&game, exiled)
        .into_iter()
        .next()
        .expect("six mana casts it for its foretell cost");
    game.apply(PlayerId::One, cast).expect("it casts");
    settle(&mut game);

    assert_eq!(game.players[1].life, 15, "five to the opponent");
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::SERRA_ANGEL),
        "and five kills a 4/4",
    );
}

/// The foretell cost is what it costs from exile, not the printed one.
#[test]
fn the_foretold_cast_costs_the_foretell_price() {
    let (mut game, fireball) = staged(&[]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);
    let action = foretell_action(&game, fireball).expect("it is foretellable");
    game.apply(PlayerId::One, action).expect("it foretells");
    let exiled = in_exile(&game).expect("it is in exile");
    next_own_turn(&mut game);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 3);

    assert!(
        casts(&game, exiled).is_empty(),
        "five mana is one short of the foretell cost",
    );
}

/// A foretold card lies face down: they may count it, not read it.
#[test]
fn a_foretold_card_is_hidden_from_them() {
    let (mut game, fireball) = staged(&[]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);
    let action = foretell_action(&game, fireball).expect("it is foretellable");
    game.apply(PlayerId::One, action).expect("it foretells");

    let theirs = game.observe(PlayerId::Two);
    assert!(
        theirs.exiles[PlayerId::One.index()].is_empty(),
        "they cannot see what it is",
    );
    assert_eq!(
        theirs.face_down_exile_sizes[PlayerId::One.index()],
        1,
        "but they can count it",
    );

    let yours = game.observe(PlayerId::One);
    assert_eq!(
        yours.exiles[PlayerId::One.index()].len(),
        1,
        "and you know what you exiled",
    );
}
