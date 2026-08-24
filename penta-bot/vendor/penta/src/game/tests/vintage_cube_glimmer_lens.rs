//! Glimmer Lens: an Equipment that brings its own creature and then asks for
//! a second one before it pays off.

use super::*;

/// The Lens on the battlefield with its Rebel, both player one's.
fn lensed() -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.turns_started[PlayerId::One.index()] = 5;
    let lens = game
        .put_onto_battlefield(PlayerId::One, cards::GLIMMER_LENS)
        .expect("cataloged");
    drain_pending(&mut game);
    game.check_state_based_actions();
    let rebel = game
        .battlefield
        .iter()
        .find(|permanent| {
            is_token_with(
                permanent,
                tokens::creature(&["Rebel"], &[ManaColor::Red], 2, 2),
            )
        })
        .expect("For Mirrodin! made one")
        .card
        .id;
    game.active_player = PlayerId::One;
    game.priority = PlayerId::One;
    (game, lens, rebel)
}

fn attack_with(game: &mut Game, attackers: &[GameObjectId]) {
    game.step = Step::DeclareAttackers;
    for attacker in attackers {
        game.declare_attacker(*attacker, AttackDefender::Player(PlayerId::Two));
    }
    game.finish_declaring_attackers();
    drain_pending(game);
}

fn host_of(game: &Game, equipment: GameObjectId) -> Option<GameObjectId> {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == equipment)
        .and_then(|permanent| permanent.attached_to)
}

fn hand_size(game: &Game) -> usize {
    game.players[PlayerId::One.index()].hand.len()
}

/// For Mirrodin! makes a 2/2 red Rebel and puts the Lens on it.
#[test]
fn it_arrives_with_a_rebel_already_carrying_it() {
    let (game, lens, rebel) = lensed();

    let token = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == rebel)
        .expect("the Rebel is there");
    assert_eq!(
        (game.power(token), game.toughness(token)),
        (Some(2), Some(2))
    );
    assert_eq!(
        host_of(&game, lens),
        Some(rebel),
        "the Equipment attaches itself to what it just made",
    );
}

/// One attacker is not "and at least one other creature".
#[test]
fn the_rebel_attacking_alone_draws_nothing() {
    let (mut game, _, rebel) = lensed();
    let before = hand_size(&game);

    attack_with(&mut game, &[rebel]);

    assert_eq!(hand_size(&game), before);
}

/// Two attackers, one of them the equipped creature: a card.
#[test]
fn the_rebel_plus_a_friend_draws_one() {
    let (mut game, _, rebel) = lensed();
    let friend = creature(70_000, cards::GRIZZLY_BEARS, PlayerId::One);
    let friend_id = friend.card.id;
    game.battlefield.push(friend);
    let before = hand_size(&game);

    attack_with(&mut game, &[rebel, friend_id]);

    assert_eq!(hand_size(&game), before + 1);
}

/// The trigger is the equipped creature's, not any two attackers'. A pair
/// that leaves the Rebel at home draws nothing.
#[test]
fn two_other_attackers_without_the_equipped_one_draw_nothing() {
    let (mut game, _, _) = lensed();
    let first = creature(70_100, cards::GRIZZLY_BEARS, PlayerId::One);
    let first_id = first.card.id;
    game.battlefield.push(first);
    let second = creature(70_101, cards::SAVANNAH_LIONS, PlayerId::One);
    let second_id = second.card.id;
    game.battlefield.push(second);
    let before = hand_size(&game);

    attack_with(&mut game, &[first_id, second_id]);

    assert_eq!(hand_size(&game), before);
}

/// Equip moves the trigger's subject. Once the Lens is on the Bears, the
/// Bears and one other attacker are what draws.
#[test]
fn equipping_something_else_moves_the_trigger() {
    let (mut game, lens, rebel) = lensed();
    let bears = creature(70_200, cards::GRIZZLY_BEARS, PlayerId::One);
    let bears_id = bears.card.id;
    game.battlefield.push(bears);
    game.players[PlayerId::One.index()].mana_pool.colorless = 1;
    game.players[PlayerId::One.index()].mana_pool.white = 1;
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == lens
                    && targets
                        .iter()
                        .flat_map(crate::casting::TargetSelection::targets)
                        .any(|target| *target == Target::Permanent(bears_id))
            }
            _ => false,
        })
        .expect("equip is offered for the Bears");
    game.apply(PlayerId::One, action).expect("it activates");
    drain_pending(&mut game);
    assert_eq!(host_of(&game, lens), Some(bears_id));

    let before = hand_size(&game);
    attack_with(&mut game, &[bears_id, rebel]);
    assert_eq!(hand_size(&game), before + 1);
}
