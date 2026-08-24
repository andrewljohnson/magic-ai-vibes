//! Plagon, Lord of the Beach: a 0/3 that pays for a board of walls and then
//! sends them into combat with the other number.

use super::*;

fn staged() -> Game {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    for _ in 0..6 {
        let card = game
            .build_zone(PlayerId::One, &[cards::MOUNTAIN])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[0].library.push(card);
    }
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

/// He counts himself and every other creature whose toughness is the bigger
/// number, and nothing else.
#[test]
fn he_draws_for_each_defensive_creature() {
    let mut game = staged();
    // A 0/5 Wall qualifies; a 2/2 does not, and neither does theirs.
    game.battlefield
        .push(creature(210_000, cards::WALL_OF_STONE, PlayerId::One));
    game.battlefield
        .push(creature(210_001, cards::GRIZZLY_BEARS, PlayerId::One));
    game.battlefield
        .push(creature(210_002, cards::WALL_OF_STONE, PlayerId::Two));

    game.put_onto_battlefield(PlayerId::One, cards::PLAGON_LORD_OF_THE_BEACH)
        .expect("cataloged");
    settle(&mut game);

    assert_eq!(
        game.players[0].hand.len(),
        2,
        "his own 0/3 and your 0/5, not the 2/2 and not theirs",
    );
}

/// The activation makes a creature hit for its toughness, and changes
/// neither of its numbers.
#[test]
fn the_activation_swaps_which_number_is_dealt() {
    let mut game = staged();
    let plagon = game
        .put_onto_battlefield(PlayerId::One, cards::PLAGON_LORD_OF_THE_BEACH)
        .expect("cataloged");
    settle(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 1);
    game.players[1].life = 20;

    let activate = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == plagon
                    && targets
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|target| *target == Target::Permanent(plagon))
            }
            _ => false,
        })
        .expect("he may name himself");
    game.apply(PlayerId::One, activate).expect("it activates");
    settle(&mut game);

    let permanent = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == plagon)
        .expect("he is there");
    assert_eq!(game.power(permanent), Some(0), "his power is untouched");
    assert_eq!(game.toughness(permanent), Some(3));

    game.step = Step::DeclareAttackers;
    game.declare_attacker(plagon, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    settle(&mut game);
    game.step = Step::CombatDamage;
    game.deal_combat_damage();
    settle(&mut game);

    assert_eq!(
        game.players[1].life, 17,
        "a 0/3 that assigns its toughness hits for three",
    );
}

/// Without the activation the same creature deals nothing.
#[test]
fn without_it_a_zero_power_creature_deals_nothing() {
    let mut game = staged();
    let plagon = game
        .put_onto_battlefield(PlayerId::One, cards::PLAGON_LORD_OF_THE_BEACH)
        .expect("cataloged");
    settle(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.players[1].life = 20;

    game.step = Step::DeclareAttackers;
    game.declare_attacker(plagon, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    settle(&mut game);
    game.step = Step::CombatDamage;
    game.deal_combat_damage();
    settle(&mut game);

    assert_eq!(game.players[1].life, 20);
}

/// And it lasts only the turn.
#[test]
fn it_wears_off_with_the_turn() {
    let mut game = staged();
    let plagon = game
        .put_onto_battlefield(PlayerId::One, cards::PLAGON_LORD_OF_THE_BEACH)
        .expect("cataloged");
    settle(&mut game);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Blue, 1);

    let activate = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == plagon),
        )
        .expect("he may name himself");
    game.apply(PlayerId::One, activate).expect("it activates");
    settle(&mut game);

    game.complete_cleanup();
    game.commit_next_turn(PlayerId::Two, Vec::new());
    game.complete_cleanup();
    game.commit_next_turn(PlayerId::One, Vec::new());
    game.players[1].life = 20;
    game.step = Step::DeclareAttackers;
    game.declare_attacker(plagon, AttackDefender::Player(PlayerId::Two));
    game.finish_declaring_attackers();
    settle(&mut game);
    game.step = Step::CombatDamage;
    game.deal_combat_damage();
    settle(&mut game);

    assert_eq!(game.players[1].life, 20, "the swap ended with the turn");
}
