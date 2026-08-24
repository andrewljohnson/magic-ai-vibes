//! Virtue of Loyalty // Ardenvale Fealty: a Knight now, and the enchantment
//! later out of exile.

use super::*;

/// Player One holding the card, on their own turn, with `mana` white.
fn staged(mana: u16) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let card = game
        .build_zone(PlayerId::One, &[cards::VIRTUE_OF_LOYALTY])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let card_id = card.id;
    game.players[0].hand.push(card);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, mana);
    (game, card_id)
}

fn settle(game: &mut Game) {
    for _ in 0..32 {
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

fn cast_with(game: &Game, card: GameObjectId, option: PlayOptionId) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell {
                card: id, choices, ..
            } => *id == card && choices.play_option() == option,
            _ => false,
        })
}

fn tokens(game: &Game) -> Vec<GameObjectId> {
    game.battlefield
        .iter()
        .filter(|permanent| permanent.card.definition == ObjectKind::Token)
        .map(|permanent| permanent.card.id)
        .collect()
}

/// Both halves are on offer from hand, each for its own cost.
#[test]
fn both_halves_are_castable_from_hand() {
    let (game, card) = staged(5);

    assert!(
        cast_with(&game, card, PlayOptionId::DEFAULT).is_some(),
        "the enchantment is castable",
    );
    assert!(
        cast_with(&game, card, PlayOptionId(1)).is_some(),
        "and so is the Adventure",
    );
}

/// Two mana makes a 2/2 Knight with vigilance, and the card goes to exile
/// rather than to the graveyard.
#[test]
fn the_adventure_makes_a_knight_and_exiles_itself() {
    let (mut game, card) = staged(2);

    let fealty = cast_with(&game, card, PlayOptionId(1)).expect("two mana casts it");
    game.apply(PlayerId::One, fealty).expect("it is cast");
    settle(&mut game);

    let made = tokens(&game);
    assert_eq!(made.len(), 1, "one Knight");
    let knight = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == made[0])
        .expect("still there");
    assert_eq!(game.power(knight), Some(2));
    assert_eq!(game.toughness(knight), Some(2));
    assert!(game.permanent_has_executable_keyword(knight, KeywordAbility::Vigilance));

    assert!(game.players[0].graveyard.is_empty(), "not the graveyard");
    assert_eq!(
        game.players[0]
            .exile
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::VIRTUE_OF_LOYALTY],
    );
}

/// And the enchantment may be cast out of exile afterwards -- as the
/// enchantment, never as the Adventure again.
#[test]
fn the_enchantment_follows_from_exile() {
    let (mut game, card) = staged(2);
    let fealty = cast_with(&game, card, PlayOptionId(1)).expect("two mana casts it");
    game.apply(PlayerId::One, fealty).expect("it is cast");
    settle(&mut game);
    let exiled = game.players[0]
        .exile
        .first()
        .map(|card| card.id)
        .expect("the card is in exile");

    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 5);
    assert!(
        cast_with(&game, exiled, PlayOptionId(1)).is_none(),
        "the adventure cannot be taken twice",
    );
    let virtue = cast_with(&game, exiled, PlayOptionId::DEFAULT)
        .expect("the enchantment may be cast from exile");
    game.apply(PlayerId::One, virtue).expect("it is cast");
    settle(&mut game);

    assert!(
        game.battlefield.iter().any(
            |permanent| permanent.card.definition == ObjectKind::Card(cards::VIRTUE_OF_LOYALTY)
        ),
        "the enchantment is on the battlefield",
    );
}

/// The end step grows every creature you control and untaps them, and leaves
/// the other player's alone.
#[test]
fn the_end_step_grows_and_untaps_your_creatures() {
    let (mut game, card) = staged(5);
    let virtue = cast_with(&game, card, PlayOptionId::DEFAULT).expect("five mana casts it");
    game.apply(PlayerId::One, virtue).expect("it is cast");
    settle(&mut game);
    let bears = game
        .put_onto_battlefield(PlayerId::One, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    let theirs = game
        .put_onto_battlefield(PlayerId::Two, cards::SERRA_ANGEL)
        .expect("cataloged");
    drain_pending(&mut game);
    game.tap_permanent(bears);
    game.tap_permanent(theirs);

    game.step = Step::End;
    game.begin_step_triggers();
    settle(&mut game);

    let grown = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == bears)
        .expect("the Bears are there");
    assert_eq!(game.power(grown), Some(3), "a counter arrived");
    assert_eq!(game.toughness(grown), Some(3));
    assert!(!grown.tapped, "and it untapped");

    let untouched = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == theirs)
        .expect("their Angel is there");
    assert_eq!(game.power(untouched), Some(4), "theirs grew nothing");
    assert!(untouched.tapped, "and stayed tapped");
}

/// It is your end step, not everybody's.
#[test]
fn it_does_nothing_on_their_end_step() {
    let (mut game, card) = staged(5);
    let virtue = cast_with(&game, card, PlayOptionId::DEFAULT).expect("five mana casts it");
    game.apply(PlayerId::One, virtue).expect("it is cast");
    settle(&mut game);
    let bears = game
        .put_onto_battlefield(PlayerId::One, cards::GRIZZLY_BEARS)
        .expect("cataloged");
    drain_pending(&mut game);

    game.active_player = PlayerId::Two;
    game.step = Step::End;
    game.begin_step_triggers();
    settle(&mut game);

    let unchanged = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == bears)
        .expect("the Bears are there");
    assert_eq!(game.power(unchanged), Some(2), "no counter on their turn");
}
