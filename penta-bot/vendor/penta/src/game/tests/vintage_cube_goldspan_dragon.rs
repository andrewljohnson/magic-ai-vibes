//! Goldspan Dragon: he pays for himself, whether he is attacking or being
//! answered, and the Treasures he leaves are worth twice what they say.

use super::*;

/// The Dragon on the battlefield, ready to attack.
fn staged() -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let dragon = game
        .put_onto_battlefield(PlayerId::One, cards::GOLDSPAN_DRAGON)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, dragon)
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

fn treasures(game: &Game) -> Vec<GameObjectId> {
    game.battlefield
        .iter()
        .filter(|permanent| game.effective_subtypes(permanent).contains(&"Treasure"))
        .map(|permanent| permanent.card.id)
        .collect()
}

/// He is hasty, so the attack that pays for him happens the turn he lands.
#[test]
fn he_flies_and_attacks_the_turn_he_lands() {
    let (game, dragon) = staged();
    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == dragon)
        .expect("he is there");

    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Flying));
    assert!(game.permanent_has_executable_keyword(permanent, KeywordAbility::Haste));
}

/// Attacking makes a Treasure.
#[test]
fn attacking_makes_a_treasure() {
    let (mut game, dragon) = staged();
    game.step = Step::DeclareAttackers;
    game.declare_attacker(dragon, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    settle(&mut game);

    assert_eq!(treasures(&game).len(), 1);
}

/// So does being targeted, whatever the spell does about it.
#[test]
fn being_targeted_makes_a_treasure() {
    let (mut game, dragon) = staged();
    let bolt = card(180_000, cards::LIGHTNING_BOLT, PlayerId::Two);
    let bolt_id = bolt.id;
    game.players[1].hand.push(bolt);
    game.add_unrestricted_mana(PlayerId::Two, ManaColor::Red, 1);
    game.priority = PlayerId::Two;

    let cast = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == bolt_id
                    && choices
                        .iter_targets()
                        .any(|target| *target == Target::Permanent(dragon))
            }
            _ => false,
        })
        .expect("the Dragon is a legal target");
    game.apply(PlayerId::Two, cast).expect("it is cast");
    settle(&mut game);

    assert_eq!(
        treasures(&game).len(),
        1,
        "the trigger is the targeting, not what the spell did",
    );
}

/// A Treasure of his is worth two mana of one colour rather than one of any.
#[test]
fn his_treasures_are_worth_two() {
    let (mut game, dragon) = staged();
    game.step = Step::DeclareAttackers;
    game.declare_attacker(dragon, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    settle(&mut game);
    let treasure = treasures(&game)[0];

    let doubled = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            // The granted ability, not the Treasure's own: both make blue,
            // and only one of them makes two of it.
            Action::ActivateManaAbility {
                source,
                ability:
                    AbilityOrigin::Granted {
                        source: granter, ..
                    },
                color,
                ..
            } => *source == treasure && *granter == dragon && *color == ManaColor::Blue,
            _ => false,
        })
        .expect("his grant reaches the Treasure he just made");
    game.apply(PlayerId::One, doubled).expect("it activates");

    assert_eq!(
        game.players[0].mana.len(),
        2,
        "two mana of the one colour chosen",
    );
    assert!(treasures(&game).is_empty(), "and the Treasure is spent");
}

/// The grant reaches Treasures however they arrived, and stops when he does.
#[test]
fn the_grant_covers_every_treasure_and_ends_with_him() {
    let (mut game, dragon) = staged();
    game.create_token(PlayerId::One, tokens::treasure());
    drain_pending(&mut game);

    let outside = treasures(&game)[0];
    assert!(
        game.legal_actions(PlayerId::One).iter().any(|action| {
            matches!(action, Action::ActivateManaAbility { source, .. } if *source == outside)
        }),
        "a Treasure he did not make still has the ability",
    );

    game.battlefield
        .retain(|permanent| permanent.card.id != dragon);
    game.check_state_based_actions();

    let activations = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| {
            matches!(action, Action::ActivateManaAbility { source, .. } if *source == outside)
        })
        .count();
    assert_eq!(
        activations, 5,
        "with him gone only the Treasure's own five colours remain",
    );
}
