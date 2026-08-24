//! Tear Asunder: two mana for the artifact or enchantment, four for
//! anything, and exile either way.

use super::*;

/// Player One holding it, with `theirs` on the battlefield under player Two.
fn staged(theirs: &[CardDefinitionId]) -> (Game, GameObjectId, Vec<GameObjectId>) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[0].hand.clear();
    let mut ids = Vec::new();
    for definition in theirs {
        ids.push(
            game.put_onto_battlefield(PlayerId::Two, *definition)
                .expect("cataloged"),
        );
    }
    drain_pending(&mut game);
    let spell = game
        .build_zone(PlayerId::One, &[cards::TEAR_ASUNDER])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let spell_id = spell.id;
    game.players[0].hand.push(spell);
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [5, 5];
    game.turn = 9;
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, spell_id, ids)
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

/// Every cast of the spell that names `wanted`.
fn casts_at(game: &Game, spell: GameObjectId, wanted: GameObjectId) -> Vec<Action> {
    game.legal_actions(PlayerId::One)
        .into_iter()
        .filter(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == spell
                    && choices
                        .targets()
                        .iter()
                        .any(|slot| slot.targets().contains(&Target::Permanent(wanted)))
            }
            _ => false,
        })
        .collect()
}

fn on_battlefield(game: &Game, id: GameObjectId) -> bool {
    game.battlefield
        .iter()
        .any(|permanent| permanent.card.id == id)
}

/// Two mana exiles an artifact, and exiles it rather than destroying it.
#[test]
fn two_mana_exiles_an_artifact() {
    let (mut game, spell, theirs) = staged(&[cards::MANIFOLD_KEY]);
    let key = theirs[0];
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 2);

    let cast = casts_at(&game, spell, key)
        .into_iter()
        .next()
        .expect("two mana casts it at the artifact");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);

    assert!(!on_battlefield(&game, key), "the artifact is gone");
    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::MANIFOLD_KEY),
        "exiled, not destroyed",
    );
    assert!(game.players[1].graveyard.is_empty());
}

/// Unkicked it cannot name a creature: that is what the kicker buys.
#[test]
fn unkicked_it_cannot_name_a_creature() {
    let (mut game, spell, theirs) = staged(&[cards::GRIZZLY_BEARS]);
    let bears = theirs[0];
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 2);

    assert!(
        casts_at(&game, spell, bears).is_empty(),
        "two mana names artifacts and enchantments only",
    );
}

/// Kicked it names anything nonland.
#[test]
fn kicked_it_exiles_a_creature() {
    let (mut game, spell, theirs) = staged(&[cards::GRIZZLY_BEARS]);
    let bears = theirs[0];
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 2);

    let cast = casts_at(&game, spell, bears)
        .into_iter()
        .next()
        .expect("four mana casts it at the creature");
    game.apply(PlayerId::One, cast).expect("it is cast");
    settle(&mut game);

    assert!(!on_battlefield(&game, bears));
    assert!(
        game.players[1]
            .exile
            .iter()
            .any(|card| card.definition == cards::GRIZZLY_BEARS),
    );
}

/// Nonland either way: a land is never a legal target.
#[test]
fn it_never_names_a_land() {
    let (mut game, spell, theirs) = staged(&[cards::FOREST]);
    let forest = theirs[0];
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 2);

    assert!(casts_at(&game, spell, forest).is_empty());
}

/// "Instead": the kicked spell targets once, not twice.
#[test]
fn the_kicked_spell_names_one_thing() {
    let (mut game, spell, theirs) = staged(&[cards::MANIFOLD_KEY]);
    let key = theirs[0];
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Green, 2);
    game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 2);

    let casts = casts_at(&game, spell, key);
    assert!(
        casts.iter().all(|action| match action {
            Action::CastSpell { choices, .. } =>
                choices
                    .targets()
                    .iter()
                    .map(|slot| slot.targets().len())
                    .sum::<usize>()
                    == 1,
            _ => false,
        }),
        "one target on every way of casting it",
    );
    assert!(
        !casts.is_empty(),
        "and the kicked cast can still name an artifact",
    );
}
