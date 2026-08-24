//! Robber of the Rich: a hasty archer that takes a card off whoever is
//! holding more, and hands it back on the turns your Rogues attack.

use super::*;

/// The Robber attacking, with `their_hand` cards in the opponent's hand and
/// `your_hand` in yours.
fn staged(their_hand: usize, your_hand: usize) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[1].hand.clear();
    game.players[1].library.clear();
    for index in 0..3 {
        game.players[1]
            .library
            .push(card(108_000 + index, cards::SERRA_ANGEL, PlayerId::Two));
    }
    for index in 0..their_hand {
        game.players[1].hand.push(card(
            108_100 + u32::try_from(index).expect("few cards"),
            cards::ISLAND,
            PlayerId::Two,
        ));
    }
    for index in 0..your_hand {
        game.players[0].hand.push(card(
            108_200 + u32::try_from(index).expect("few cards"),
            cards::MOUNTAIN,
            PlayerId::One,
        ));
    }
    let robber = game
        .put_onto_battlefield(PlayerId::One, cards::ROBBER_OF_THE_RICH)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    (game, robber)
}

fn attack(game: &mut Game, robber: GameObjectId) {
    game.step = Step::DeclareAttackers;
    game.declare_attacker(robber, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    drain_pending(game);
}

/// Reach and haste, and it steals when they are holding more.
#[test]
fn attacking_steals_from_the_richer_hand() {
    let (mut game, robber) = staged(3, 0);
    let archer = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == robber)
        .expect("it is there");
    assert!(game.permanent_has_executable_keyword(archer, KeywordAbility::Reach));
    assert!(game.permanent_has_executable_keyword(archer, KeywordAbility::Haste));

    attack(&mut game, robber);

    assert_eq!(
        game.players[1]
            .exile
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::SERRA_ANGEL],
        "the top card of their library is exiled",
    );
    assert_eq!(game.players[1].library.len(), 2);
}

/// With hands level, the intervening-if stops it.
#[test]
fn it_steals_nothing_from_a_smaller_hand() {
    let (mut game, robber) = staged(1, 1);

    attack(&mut game, robber);

    assert!(game.players[1].exile.is_empty());
}

/// The stolen card is castable on a turn a Rogue attacked, and its mana may
/// be of any colour.
#[test]
fn the_stolen_card_is_castable_while_a_rogue_attacked() {
    let (mut game, robber) = staged(3, 0);
    attack(&mut game, robber);
    let stolen = game.players[1].exile[0].id;

    // A white Angel, paid for entirely in red.
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 5);
    game.step = Step::PostcombatMain;
    game.priority = PlayerId::One;
    assert!(
        game.legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if *card == stolen)),
        "the Robber is a Rogue and it attacked this turn",
    );

    // A later turn on which nothing attacked: the permission asks again.
    game.turns_started = [6, 6];
    for permanent in &mut game.battlefield {
        permanent.attacked_this_turn = false;
    }
    game.step = Step::PrecombatMain;
    assert!(
        !game
            .legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if *card == stolen)),
        "no Rogue attacked this turn",
    );
}

/// And it really is castable off the wrong colours.
#[test]
fn it_may_be_paid_for_in_any_color() {
    let (mut game, robber) = staged(3, 0);
    attack(&mut game, robber);
    let stolen = game.players[1].exile[0].id;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 5);
    game.step = Step::PostcombatMain;
    game.priority = PlayerId::One;

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == stolen))
        .expect("five red mana pays for a white Angel");
    game.apply(PlayerId::One, cast).expect("it is cast");
    drain_pending(&mut game);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::SERRA_ANGEL
                && permanent.controller == PlayerId::One),
        "and it arrives under your control",
    );
}
