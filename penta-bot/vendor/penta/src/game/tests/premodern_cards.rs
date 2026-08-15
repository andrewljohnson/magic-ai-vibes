use super::*;
use crate::ImplementationStatus;

#[test]
fn mogg_fanatic_and_seal_of_fire_pay_sacrifice_before_dealing_damage() {
    for (definition, amount) in [(cards::MOGG_FANATIC, 1), (cards::SEAL_OF_FIRE, 2)] {
        let mut game = ready_game();
        let source = CardInstanceId(10_000);
        game.battlefield
            .push(creature(source.0, definition, PlayerId::One));
        let activation = Action::ActivateAbility {
            source,
            ability: primary_ability(definition),
            targets: activated_targets(Target::Player(PlayerId::Two)),
            cost_object: None,
            x: 0,
        };

        assert!(game.legal_actions(PlayerId::One).contains(&activation));
        game.apply(PlayerId::One, activation).unwrap();
        assert!(
            game.battlefield.is_empty(),
            "the source is sacrificed as a cost"
        );

        pass_priority_pair(&mut game);
        assert_eq!(game.players[PlayerId::Two.index()].life, 20 - amount);
    }
}

#[test]
fn incinerating_jackal_pup_deals_the_same_damage_back() {
    let mut game = ready_game();
    let pup = CardInstanceId(10_000);
    game.battlefield
        .push(creature(pup.0, cards::JACKAL_PUP, PlayerId::One));
    let incinerate = card(10_001, cards::INCINERATE, PlayerId::Two);
    game.players[PlayerId::Two.index()]
        .hand
        .push(incinerate.clone());
    game.players[PlayerId::Two.index()].mana_pool.colorless = 1;
    game.players[PlayerId::Two.index()].mana_pool.red = 1;
    game.priority = PlayerId::Two;

    game.apply(
        PlayerId::Two,
        cast_action(incinerate.id, vec![Target::Permanent(pup)], Vec::new(), 0),
    )
    .unwrap();
    pass_priority_pair(&mut game);
    assert!(game.battlefield.is_empty(), "three damage kills the Pup");

    pass_priority_pair(&mut game);
    assert_eq!(game.players[PlayerId::One.index()].life, 17);
}

#[test]
fn naturalize_destroys_an_artifact_or_enchantment() {
    for target_definition in [cards::BLACK_VISE, cards::SEAL_OF_FIRE] {
        let mut game = ready_game();
        let target = CardInstanceId(10_000);
        game.battlefield
            .push(creature(target.0, target_definition, PlayerId::Two));
        let naturalize = card(10_001, cards::NATURALIZE, PlayerId::One);
        game.players[PlayerId::One.index()]
            .hand
            .push(naturalize.clone());
        game.players[PlayerId::One.index()].mana_pool.colorless = 1;
        game.players[PlayerId::One.index()].mana_pool.green = 1;

        game.apply(
            PlayerId::One,
            cast_action(
                naturalize.id,
                vec![Target::Permanent(target)],
                Vec::new(),
                0,
            ),
        )
        .unwrap();
        pass_priority_pair(&mut game);
        assert!(game.battlefield.is_empty());
    }
}

#[test]
fn hydroblast_and_pyroblast_are_complete_opposite_color_modal_answers() {
    let game = ready_game();
    for (definition, color) in [
        (cards::HYDROBLAST, ManaColor::Red),
        (cards::PYROBLAST, ManaColor::Blue),
    ] {
        let rules = game.catalog.get(definition).unwrap().rules;
        assert_eq!(
            rules.implementation_status(),
            ImplementationStatus::Complete
        );
        let DeclarativeAbilityDef::Spell(wrapper) = rules.ability_clauses()[0].definition else {
            panic!("the blast has a spell wrapper");
        };
        let modes = wrapper.modal().expect("the blast is modal").modes;
        assert_eq!(modes.len(), 2);
        let DeclarativeAbilityDef::Spell(counter_mode) = modes[0].definition else {
            panic!("the first mode is a spell clause");
        };
        assert_eq!(
            counter_mode.targets(),
            &[AbilityTargetDef::exactly_one_spell(
                ObjectPredicateDef::Color(color)
            )]
        );
        assert!(matches!(
            modes[1].effect.definition,
            EffectDef::Destroy {
                can_regenerate: true,
                ..
            }
        ));
    }
}

#[test]
fn lotus_petal_sacrifices_for_one_mana_of_the_chosen_color() {
    let mut game = ready_game();
    let petal = creature(10_000, cards::LOTUS_PETAL, PlayerId::One);
    let source = petal.card.id;
    game.battlefield.push(petal);
    let action = Action::ActivateManaAbility {
        source,
        ability: mana_ability_for(&game, source, ManaColor::Blue),
        color: ManaColor::Blue,
    };

    assert!(game.legal_actions(PlayerId::One).contains(&action));
    game.apply(PlayerId::One, action).unwrap();

    assert_eq!(game.players[PlayerId::One.index()].mana_pool.blue, 1);
    assert!(game.battlefield.is_empty());
    assert_eq!(
        game.players[PlayerId::One.index()].graveyard[0].definition,
        cards::LOTUS_PETAL
    );
}

#[test]
fn volcanic_hammer_hits_any_target_and_vindicate_hits_a_land() {
    let mut game = ready_game();
    let hammer = card(10_000, cards::VOLCANIC_HAMMER, PlayerId::One);
    game.players[PlayerId::One.index()]
        .hand
        .push(hammer.clone());
    game.players[PlayerId::One.index()].mana_pool.colorless = 1;
    game.players[PlayerId::One.index()].mana_pool.red = 1;
    game.apply(
        PlayerId::One,
        cast_action(
            hammer.id,
            vec![Target::Player(PlayerId::Two)],
            Vec::new(),
            0,
        ),
    )
    .unwrap();
    pass_priority_pair(&mut game);
    assert_eq!(game.players[PlayerId::Two.index()].life, 17);

    let land = CardInstanceId(10_001);
    game.battlefield
        .push(creature(land.0, cards::MOUNTAIN, PlayerId::Two));
    let vindicate = card(10_002, cards::VINDICATE, PlayerId::One);
    game.players[PlayerId::One.index()]
        .hand
        .push(vindicate.clone());
    game.players[PlayerId::One.index()].mana_pool.colorless = 1;
    game.players[PlayerId::One.index()].mana_pool.white = 1;
    game.players[PlayerId::One.index()].mana_pool.black = 1;
    game.priority = PlayerId::One;
    game.apply(
        PlayerId::One,
        cast_action(vindicate.id, vec![Target::Permanent(land)], Vec::new(), 0),
    )
    .unwrap();
    pass_priority_pair(&mut game);
    assert!(game.battlefield.is_empty());
}

#[test]
fn seal_of_cleansing_and_monk_realist_destroy_enchantments_at_their_printed_timing() {
    let mut game = ready_game();
    let seal = creature(10_000, cards::SEAL_OF_CLEANSING, PlayerId::One);
    let seal_id = seal.card.id;
    let artifact = creature(10_001, cards::BLACK_VISE, PlayerId::Two);
    let artifact_id = artifact.card.id;
    game.battlefield.extend([seal, artifact]);
    game.apply(
        PlayerId::One,
        Action::ActivateAbility {
            source: seal_id,
            ability: primary_ability(cards::SEAL_OF_CLEANSING),
            targets: activated_targets(Target::Permanent(artifact_id)),
            cost_object: None,
            x: 0,
        },
    )
    .unwrap();
    assert_eq!(game.battlefield.len(), 1, "the Seal is already sacrificed");
    pass_priority_pair(&mut game);
    assert!(game.battlefield.is_empty());

    let target = creature(10_002, cards::SEAL_OF_FIRE, PlayerId::Two);
    let target_id = target.card.id;
    game.battlefield.push(target);
    let monk = card(10_003, cards::MONK_REALIST, PlayerId::One);
    game.players[PlayerId::One.index()].hand.push(monk.clone());
    game.players[PlayerId::One.index()].mana_pool.colorless = 1;
    game.players[PlayerId::One.index()].mana_pool.white = 1;
    game.priority = PlayerId::One;
    game.apply(
        PlayerId::One,
        cast_action(monk.id, Vec::new(), Vec::new(), 0),
    )
    .unwrap();
    pass_priority_pair(&mut game);

    let decision = game
        .observe(PlayerId::One)
        .decision
        .expect("Monk Realist's entry trigger asks for an enchantment target");
    let target_option = decision
        .options
        .iter()
        .find(|option| option.card.is_some_and(|(id, _)| id == target_id))
        .expect("Seal of Fire is offered")
        .id;
    game.apply(
        PlayerId::One,
        Action::ChooseDecision {
            decision: decision.id,
            options: vec![target_option],
        },
    )
    .unwrap();
    pass_priority_pair(&mut game);
    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != target_id)
    );
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.definition == cards::MONK_REALIST)
    );
}

#[test]
fn annul_only_targets_artifact_or_enchantment_spells() {
    for (target_definition, expected) in [
        (cards::BLACK_VISE, true),
        (cards::SEAL_OF_FIRE, true),
        (cards::VOLCANIC_HAMMER, false),
    ] {
        let mut game = ready_game();
        let target = spell(10_000, target_definition, PlayerId::Two, 0);
        let target_id = target.card.id;
        game.stack.push(target);
        let annul = card(10_001, cards::ANNUL, PlayerId::One);
        game.players[PlayerId::One.index()].hand.push(annul.clone());
        game.players[PlayerId::One.index()].mana_pool.blue = 1;
        let action = cast_action(annul.id, vec![Target::Spell(target_id)], Vec::new(), 0);

        assert_eq!(
            game.legal_actions(PlayerId::One).contains(&action),
            expected
        );
    }
}

#[test]
fn mana_leak_counters_without_three_mana_and_offers_payment_with_it() {
    let run = |pay: bool| {
        let mut game = ready_game();
        let incinerate = card(10_000, cards::INCINERATE, PlayerId::Two);
        game.players[PlayerId::Two.index()]
            .hand
            .push(incinerate.clone());
        game.players[PlayerId::Two.index()].mana_pool.colorless = if pay { 4 } else { 1 };
        game.players[PlayerId::Two.index()].mana_pool.red = 1;
        game.priority = PlayerId::Two;
        game.apply(
            PlayerId::Two,
            cast_action(
                incinerate.id,
                vec![Target::Player(PlayerId::One)],
                Vec::new(),
                0,
            ),
        )
        .unwrap();
        let incinerate_on_stack = game.stack.last().expect("Incinerate is on the stack").id;
        game.apply(PlayerId::Two, Action::PassPriority).unwrap();

        let leak = card(10_001, cards::MANA_LEAK, PlayerId::One);
        game.players[PlayerId::One.index()].hand.push(leak.clone());
        game.players[PlayerId::One.index()].mana_pool.colorless = 1;
        game.players[PlayerId::One.index()].mana_pool.blue = 1;
        game.apply(
            PlayerId::One,
            cast_action(
                leak.id,
                vec![Target::Spell(incinerate_on_stack)],
                Vec::new(),
                0,
            ),
        )
        .unwrap();
        pass_priority_pair(&mut game);

        if pay {
            let decision = game
                .observe(PlayerId::Two)
                .decision
                .expect("the spell's controller can pay three");
            let payment = decision
                .options
                .iter()
                .find(|option| option.label == "Pay the cost")
                .expect("payment is offered")
                .id;
            game.apply(
                PlayerId::Two,
                Action::ChooseDecision {
                    decision: decision.id,
                    options: vec![payment],
                },
            )
            .unwrap();
            pass_priority_pair(&mut game);
        }

        game.players[PlayerId::One.index()].life
    };

    assert_eq!(run(false), 20, "Incinerate was countered");
    assert_eq!(run(true), 17, "paying three preserved Incinerate");
}

#[test]
fn fact_or_fiction_uses_an_opponents_split_and_its_controllers_choice() {
    let mut game = ready_game();
    game.players[PlayerId::One.index()].library.clear();
    stack_library(
        &mut game,
        &[
            (10_001, cards::SERRA_ANGEL),
            (10_002, cards::SAVANNAH_LIONS),
            (10_003, cards::LIGHTNING_BOLT),
            (10_004, cards::PLAINS),
            (10_005, cards::MOUNTAIN),
            (10_006, cards::SWAMP),
        ],
    );
    let fact = card(10_000, cards::FACT_OR_FICTION, PlayerId::One);
    game.players[PlayerId::One.index()].hand.push(fact.clone());
    game.players[PlayerId::One.index()].mana_pool.colorless = 3;
    game.players[PlayerId::One.index()].mana_pool.blue = 1;
    game.apply(
        PlayerId::One,
        cast_action(fact.id, Vec::new(), Vec::new(), 0),
    )
    .unwrap();
    pass_priority_pair(&mut game);

    let split = game
        .observe(PlayerId::Two)
        .decision
        .expect("the opponent separates the five revealed cards");
    assert_eq!(split.options.len(), 5);
    let angel = split
        .options
        .iter()
        .find(|option| {
            option
                .card
                .is_some_and(|(id, _)| id == CardInstanceId(10_001))
        })
        .expect("Serra Angel was revealed")
        .id;
    game.apply(
        PlayerId::Two,
        Action::ChooseDecision {
            decision: split.id,
            options: vec![angel],
        },
    )
    .unwrap();

    let choice = game
        .observe(PlayerId::One)
        .decision
        .expect("Fact or Fiction's controller chooses a pile");
    let four_cards = choice
        .options
        .iter()
        .find(|option| option.label.contains("Savannah Lions"))
        .expect("the other four cards form the second pile")
        .id;
    game.apply(
        PlayerId::One,
        Action::ChooseDecision {
            decision: choice.id,
            options: vec![four_cards],
        },
    )
    .unwrap();

    assert_eq!(
        game.players[PlayerId::One.index()]
            .hand
            .iter()
            .map(|card| card.definition)
            .collect::<Vec<_>>(),
        vec![
            cards::SAVANNAH_LIONS,
            cards::LIGHTNING_BOLT,
            cards::PLAINS,
            cards::MOUNTAIN,
        ]
    );
    assert!(
        game.players[PlayerId::One.index()]
            .graveyard
            .iter()
            .any(|card| card.definition == cards::SERRA_ANGEL)
    );
    assert_eq!(
        game.players[PlayerId::One.index()].library[0].definition,
        cards::SWAMP
    );
}
