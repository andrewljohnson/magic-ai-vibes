//! Pillage the Bog: a dig as deep as your mana base, and plot is what makes
//! it free on the turn it matters.

use super::*;

/// Player One with `lands` lands out, the card in hand, and `library`
/// stacked so the last entry is on top.
fn staged(lands: usize, library: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].library.clear();
    game.players[0].exile.clear();
    for _ in 0..lands {
        game.put_onto_battlefield(PlayerId::One, cards::FOREST)
            .expect("cataloged");
    }
    drain_pending(&mut game);
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
        .build_zone(PlayerId::One, &[cards::PILLAGE_THE_BOG])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let id = spell.id;
    game.players[0].hand.push(spell);
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, id)
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

/// The mana the plot cost wants, exactly: {1}{B}{G}.
fn plot_mana(game: &mut Game) {
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 1);
}

fn untapped_lands(game: &Game) -> usize {
    game.battlefield
        .iter()
        .filter(|permanent| permanent.card.definition == cards::FOREST && !permanent.tapped)
        .count()
}

fn plot_action(game: &Game, card: GameObjectId) -> Option<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::Plot { card: id } if *id == card))
}

/// Two lands look at four cards; one of them is kept.
#[test]
fn the_dig_is_twice_your_lands() {
    let (mut game, spell) = staged(
        2,
        &[
            cards::MOUNTAIN,
            cards::MOUNTAIN,
            cards::MOUNTAIN,
            cards::LIGHTNING_BOLT,
            cards::GRIZZLY_BEARS,
        ],
    );
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 2);

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == spell))
        .expect("two mana pays for it");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);

    assert_eq!(game.players[0].hand.len(), 1, "one card was kept");
    assert_eq!(
        game.players[0].library.len(),
        4,
        "the other three went to the bottom and one was never seen",
    );
}

/// Plot pays the cost now and leaves the card in exile, face up.
#[test]
fn plotting_exiles_it_for_a_later_turn() {
    let (mut game, spell) = staged(2, &[cards::MOUNTAIN]);
    plot_mana(&mut game);

    let plot = plot_action(&game, spell).expect("three mana pays the plot cost");
    game.apply(PlayerId::One, plot).expect("it plots");

    assert!(game.players[0].hand.is_empty());
    assert_eq!(game.players[0].exile.len(), 1, "it is waiting in exile");
    assert!(
        game.players[0].mana.is_empty(),
        "and the plot cost was paid now",
    );
    let plotted = game.players[0].exile[0].id;
    assert!(
        !game
            .legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if *card == plotted)),
        "not on the turn it was plotted",
    );
}

/// On a later turn it is cast for nothing.
#[test]
fn a_plotted_card_is_cast_for_free_later() {
    let (mut game, spell) = staged(2, &[cards::MOUNTAIN, cards::LIGHTNING_BOLT]);
    plot_mana(&mut game);
    let plot = plot_action(&game, spell).expect("three mana pays the plot cost");
    game.apply(PlayerId::One, plot).expect("it plots");
    let plotted = game.players[0].exile[0].id;

    game.commit_next_turn(PlayerId::Two, Vec::new());
    game.commit_next_turn(PlayerId::One, Vec::new());
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| matches!(action, Action::CastSpell { card, .. } if *card == plotted))
        .expect("a plotted card is castable on a later turn");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);

    assert_eq!(game.players[0].hand.len(), 1, "it dug for a card");
    assert_eq!(
        untapped_lands(&game),
        2,
        "and cost nothing to cast: no land was tapped for it",
    );
    assert!(game.players[0].exile.is_empty(), "the card left exile");
}

/// "Plot only as a sorcery": not on the other player's turn.
#[test]
fn plotting_is_a_sorcery_speed_action() {
    let (mut game, spell) = staged(2, &[cards::MOUNTAIN]);
    plot_mana(&mut game);
    game.active_player = PlayerId::Two;

    assert!(plot_action(&game, spell).is_none());
}
