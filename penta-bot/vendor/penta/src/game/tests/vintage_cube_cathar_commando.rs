//! Cathar Commando: removal you hold up, and a 3/1 when nothing needed
//! killing.

use super::*;

/// Player One holding a Commando with two mana up, and `theirs` on the
/// battlefield under Player Two.
fn staged(theirs: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    for definition in theirs {
        game.put_onto_battlefield(PlayerId::Two, *definition)
            .expect("cataloged");
    }
    let card = game
        .build_zone(PlayerId::One, &[cards::CATHAR_COMMANDO])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let commando = card.id;
    game.players[0].hand.push(card);
    drain_pending(&mut game);
    game.turns_started = [1, 1];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);
    (game, commando)
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

fn castable(game: &Game, card: GameObjectId) -> bool {
    game.legal_actions(PlayerId::One)
        .iter()
        .any(|action| matches!(action, Action::CastSpell { card: id, .. } if *id == card))
}

/// Puts the Commando onto the battlefield the ordinary way and returns the
/// permanent's id, which the cast mints fresh.
fn resolve_it(game: &mut Game, commando: GameObjectId) -> GameObjectId {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == commando))
        .expect("two mana casts it");
    game.apply(PlayerId::One, action).expect("it casts");
    settle(game);
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::CATHAR_COMMANDO)
        .expect("it resolved")
        .card
        .id
}

/// Every way the Commando could shoot down `target`.
fn shots(game: &Game, commando: GameObjectId, target: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == commando
                    && targets
                        .iter()
                        .any(|selection| selection.targets().contains(&Target::Permanent(target)))
            }
            _ => false,
        })
        .collect()
}

fn on_battlefield(game: &Game, definition: CardDefinitionId) -> bool {
    game.battlefield
        .iter()
        .any(|permanent| permanent.card.definition == definition)
}

/// Flash is the whole card: it is castable with the opponent's spell on the
/// stack, when a creature without it would have to wait.
#[test]
fn flash_lets_it_be_held_up() {
    let (mut game, commando) = staged(&[]);
    game.active_player = PlayerId::Two;
    game.step = Step::Upkeep;
    game.priority = PlayerId::One;
    // Mana empties as the step changes, so the two are raised again here.
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 1);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    assert!(
        castable(&game, commando),
        "on their upkeep, which is not a moment a sorcery-speed creature gets",
    );
}

/// A 3/1 body when the removal was not needed.
#[test]
fn it_is_a_three_one_when_it_resolves() {
    let (mut game, commando) = staged(&[]);
    let body = resolve_it(&mut game, commando);

    let permanent = game
        .battlefield
        .iter()
        .find(|found| found.card.id == body)
        .expect("it is here");
    assert_eq!(game.power(permanent), Some(3));
    assert_eq!(game.toughness(permanent), Some(1));
}

/// One mana and the body itself destroys an artifact.
#[test]
fn one_mana_and_itself_destroys_an_artifact() {
    let (mut game, commando) = staged(&[cards::SOL_RING]);
    let body = resolve_it(&mut game, commando);
    let ring = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::SOL_RING)
        .expect("it is here")
        .card
        .id;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    let action = shots(&game, body, ring)
        .into_iter()
        .next()
        .expect("one mana activates it");
    game.apply(PlayerId::One, action).expect("it activates");
    settle(&mut game);

    assert!(
        !on_battlefield(&game, cards::SOL_RING),
        "the Sol Ring was destroyed",
    );
    assert!(
        !on_battlefield(&game, cards::CATHAR_COMMANDO),
        "and the Commando sacrificed itself to do it",
    );
    assert!(
        game.players[0]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::CATHAR_COMMANDO),
        "into its owner's graveyard",
    );
}

/// The mana is a real half of the cost: with nothing in the pool the ability
/// is not offered at all.
#[test]
fn the_sacrifice_alone_does_not_pay_for_it() {
    let (mut game, commando) = staged(&[cards::SOL_RING]);
    let body = resolve_it(&mut game, commando);
    let ring = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::SOL_RING)
        .expect("it is here")
        .card
        .id;

    assert_eq!(
        game.players[0].mana_pool.total(),
        0,
        "casting it spent everything staged",
    );
    assert!(
        shots(&game, body, ring).is_empty(),
        "the mana half of the cost has to come from somewhere",
    );
}

/// "Artifact or enchantment", and nothing else: a creature is not a legal
/// target however badly it needs shooting.
#[test]
fn a_creature_is_not_a_legal_target() {
    let (mut game, commando) = staged(&[cards::SERRA_ANGEL]);
    let body = resolve_it(&mut game, commando);
    let angel = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::SERRA_ANGEL)
        .expect("it is here")
        .card
        .id;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    assert!(
        shots(&game, body, angel).is_empty(),
        "a Serra Angel is neither an artifact nor an enchantment",
    );
}

/// It reaches an enchantment as readily as an artifact, and either side's.
#[test]
fn it_reaches_an_enchantment_too() {
    let (mut game, commando) = staged(&[cards::CIRCLE_OF_PROTECTION_BLUE]);
    let body = resolve_it(&mut game, commando);
    let circle = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.definition == cards::CIRCLE_OF_PROTECTION_BLUE)
        .expect("it is here")
        .card
        .id;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 1);

    let action = shots(&game, body, circle)
        .into_iter()
        .next()
        .expect("an enchantment is a legal target");
    game.apply(PlayerId::One, action).expect("it activates");
    settle(&mut game);

    assert!(
        !on_battlefield(&game, cards::CIRCLE_OF_PROTECTION_BLUE),
        "it is gone"
    );
}
