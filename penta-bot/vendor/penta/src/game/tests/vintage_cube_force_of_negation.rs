//! Force of Negation: free interaction that answers only the half of a
//! format worth answering for free, and only on somebody else's turn.

use super::*;

/// Player Two casting `spell`, with Player One holding a Force and `hand`
/// besides. It is Player Two's turn unless a test says otherwise.
fn staged(hand: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[1].hand.clear();
    let card = game
        .build_zone(PlayerId::One, &[cards::FORCE_OF_NEGATION])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let force = card.id;
    game.players[0].hand.push(card);
    for definition in hand {
        let card = game
            .build_zone(PlayerId::One, &[*definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].hand.push(card);
    }
    game.turns_started = [1, 1];
    game.active_player = PlayerId::Two;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::Two;
    (game, force)
}

fn settle(game: &mut Game) {
    for _ in 0..24 {
        if game.observe(PlayerId::One).decision.is_some()
            || game.observe(PlayerId::Two).decision.is_some()
        {
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

/// Puts `definition` on the stack for Player Two and hands priority back.
fn they_cast(game: &mut Game, definition: CardDefinitionId) -> GameObjectId {
    let card = game
        .build_zone(PlayerId::Two, &[definition])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let id = card.id;
    game.players[1].hand.push(card);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::Red, 2);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::White, 2);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::Colorless, 4);
    let action = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == id))
        .expect("they can cast it");
    game.apply(PlayerId::Two, action).expect("it casts");
    game.priority = PlayerId::One;
    // Casting mints a new object: the spell on the stack is not the card
    // that was in hand, and a counterspell points at the former.
    game.stack.last().expect("it is on the stack").id
}

/// Every way Player One could cast the Force at the spell on the stack.
fn answers(game: &Game, force: GameObjectId, spell: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == force
                    && choices
                        .targets()
                        .iter()
                        .any(|selection| selection.targets().contains(&Target::Spell(spell)))
            }
            _ => false,
        })
        .collect()
}

/// The free half: a blue card leaves the hand, the spell is countered, and
/// no mana is spent.
#[test]
fn exiling_a_blue_card_pays_for_it_on_their_turn() {
    let (mut game, force) = staged(&[cards::BRAINSTORM]);
    let bolt = they_cast(&mut game, cards::LIGHTNING_BOLT);

    let offers = answers(&game, force, bolt);
    assert_eq!(offers.len(), 1, "only the free half is affordable");
    game.apply(PlayerId::One, offers[0].clone())
        .expect("the blue card pays for it");
    settle(&mut game);

    assert_eq!(game.players[0].life, 20, "the Bolt never resolved");
    assert_eq!(
        game.players[0].mana_pool.total(),
        0,
        "with no mana raised at all",
    );
    assert!(
        game.players[0]
            .exile
            .iter()
            .any(|card| card.definition == cards::BRAINSTORM),
        "and the Brainstorm was exiled to pay",
    );
}

/// The countered spell is exiled rather than buried, which is what the
/// second sentence buys.
#[test]
fn the_countered_spell_is_exiled_rather_than_buried() {
    let (mut game, force) = staged(&[cards::BRAINSTORM]);
    let bolt = they_cast(&mut game, cards::LIGHTNING_BOLT);

    let action = answers(&game, force, bolt)
        .into_iter()
        .next()
        .expect("it is castable");
    game.apply(PlayerId::One, action).expect("it casts");
    settle(&mut game);

    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::LIGHTNING_BOLT),
        "the Bolt is in exile",
    );
    assert!(
        !game.players[1]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::LIGHTNING_BOLT),
        "and never reached a graveyard",
    );
}

/// "Noncreature spell": a creature on the stack is not a legal target.
#[test]
fn a_creature_spell_is_not_a_legal_target() {
    let (mut game, force) = staged(&[cards::BRAINSTORM]);
    let angel = they_cast(&mut game, cards::SERRA_ANGEL);

    assert!(
        answers(&game, force, angel).is_empty(),
        "a Serra Angel is a creature spell",
    );
}

/// "If it's not your turn" gates only the free half. On your own turn the
/// Force is a three-mana counterspell again.
#[test]
fn the_free_half_is_not_offered_on_your_own_turn() {
    let (mut game, force) = staged(&[cards::BRAINSTORM]);
    let bolt = they_cast(&mut game, cards::LIGHTNING_BOLT);
    game.active_player = PlayerId::One;

    assert!(
        answers(&game, force, bolt).is_empty(),
        "a blue card buys nothing on your own turn",
    );

    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);
    assert_eq!(
        answers(&game, force, bolt).len(),
        1,
        "but three mana still casts it",
    );
}

/// A hand with no blue card cannot pay the alternative, however full it is.
#[test]
fn a_hand_with_no_blue_card_cannot_pay_it() {
    let (mut game, force) = staged(&[cards::MOUNTAIN, cards::SAVANNAH_LIONS]);
    let bolt = they_cast(&mut game, cards::LIGHTNING_BOLT);

    assert!(
        answers(&game, force, bolt).is_empty(),
        "a Mountain and a Savannah Lions are not blue cards",
    );
}

/// The printed cost stands beside the free one when both are payable.
#[test]
fn both_halves_are_offered_when_both_are_payable() {
    let (mut game, force) = staged(&[cards::BRAINSTORM]);
    let bolt = they_cast(&mut game, cards::LIGHTNING_BOLT);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    assert_eq!(
        answers(&game, force, bolt).len(),
        2,
        "the mana cost and the exile are two ways to pay one spell",
    );
}
