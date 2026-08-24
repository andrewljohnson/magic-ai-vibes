//! Agatha's Soul Cauldron: a graveyard eater that hands what it ate to
//! whichever of your creatures is carrying a counter, and pays for their
//! abilities with whatever mana you happen to have.

use super::*;

/// Player One with a Cauldron on the battlefield, `graveyard` in Player Two's
/// graveyard, and `battlefield` under Player One beside the Cauldron.
fn staged(
    graveyard: &[CardDefinitionId],
    battlefield: &[CardDefinitionId],
) -> (Game, GameObjectId, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[1].graveyard.clear();
    for definition in graveyard {
        let card = game
            .build_zone(PlayerId::Two, &[*definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[1].graveyard.push(card);
    }
    let cauldron = game
        .put_onto_battlefield(PlayerId::One, cards::AGATHAS_SOUL_CAULDRON)
        .expect("cataloged");
    let mut theirs = Vec::new();
    for definition in battlefield {
        theirs.push(
            game.put_onto_battlefield(PlayerId::One, *definition)
                .expect("cataloged"),
        );
    }
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    drain_pending(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, cauldron, theirs)
}

fn resolve(game: &mut Game) {
    for _ in 0..16 {
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

fn counters(game: &Game, permanent: GameObjectId) -> u16 {
    game.battlefield
        .iter()
        .find(|candidate| candidate.card.id == permanent)
        .expect("it is on the battlefield")
        .counters(CounterKind::PlusOnePlusOne)
}

fn put_a_counter_on(game: &mut Game, permanent: GameObjectId) {
    game.battlefield
        .iter_mut()
        .find(|candidate| candidate.card.id == permanent)
        .expect("it is on the battlefield")
        .add_counters(CounterKind::PlusOnePlusOne, 1);
}

/// Taps the Cauldron for the card of `definition` in Player Two's graveyard,
/// naming `grow` for the reflexive counter when one is given.
fn eat(
    game: &mut Game,
    cauldron: GameObjectId,
    definition: CardDefinitionId,
    grow: Option<GameObjectId>,
) {
    let target = game.players[1]
        .graveyard
        .iter()
        .find(|card| card.definition == definition)
        .expect("it is in the graveyard")
        .id;
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == cauldron
                    && targets
                        .iter()
                        .any(|selection| selection.targets().contains(&Target::Card(target)))
                    && grow.is_none_or(|grow| {
                        targets
                            .iter()
                            .any(|selection| selection.targets().contains(&Target::Permanent(grow)))
                    })
            }
            _ => false,
        })
        .expect("a card in a graveyard is a legal target");
    game.apply(PlayerId::One, action).expect("it activates");
    resolve(game);
}

/// Every activated ability the permanent currently has, by its rules text.
fn activated_ability_texts(game: &Game, permanent: GameObjectId) -> Vec<&'static str> {
    let permanent = game
        .battlefield
        .iter()
        .find(|candidate| candidate.card.id == permanent)
        .expect("it is on the battlefield");
    let mut texts = Vec::new();
    let _ = game.visit_effective_abilities(permanent, |effective| {
        if matches!(
            effective.ability.definition,
            crate::card::DeclarativeAbilityDef::Activated(_)
        ) {
            texts.push(effective.ability.text);
        }
        std::ops::ControlFlow::Continue(())
    });
    texts
}

/// Activates the ability whose printed text starts with `prefix`.
fn activate(game: &mut Game, source: GameObjectId, prefix: &str) {
    let action = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source: candidate, ..
            } => *candidate == source && ability_text(game, action).starts_with(prefix),
            _ => false,
        })
        .unwrap_or_else(|| panic!("{prefix} is activatable"));
    game.apply(PlayerId::One, action).expect("it activates");
    resolve(game);
}

/// The printed text behind one activation action, so a test can name an
/// ability the way the card prints it rather than by index.
fn ability_text(game: &Game, action: &Action) -> &'static str {
    let Action::ActivateAbility {
        source, ability, ..
    } = action
    else {
        return "";
    };
    let permanent = game
        .battlefield
        .iter()
        .find(|candidate| candidate.card.id == *source)
        .expect("the activation names a permanent");
    let mut text = "";
    let _ = game.visit_effective_abilities(permanent, |effective| {
        if effective.origin == *ability {
            text = effective.ability.text;
            return std::ops::ControlFlow::Break(());
        }
        std::ops::ControlFlow::Continue(())
    });
    text
}

fn power(game: &Game, permanent: GameObjectId) -> i16 {
    let permanent = game
        .battlefield
        .iter()
        .find(|candidate| candidate.card.id == permanent)
        .expect("it is on the battlefield");
    game.power(permanent).expect("it is a creature")
}

/// The reflexive trigger fires for a creature card and grows the creature the
/// activation named.
#[test]
fn eating_a_creature_card_grows_the_named_creature() {
    let (mut game, cauldron, mine) =
        staged(&[cards::ORDER_OF_THE_EBON_HAND], &[cards::SAVANNAH_LIONS]);
    let lion = mine[0];

    eat(
        &mut game,
        cauldron,
        cards::ORDER_OF_THE_EBON_HAND,
        Some(lion),
    );

    assert_eq!(counters(&game, lion), 1, "a creature card was exiled");
}

/// "When a creature card is exiled this way" is a real condition: a land
/// leaves the graveyard just the same, and nothing grows.
#[test]
fn eating_a_noncreature_card_grows_nothing() {
    let (mut game, cauldron, mine) = staged(&[cards::PLAINS], &[cards::SAVANNAH_LIONS]);
    let lion = mine[0];

    eat(&mut game, cauldron, cards::PLAINS, Some(lion));

    assert_eq!(counters(&game, lion), 0, "no creature card was exiled");
    assert!(
        game.players[1].graveyard.is_empty(),
        "the land was exiled either way",
    );
}

/// The grant is gated on counters, not on being a creature: a creature with
/// none of them reads only its own text.
#[test]
fn a_creature_without_counters_gains_nothing() {
    let (mut game, cauldron, mine) =
        staged(&[cards::ORDER_OF_THE_EBON_HAND], &[cards::SAVANNAH_LIONS]);
    let lion = mine[0];

    eat(&mut game, cauldron, cards::ORDER_OF_THE_EBON_HAND, None);

    assert_eq!(counters(&game, lion), 0, "nothing named it");
    assert!(
        activated_ability_texts(&game, lion).is_empty(),
        "a Savannah Lions with no counters prints no activated abilities: {:?}",
        activated_ability_texts(&game, lion),
    );
}

/// Both of the exiled card's activated abilities land on the creature the
/// counter went to.
#[test]
fn a_countered_creature_gains_every_exiled_activated_ability() {
    let (mut game, cauldron, mine) =
        staged(&[cards::ORDER_OF_THE_EBON_HAND], &[cards::SAVANNAH_LIONS]);
    let lion = mine[0];

    eat(
        &mut game,
        cauldron,
        cards::ORDER_OF_THE_EBON_HAND,
        Some(lion),
    );

    let texts = activated_ability_texts(&game, lion);
    assert_eq!(
        texts.len(),
        2,
        "the Order prints two activated abilities: {texts:?}",
    );
    assert!(
        texts
            .iter()
            .any(|text| text.starts_with("{B}{B}: This creature gets +1/+0")),
        "the pump came along: {texts:?}",
    );
}

/// Protection from white is a static ability, so it stays behind: only
/// activated abilities are handed out.
#[test]
fn the_exiled_cards_static_abilities_stay_behind() {
    let (mut game, cauldron, mine) =
        staged(&[cards::ORDER_OF_THE_EBON_HAND], &[cards::SAVANNAH_LIONS]);
    let lion = mine[0];
    put_a_counter_on(&mut game, lion);

    eat(&mut game, cauldron, cards::ORDER_OF_THE_EBON_HAND, None);

    let lion_permanent = game
        .battlefield
        .iter()
        .find(|candidate| candidate.card.id == lion)
        .expect("it is on the battlefield");
    let mut texts = Vec::new();
    let _ = game.visit_effective_abilities(lion_permanent, |effective| {
        texts.push(effective.ability.text);
        std::ops::ControlFlow::Continue(())
    });
    assert!(
        !texts.iter().any(|text| text.contains("Protection from")),
        "protection from white is static and was not granted: {texts:?}",
    );
    assert_eq!(
        activated_ability_texts(&game, lion).len(),
        2,
        "the two activated abilities did come across",
    );
}

/// The granted ability is not decoration: it can be activated, and it does
/// what the exiled card says.
#[test]
fn a_granted_ability_can_be_activated() {
    let (mut game, cauldron, mine) =
        staged(&[cards::ORDER_OF_THE_EBON_HAND], &[cards::SAVANNAH_LIONS]);
    let lion = mine[0];
    eat(
        &mut game,
        cauldron,
        cards::ORDER_OF_THE_EBON_HAND,
        Some(lion),
    );
    let before = power(&game, lion);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 2);

    activate(&mut game, lion, "{B}{B}: This creature gets +1/+0");

    assert_eq!(
        power(&game, lion),
        before + 1,
        "the granted pump resolved on the creature that has it",
    );
}

/// The Cauldron's other half: white mana pays a black activation cost,
/// because the ability belongs to a creature its controller controls.
#[test]
fn white_mana_pays_a_creatures_black_ability() {
    let (mut game, _cauldron, mine) = staged(&[], &[cards::ORDER_OF_THE_EBON_HAND]);
    let order = mine[0];
    let before = power(&game, order);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 2);

    activate(&mut game, order, "{B}{B}: This creature gets +1/+0");

    assert_eq!(
        power(&game, order),
        before + 1,
        "the permission let two white mana pay {{B}}{{B}}",
    );
}

/// Without the Cauldron the same white mana pays for nothing, which is what
/// makes the permission the reason the activation above was offered.
#[test]
fn without_the_cauldron_white_mana_pays_nothing_black() {
    let (mut game, cauldron, mine) = staged(&[], &[cards::ORDER_OF_THE_EBON_HAND]);
    let order = mine[0];
    game.battlefield
        .retain(|permanent| permanent.card.id != cauldron);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 2);

    assert!(
        !game.legal_actions(PlayerId::One).iter().any(|action| {
            matches!(action, Action::ActivateAbility { source, .. } if *source == order)
                && ability_text(&game, action).starts_with("{B}{B}")
        }),
        "white mana does not pay a black cost on its own",
    );
}

/// The permission is about abilities, not spells: a black card in hand is
/// still uncastable off white mana.
#[test]
fn the_permission_does_not_reach_spells() {
    let (mut game, _cauldron, _mine) = staged(&[], &[]);
    game.players[0].hand.clear();
    let card = game
        .build_zone(PlayerId::One, &[cards::ORDER_OF_THE_EBON_HAND])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let order = card.id;
    game.players[0].hand.push(card);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 2);

    assert!(
        !game
            .legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if *card == order)),
        "{{B}}{{B}} for a creature spell is not what the Cauldron permits",
    );
}

/// The pile accumulates: a second creature card adds its abilities to what
/// the first one already handed out.
#[test]
fn a_second_exiled_creature_adds_its_abilities_too() {
    let (mut game, cauldron, mine) = staged(
        &[cards::ORDER_OF_THE_EBON_HAND, cards::PRODIGAL_SORCERER],
        &[cards::SAVANNAH_LIONS],
    );
    let lion = mine[0];
    put_a_counter_on(&mut game, lion);

    eat(&mut game, cauldron, cards::ORDER_OF_THE_EBON_HAND, None);
    let after_one = activated_ability_texts(&game, lion).len();
    for permanent in &mut game.battlefield {
        permanent.tapped = false;
    }
    eat(&mut game, cauldron, cards::PRODIGAL_SORCERER, None);

    assert_eq!(after_one, 2, "the Order printed two");
    assert_eq!(
        activated_ability_texts(&game, lion).len(),
        3,
        "the Sorcerer's tapper came along as well: {:?}",
        activated_ability_texts(&game, lion),
    );
}

/// The grant is read fresh every time, so a creature that loses its last
/// counter hands the abilities straight back.
#[test]
fn losing_the_last_counter_takes_the_abilities_back() {
    let (mut game, cauldron, mine) =
        staged(&[cards::ORDER_OF_THE_EBON_HAND], &[cards::SAVANNAH_LIONS]);
    let lion = mine[0];
    eat(
        &mut game,
        cauldron,
        cards::ORDER_OF_THE_EBON_HAND,
        Some(lion),
    );
    assert_eq!(activated_ability_texts(&game, lion).len(), 2, "granted");

    game.battlefield
        .iter_mut()
        .find(|candidate| candidate.card.id == lion)
        .expect("it is on the battlefield")
        .remove_counters(CounterKind::PlusOnePlusOne, 1);

    assert!(
        activated_ability_texts(&game, lion).is_empty(),
        "the abilities went back with the counter: {:?}",
        activated_ability_texts(&game, lion),
    );
}
