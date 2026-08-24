//! Lavaspur Boots: a one-mana Equipment whose real text is ward, the first
//! of it in this engine.

use super::*;

/// Player One with the Boots strapped to a Savannah Lions, and Player Two
/// holding whatever `theirs` names.
fn staged(theirs: &[CardDefinitionId]) -> (Game, GameObjectId, GameObjectId) {
    let mut game = ready_game();
    game.battlefield.clear();
    game.players[1].hand.clear();
    let lion = game
        .put_onto_battlefield(PlayerId::One, cards::SAVANNAH_LIONS)
        .expect("cataloged");
    let boots = game
        .put_onto_battlefield(PlayerId::One, cards::LAVASPUR_BOOTS)
        .expect("cataloged");
    for definition in theirs {
        let card = game
            .build_zone(PlayerId::Two, &[*definition])
            .expect("cataloged")
            .into_iter()
            .next()
            .expect("one card");
        game.players[1].hand.push(card);
    }
    for permanent in &mut game.battlefield {
        permanent.entered_controller_turn = 0;
    }
    game.turns_started = [1, 1];
    drain_pending(&mut game);
    game.active_player = PlayerId::One;
    game.step = Step::PrecombatMain;
    game.priority = PlayerId::One;
    (game, boots, lion)
}

fn equip(game: &mut Game, boots: GameObjectId, lion: GameObjectId) {
    game.battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == boots)
        .expect("the Boots are on the battlefield")
        .attached_to = Some(lion);
}

fn permanent(game: &Game, id: GameObjectId) -> &Permanent {
    game.battlefield
        .iter()
        .find(|permanent| permanent.card.id == id)
        .expect("it is on the battlefield")
}

/// Lets both players pass until the stack and the pending triggers are quiet,
/// stopping the moment a decision is waiting for someone.
fn settle(game: &mut Game) {
    for _ in 0..24 {
        if game.observe(PlayerId::One).decision.is_some()
            || game.observe(PlayerId::Two).decision.is_some()
        {
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

/// Answers a waiting pay-or-counter decision, either paying or declining.
fn answer_ward(game: &mut Game, player: PlayerId, pay: bool) {
    let decision = game
        .observe(player)
        .decision
        .expect("the targeting player was asked about the ward cost");
    let label = if pay { "Pay the cost" } else { "Decline" };
    let option = decision
        .options
        .iter()
        .find(|option| option.label == label)
        .unwrap_or_else(|| {
            panic!(
                "{label} is offered: {:?}",
                decision
                    .options
                    .iter()
                    .map(|option| option.label.clone())
                    .collect::<Vec<_>>()
            )
        })
        .id;
    game.apply(
        player,
        Action::ChooseDecision {
            decision: decision.id,
            options: vec![option],
        },
    )
    .expect("the answer is legal");
    settle(game);
}

/// Player Two casts the Bolt in hand at `target`, with mana for it plus
/// `spare` colorless left over for a ward cost.
fn bolt(game: &mut Game, target: Target, spare: u16) {
    let card = game.players[1]
        .hand
        .iter()
        .find(|card| card.definition == cards::LIGHTNING_BOLT)
        .expect("the Bolt is in hand")
        .id;
    game.players[1].mana_pool.red = 1;
    game.players[1].mana_pool.colorless = spare;
    game.priority = PlayerId::Two;
    game.apply(
        PlayerId::Two,
        cast_action(card, vec![target], Vec::new(), 0),
    )
    .expect("the Bolt is castable");
    settle(game);
}

/// The plain half of the Equipment, so a ward test is not also the first
/// check that the Boots do anything at all.
#[test]
fn the_boots_give_power_and_haste() {
    let (mut game, boots, lion) = staged(&[]);
    let bare = game.power(permanent(&game, lion)).expect("a creature");

    equip(&mut game, boots, lion);

    assert_eq!(
        game.power(permanent(&game, lion)).expect("a creature"),
        bare + 1,
        "+1/+0",
    );
    assert!(
        game.permanent_has_executable_keyword(permanent(&game, lion), KeywordAbility::Haste),
        "and haste, which is what makes the Boots a red card",
    );
}

/// An opponent's spell pointed at the equipped creature is countered when
/// they decline the ward cost, with the mana to pay it sitting right there.
#[test]
fn a_declined_ward_counters_the_opponents_spell() {
    let (mut game, boots, lion) = staged(&[cards::LIGHTNING_BOLT]);
    equip(&mut game, boots, lion);

    bolt(&mut game, Target::Permanent(lion), 1);
    answer_ward(&mut game, PlayerId::Two, false);

    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == lion),
        "the Bolt never resolved",
    );
    assert!(game.stack.is_empty(), "and nothing is left waiting");
}

/// Paying the ward cost lets the spell through, which is the other half of
/// the same trigger.
#[test]
fn a_paid_ward_lets_the_spell_through() {
    let (mut game, boots, lion) = staged(&[cards::LIGHTNING_BOLT]);
    equip(&mut game, boots, lion);

    bolt(&mut game, Target::Permanent(lion), 1);
    answer_ward(&mut game, PlayerId::Two, true);

    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == lion),
        "three damage killed a 2/1 that had grown to 3/1",
    );
}

/// Ward reads "an opponent controls", so the equipped creature's own
/// controller is never asked.
#[test]
fn your_own_spell_does_not_trip_the_ward() {
    let (mut game, boots, lion) = staged(&[]);
    equip(&mut game, boots, lion);
    let card = game
        .build_zone(PlayerId::One, &[cards::LIGHTNING_BOLT])
        .expect("cataloged")
        .into_iter()
        .next()
        .expect("one card");
    let bolt_id = card.id;
    game.players[0].hand.push(card);
    game.players[0].mana_pool.red = 1;

    game.apply(
        PlayerId::One,
        cast_action(bolt_id, vec![Target::Permanent(lion)], Vec::new(), 0),
    )
    .expect("the Bolt is castable");
    settle(&mut game);

    assert!(
        game.observe(PlayerId::One).decision.is_none(),
        "no ward cost was asked of the Boots' own controller",
    );
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == lion),
        "and the Bolt resolved",
    );
}

/// Ward answers abilities as well as spells, which is the half a
/// "becomes the target of a spell" trigger does not reach.
#[test]
fn an_opponents_ability_trips_the_ward_too() {
    let (mut game, boots, lion) = staged(&[]);
    equip(&mut game, boots, lion);
    let sorcerer = game
        .put_onto_battlefield(PlayerId::Two, cards::PRODIGAL_SORCERER)
        .expect("cataloged");
    game.battlefield
        .iter_mut()
        .find(|permanent| permanent.card.id == sorcerer)
        .expect("it is on the battlefield")
        .entered_controller_turn = 0;
    drain_pending(&mut game);
    game.players[1].mana_pool.colorless = 1;
    game.priority = PlayerId::Two;

    let action = game
        .legal_actions(PlayerId::Two)
        .into_iter()
        .find(|action| match action {
            Action::ActivateAbility {
                source, targets, ..
            } => {
                *source == sorcerer
                    && targets
                        .iter()
                        .any(|selection| selection.targets().contains(&Target::Permanent(lion)))
            }
            _ => false,
        })
        .expect("the tapper can point at the equipped creature");
    game.apply(PlayerId::Two, action).expect("it activates");
    settle(&mut game);

    assert!(
        game.observe(PlayerId::Two).decision.is_some(),
        "the ward trigger asked the ability's controller for {{1}}",
    );
    answer_ward(&mut game, PlayerId::Two, false);
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == lion),
        "the countered ability dealt no damage",
    );
    assert!(
        game.stack.is_empty(),
        "and the countered ability left the stack",
    );
}

/// Take the Boots off and the ward goes with them: the grant is the only
/// place the creature had it.
#[test]
fn an_unequipped_creature_has_no_ward() {
    let (mut game, _boots, lion) = staged(&[cards::LIGHTNING_BOLT]);

    bolt(&mut game, Target::Permanent(lion), 0);

    assert!(
        game.observe(PlayerId::Two).decision.is_none(),
        "nothing asked for a ward cost",
    );
    assert!(
        !game
            .battlefield
            .iter()
            .any(|permanent| permanent.card.id == lion),
        "and three damage killed the bare 2/1",
    );
}

/// A player who cannot pay is never asked, and the spell is countered all
/// the same.
#[test]
fn a_ward_nobody_can_pay_counters_without_asking() {
    let (mut game, boots, lion) = staged(&[cards::LIGHTNING_BOLT]);
    equip(&mut game, boots, lion);

    bolt(&mut game, Target::Permanent(lion), 0);

    assert!(
        game.observe(PlayerId::Two).decision.is_none(),
        "there was nothing to ask about",
    );
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == lion),
        "and the Bolt was countered",
    );
}
