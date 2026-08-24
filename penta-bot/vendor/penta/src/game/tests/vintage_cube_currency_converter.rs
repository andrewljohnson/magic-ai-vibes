//! Currency Converter: a one-mana bank that holds the cards you throw away
//! and pays them back later as a Treasure or a body.

use super::*;

/// Player One with the Converter out since last turn and `hand` in hand.
fn staged(hand: &[CardDefinitionId]) -> (Game, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    game.players[0].graveyard.clear();
    for (index, definition) in hand.iter().enumerate() {
        game.players[0].hand.push(card(
            120_000 + u32::try_from(index).expect("few cards"),
            *definition,
            PlayerId::One,
        ));
    }
    let converter = game
        .put_onto_battlefield(PlayerId::One, cards::CURRENCY_CONVERTER)
        .expect("cataloged");
    drain_pending(&mut game);
    game.turns_started = [5, 5];
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, converter)
}

fn deciding(game: &Game) -> Option<PlayerId> {
    game.pending_decisions
        .first()
        .map(|pending| pending.observation.player)
}

/// Passes priority until somebody is asked something or nothing is left.
fn settle(game: &mut Game) {
    for _ in 0..24 {
        if deciding(game).is_some() {
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

/// Answers the pending decision with the option whose label contains
/// `wanted`, or with the first option when nothing is named.
fn answer(game: &mut Game, wanted: Option<&str>) {
    settle(game);
    let seat = deciding(game).expect("something is being asked");
    let decision = game.observe(seat).decision.expect("just checked");
    let option = match wanted {
        Some(wanted) => decision
            .options
            .iter()
            .find(|option| option.label.contains(wanted))
            .unwrap_or_else(|| panic!("no option named {wanted}: {:?}", decision.options)),
        None => decision.options.first().expect("something to choose"),
    };
    let options = vec![option.id];
    game.apply(
        seat,
        Action::ChooseDecision {
            decision: decision.id,
            options,
        },
    )
    .expect("the offered choice is legal");
    settle(game);
}

fn activate(game: &mut Game, converter: GameObjectId, ability: u8) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source,
                ability: AbilityOrigin::Printed { ability: id, .. },
                ..
            } => *source == converter && *id == AbilityId(ability),
            _ => false,
        })
        .unwrap_or_else(|| panic!("ability {ability} is activatable"));
    game.apply(PlayerId::One, action).expect("it activates");
    settle(game);
}

/// Puts a card straight into exile linked to the Converter, which is where
/// the banking trigger would have left it.
fn bank(game: &mut Game, converter: GameObjectId, definition: CardDefinitionId) -> GameObjectId {
    let card = game
        .build_zone(PlayerId::One, &[definition])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let id = card.id;
    game.players[0].exile.push(card);
    game.linked_exiles.push((converter, id));
    id
}

fn subtyped(game: &Game, subtype: &str) -> Vec<GameObjectId> {
    game.battlefield
        .iter()
        .filter(|permanent| game.effective_subtypes(permanent).contains(&subtype))
        .map(|permanent| permanent.card.id)
        .collect()
}

/// The loot draws and discards, and the discard offers to bank the card.
#[test]
fn a_banked_discard_leaves_the_graveyard() {
    let (mut game, converter) = staged(&[cards::MOUNTAIN]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);

    activate(&mut game, converter, 1);
    // The card drawn and the Mountain are both discardable; either way the
    // trigger that follows is about whichever one went.
    answer(&mut game, None);
    answer(&mut game, Some("Do it"));

    assert!(
        game.players[0].graveyard.is_empty(),
        "the discarded card was exiled out of the graveyard",
    );
    assert_eq!(
        game.linked_exiles
            .iter()
            .filter(|(source, _)| *source == converter)
            .count(),
        1,
        "and it is exiled with the Converter, not merely exiled",
    );
}

/// Declining leaves the card where the discard put it.
#[test]
fn a_declined_discard_stays_in_the_graveyard() {
    let (mut game, converter) = staged(&[cards::MOUNTAIN]);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);

    activate(&mut game, converter, 1);
    answer(&mut game, None);
    answer(&mut game, Some("Decline"));

    assert_eq!(game.players[0].graveyard.len(), 1);
    assert!(game.linked_exiles.is_empty());
}

/// A land cashes out for a Treasure, and the card lands in the graveyard.
/// Which of the banked cards goes is a choice, and the rest stay banked.
#[test]
fn cashing_out_a_land_pays_a_treasure() {
    let (mut game, converter) = staged(&[]);
    bank(&mut game, converter, cards::MOUNTAIN);
    bank(&mut game, converter, cards::LIGHTNING_BOLT);

    activate(&mut game, converter, 2);
    answer(&mut game, Some("Mountain"));

    assert_eq!(subtyped(&game, "Treasure").len(), 1);
    assert_eq!(
        game.players[0]
            .graveyard
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::MOUNTAIN],
    );
    assert_eq!(
        game.players[0]
            .exile
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::LIGHTNING_BOLT],
        "what was not chosen is still banked",
    );
}

/// Anything else cashes out for a 2/2 Rogue.
#[test]
fn cashing_out_a_spell_pays_a_rogue() {
    let (mut game, converter) = staged(&[]);
    bank(&mut game, converter, cards::LIGHTNING_BOLT);

    activate(&mut game, converter, 2);
    if deciding(&game).is_some() {
        answer(&mut game, None);
    }

    assert!(subtyped(&game, "Treasure").is_empty());
    let rogue = subtyped(&game, "Rogue");
    assert_eq!(rogue.len(), 1, "a nonland card pays a body");
    let rogue = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == rogue[0])
        .expect("just found");
    assert_eq!(game.power(rogue), Some(2));
    assert_eq!(game.toughness(rogue), Some(2));
    assert_eq!(
        game.players[0]
            .graveyard
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![cards::LIGHTNING_BOLT],
    );
}

/// A discard paid as a cost is still a discard: cycling a card offers to
/// bank it just as the loot does.
#[test]
fn cycling_a_card_is_a_discard() {
    let (mut game, converter) = staged(&[cards::KETRIA_TRIOME]);
    let triome = game.players[0].hand[0].id;
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 3);

    let cycle = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(
            |action| matches!(action, Action::ActivateAbility { source, .. } if *source == triome),
        )
        .expect("three mana pays for cycling");
    game.apply(PlayerId::One, cycle).expect("it cycles");
    answer(&mut game, Some("Do it"));

    assert!(
        game.players[0].graveyard.is_empty(),
        "the cycled card was banked out of the graveyard",
    );
    assert_eq!(
        game.linked_exiles
            .iter()
            .filter(|(source, _)| *source == converter)
            .count(),
        1,
    );
}
