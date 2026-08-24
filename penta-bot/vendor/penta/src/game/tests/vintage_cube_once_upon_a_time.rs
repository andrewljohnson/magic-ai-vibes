//! Once Upon a Time: free if it is the first thing you do all game, and a
//! two-mana dig for a land or a creature ever after.

use super::*;

/// Player One holding it, with `library` stacked so the last entry is on
/// top, and no mana anywhere.
fn staged(library: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
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
    let spell = game
        .build_zone(PlayerId::One, &[cards::ONCE_UPON_A_TIME])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let spell_id = spell.id;
    game.players[0].hand.push(spell);
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, spell_id)
}

/// Five cards with one creature and one land buried in the middle of them.
const FIVE_CARDS: [CardDefinitionId; 5] = [
    cards::LIGHTNING_BOLT,
    cards::FOREST,
    cards::GRIZZLY_BEARS,
    cards::ANCESTRAL_RECALL,
    cards::COUNTERSPELL,
];

/// Answers whatever is asked, naming `wanted` if it is on offer and taking
/// nothing when it is not.
fn settle_taking(game: &mut Game, wanted: Option<CardDefinitionId>) {
    for _ in 0..24 {
        if let Some(decision) = game
            .pending_decisions
            .first()
            .map(|pending| pending.observation.clone())
        {
            let mut options: Vec<_> = decision
                .options
                .iter()
                .filter(|option| {
                    wanted.is_some_and(|wanted| {
                        matches!(
                            option.card,
                            Some((_, ObjectCharacteristics::Card { definition, .. }))
                                if definition == wanted
                        )
                    })
                })
                .map(|option| option.id)
                .take(1)
                .collect();
            if options.len() < decision.minimum {
                options = decision
                    .options
                    .iter()
                    .map(|option| option.id)
                    .take(decision.minimum)
                    .collect();
            }
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

fn casts(game: &Game, spell: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| matches!(action, Action::CastSpell { card, .. } if *card == spell))
        .collect()
}

fn hand(game: &Game) -> Vec<CardDefinitionId> {
    game.players[0]
        .hand
        .iter()
        .map(|card| card.definition)
        .collect()
}

/// Nothing cast yet and no mana at all: it is still castable, and it finds
/// the creature five cards down.
#[test]
fn the_first_spell_of_the_game_is_free() {
    let (mut game, spell) = staged(&FIVE_CARDS);

    let cast = casts(&game, spell)
        .into_iter()
        .next()
        .expect("the free cast is on offer");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle_taking(&mut game, Some(cards::GRIZZLY_BEARS));

    assert_eq!(hand(&game), vec![cards::GRIZZLY_BEARS]);
    assert_eq!(
        game.players[0].library.len(),
        4,
        "the other four went to the bottom",
    );
}

/// Cast anything at all first and the free cast is gone for the rest of the
/// game -- including for the turn the spell is actually held for.
#[test]
fn a_spell_cast_earlier_closes_the_window() {
    let (mut game, spell) = staged(&FIVE_CARDS);
    let bolt = game
        .build_zone(PlayerId::One, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let bolt_id = bolt.id;
    game.players[0].hand.push(bolt);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Red, 1);
    let cast = casts(&game, bolt_id)
        .into_iter()
        .next()
        .expect("the Bolt is castable");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle_taking(&mut game, None);

    assert!(
        casts(&game, spell).is_empty(),
        "one spell already cast, and no mana left for the printed cost",
    );

    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 2);
    assert_eq!(
        casts(&game, spell).len(),
        1,
        "two mana still buys it the ordinary way",
    );
}

/// The window closes on the spell's own cast too: a second copy is no
/// longer free.
#[test]
fn it_is_only_free_once() {
    let (mut game, spell) = staged(&FIVE_CARDS);
    let second = game
        .build_zone(PlayerId::One, &[cards::ONCE_UPON_A_TIME])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let second_id = second.id;
    game.players[0].hand.push(second);

    let cast = casts(&game, spell)
        .into_iter()
        .next()
        .expect("the free cast is on offer");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle_taking(&mut game, Some(cards::FOREST));

    assert!(
        casts(&game, second_id).is_empty(),
        "the second copy costs mana like anything else",
    );
}

/// "You may": with nothing worth taking among the five, everything goes to
/// the bottom and the hand stays empty.
#[test]
fn a_look_with_nothing_in_it_takes_nothing() {
    let (mut game, spell) = staged(&[
        cards::LIGHTNING_BOLT,
        cards::ANCESTRAL_RECALL,
        cards::COUNTERSPELL,
        cards::LIGHTNING_BOLT,
        cards::ANCESTRAL_RECALL,
    ]);

    let cast = casts(&game, spell)
        .into_iter()
        .next()
        .expect("the free cast is on offer");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle_taking(&mut game, None);

    assert!(hand(&game).is_empty(), "no creature and no land to reveal");
    assert_eq!(game.players[0].library.len(), 5, "all five went back under");
}
