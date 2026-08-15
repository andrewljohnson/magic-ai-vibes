use super::*;

fn intrinsic_mana_colors(game: &Game, permanent: &Permanent) -> Vec<ManaColor> {
    let mut colors = game
        .effective_abilities(permanent)
        .into_iter()
        .filter_map(|effective| {
            let AbilityOrigin::IntrinsicBasicLand(land_type) = effective.origin else {
                return None;
            };
            Some(land_type.mana_color())
        })
        .collect::<Vec<_>>();
    colors.sort_unstable();
    colors
}

fn resolve_applied_effect_on_permanent(
    game: &mut Game,
    target: CardInstanceId,
    effect: AppliedEffectDef,
    duration: EffectDurationDef,
    stack_id: u32,
) {
    let object = spell_with_targets(
        stack_id,
        cards::LIGHTNING_BOLT,
        PlayerId::One,
        vec![Target::Permanent(target)],
        0,
    );
    game.resolve_effect_def(
        ScopedEffect::primary(EffectDef::Apply {
            recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
            effect,
            duration,
        }),
        &object,
        TriggerContext::empty(),
    );
}

#[test]
fn urborg_and_yavimaya_add_types_and_intrinsic_mana_to_every_land() {
    for sources in [
        [
            cards::URBORG_TOMB_OF_YAWGMOTH,
            cards::YAVIMAYA_CRADLE_OF_GROWTH,
        ],
        [
            cards::YAVIMAYA_CRADLE_OF_GROWTH,
            cards::URBORG_TOMB_OF_YAWGMOTH,
        ],
    ] {
        let mut game = ready_game();
        game.battlefield.extend([
            creature(10_000, sources[0], PlayerId::One),
            creature(10_001, sources[1], PlayerId::Two),
            creature(10_002, cards::ISLAND, PlayerId::One),
            creature(10_003, cards::THESPIANS_STAGE, PlayerId::One),
        ]);

        for permanent in &game.battlefield {
            assert_eq!(
                game.effective_land_types(permanent),
                if permanent.card.definition == cards::ISLAND {
                    [false, true, true, false, true]
                } else {
                    [false, false, true, false, true]
                },
            );
        }
        assert_eq!(
            intrinsic_mana_colors(&game, &game.battlefield[2]),
            vec![ManaColor::Blue, ManaColor::Black, ManaColor::Green],
        );
        assert_eq!(
            intrinsic_mana_colors(&game, &game.battlefield[3]),
            vec![ManaColor::Black, ManaColor::Green],
        );
        assert!(
            game.mana_ability_activations(&game.battlefield[3])
                .iter()
                .any(|activation| activation.color == ManaColor::Colorless),
            "adding land types does not remove Stage's printed mana ability",
        );
    }
}

#[test]
fn blood_moon_disables_urborg_and_yavimaya_regardless_of_timestamp() {
    for sources in [
        [
            cards::BLOOD_MOON,
            cards::URBORG_TOMB_OF_YAWGMOTH,
            cards::YAVIMAYA_CRADLE_OF_GROWTH,
        ],
        [
            cards::BLOOD_MOON,
            cards::YAVIMAYA_CRADLE_OF_GROWTH,
            cards::URBORG_TOMB_OF_YAWGMOTH,
        ],
        [
            cards::URBORG_TOMB_OF_YAWGMOTH,
            cards::BLOOD_MOON,
            cards::YAVIMAYA_CRADLE_OF_GROWTH,
        ],
        [
            cards::URBORG_TOMB_OF_YAWGMOTH,
            cards::YAVIMAYA_CRADLE_OF_GROWTH,
            cards::BLOOD_MOON,
        ],
        [
            cards::YAVIMAYA_CRADLE_OF_GROWTH,
            cards::BLOOD_MOON,
            cards::URBORG_TOMB_OF_YAWGMOTH,
        ],
        [
            cards::YAVIMAYA_CRADLE_OF_GROWTH,
            cards::URBORG_TOMB_OF_YAWGMOTH,
            cards::BLOOD_MOON,
        ],
    ] {
        let mut game = ready_game();
        game.battlefield.extend([
            creature(10_000, sources[0], PlayerId::One),
            creature(10_001, sources[1], PlayerId::Two),
            creature(10_002, sources[2], PlayerId::One),
            creature(10_003, cards::ISLAND, PlayerId::One),
            creature(10_004, cards::THESPIANS_STAGE, PlayerId::One),
        ]);

        let island = &game.battlefield[3];
        assert_eq!(
            game.effective_land_types(island),
            [false, true, false, false, false]
        );
        assert_eq!(intrinsic_mana_colors(&game, island), vec![ManaColor::Blue]);

        let stage = &game.battlefield[4];
        assert_eq!(
            game.effective_land_types(stage),
            [false, false, false, true, false]
        );
        assert_eq!(intrinsic_mana_colors(&game, stage), vec![ManaColor::Red]);
        assert!(game.effective_abilities(stage).iter().all(|effective| {
            !matches!(
                effective.ability.definition,
                DeclarativeAbilityDef::Activated(_)
            )
        }));

        for definition in [
            cards::URBORG_TOMB_OF_YAWGMOTH,
            cards::YAVIMAYA_CRADLE_OF_GROWTH,
        ] {
            let source = game
                .battlefield
                .iter()
                .find(|permanent| permanent.card.definition == definition)
                .unwrap();
            assert_eq!(
                game.effective_land_types(source),
                [false, false, false, true, false]
            );
            assert_eq!(intrinsic_mana_colors(&game, source), vec![ManaColor::Red]);
            assert!(game.effective_abilities(source).iter().all(|effective| {
                !matches!(
                    effective.ability.definition,
                    DeclarativeAbilityDef::Static(_)
                )
            }));
        }
    }
}

#[test]
fn stage_copying_a_basic_land_stays_basic_through_blood_moon() {
    let mut game = ready_game();
    game.turns_started[PlayerId::One.index()] = 1;
    let stage_id = CardInstanceId(10_000);
    let island_id = CardInstanceId(10_001);
    let urborg_id = CardInstanceId(10_002);
    let yavimaya_id = CardInstanceId(10_003);
    let moon_id = CardInstanceId(10_004);
    game.battlefield.extend([
        creature(stage_id.0, cards::THESPIANS_STAGE, PlayerId::One),
        creature(island_id.0, cards::ISLAND, PlayerId::Two),
        creature(urborg_id.0, cards::URBORG_TOMB_OF_YAWGMOTH, PlayerId::One),
        creature(
            yavimaya_id.0,
            cards::YAVIMAYA_CRADLE_OF_GROWTH,
            PlayerId::Two,
        ),
    ]);
    let copy_ability = activated_ability_for(&game, stage_id, 0);
    game.players[0].mana_pool.colorless = 2;
    game.apply(
        PlayerId::One,
        Action::ActivateAbility {
            source: stage_id,
            ability: copy_ability,
            targets: activated_targets(Target::Permanent(island_id)),
            cost_object: None,
            x: 0,
        },
    )
    .unwrap();
    pass_priority_pair(&mut game);

    let copied = &game.battlefield[0];
    assert!(
        game.effective_rules(copied)
            .unwrap()
            .has_supertype(CardSupertype::Basic)
    );
    assert_eq!(
        game.effective_land_types(copied),
        [false, true, true, false, true],
    );
    assert_eq!(
        intrinsic_mana_colors(&game, copied),
        vec![ManaColor::Blue, ManaColor::Black, ManaColor::Green],
    );
    assert_eq!(activated_ability_for(&game, stage_id, 0), copy_ability);

    game.battlefield
        .push(creature(moon_id.0, cards::BLOOD_MOON, PlayerId::Two));
    let copied = &game.battlefield[0];
    assert_eq!(
        game.effective_land_types(copied),
        [false, true, false, false, false],
    );
    assert_eq!(intrinsic_mana_colors(&game, copied), vec![ManaColor::Blue]);
    assert_eq!(activated_ability_for(&game, stage_id, 0), copy_ability);

    game.destroy_permanent(moon_id);
    let copied = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == stage_id)
        .unwrap();
    assert_eq!(
        game.effective_land_types(copied),
        [false, true, true, false, true],
    );
}

#[test]
fn stage_activation_already_on_the_stack_resolves_through_blood_moon() {
    let mut game = ready_game();
    game.turns_started[PlayerId::One.index()] = 1;
    let stage_id = CardInstanceId(10_000);
    let island_id = CardInstanceId(10_001);
    game.battlefield.extend([
        creature(stage_id.0, cards::THESPIANS_STAGE, PlayerId::One),
        creature(island_id.0, cards::ISLAND, PlayerId::Two),
    ]);
    let copy_ability = activated_ability_for(&game, stage_id, 0);
    game.players[0].mana_pool.colorless = 2;
    game.apply(
        PlayerId::One,
        Action::ActivateAbility {
            source: stage_id,
            ability: copy_ability,
            targets: activated_targets(Target::Permanent(island_id)),
            cost_object: None,
            x: 0,
        },
    )
    .unwrap();
    game.battlefield
        .push(creature(10_002, cards::BLOOD_MOON, PlayerId::Two));
    assert_eq!(
        game.effective_land_types(&game.battlefield[0]),
        [false, false, false, true, false],
    );

    pass_priority_pair(&mut game);
    let copied = &game.battlefield[0];
    assert!(copied.tapped);
    assert!(
        game.effective_rules(copied)
            .unwrap()
            .has_supertype(CardSupertype::Basic)
    );
    assert_eq!(
        game.effective_land_types(copied),
        [false, true, false, false, false],
    );
    assert_eq!(intrinsic_mana_colors(&game, copied), vec![ManaColor::Blue]);
    assert_eq!(activated_ability_for(&game, stage_id, 0), copy_ability);
}

#[test]
fn stage_copying_a_nonbasic_land_is_masked_but_persists_through_blood_moon() {
    let mut game = ready_game();
    game.turns_started[PlayerId::One.index()] = 1;
    let stage_id = CardInstanceId(10_000);
    let yavimaya_id = CardInstanceId(10_001);
    let moon_id = CardInstanceId(10_002);
    game.battlefield.extend([
        creature(stage_id.0, cards::THESPIANS_STAGE, PlayerId::One),
        creature(
            yavimaya_id.0,
            cards::YAVIMAYA_CRADLE_OF_GROWTH,
            PlayerId::Two,
        ),
    ]);
    let copy_ability = activated_ability_for(&game, stage_id, 0);
    game.players[0].mana_pool.colorless = 2;
    game.apply(
        PlayerId::One,
        Action::ActivateAbility {
            source: stage_id,
            ability: copy_ability,
            targets: activated_targets(Target::Permanent(yavimaya_id)),
            cost_object: None,
            x: 0,
        },
    )
    .unwrap();

    game.battlefield
        .push(creature(moon_id.0, cards::BLOOD_MOON, PlayerId::Two));
    pass_priority_pair(&mut game);

    let copied = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == stage_id)
        .unwrap();
    assert_eq!(
        copied.copy_effect.as_ref().map(|copy| copy.base),
        Some((cards::YAVIMAYA_CRADLE_OF_GROWTH, CardPartId::PRIMARY)),
        "the already-stacked activation resolves even though Moon masks its source",
    );
    assert_eq!(
        game.effective_land_types(copied),
        [false, false, false, true, false],
    );
    assert_eq!(intrinsic_mana_colors(&game, copied), vec![ManaColor::Red]);
    assert_eq!(game.effective_abilities(copied).len(), 1);

    game.destroy_permanent(moon_id);
    let copied = game
        .battlefield
        .iter()
        .find(|permanent| permanent.card.id == stage_id)
        .unwrap();
    assert_eq!(
        game.effective_permanent_name(copied),
        Some("Yavimaya, Cradle of Growth"),
    );
    assert!(
        game.effective_rules(copied)
            .unwrap()
            .has_supertype(CardSupertype::Legendary),
    );
    assert!(
        !game
            .effective_rules(copied)
            .unwrap()
            .has_supertype(CardSupertype::Basic),
    );
    assert_eq!(
        game.effective_land_types(copied),
        [false, false, false, false, true],
    );
    assert_eq!(intrinsic_mana_colors(&game, copied), vec![ManaColor::Green]);
    assert_eq!(activated_ability_for(&game, stage_id, 0), copy_ability);
}

#[test]
fn blood_moon_preserves_external_grants_but_later_ability_removal_removes_them() {
    static GRANTED_FLYING: AbilityDef = abilities::flying();

    let mut game = ready_game();
    let stage_id = CardInstanceId(10_000);
    game.battlefield.extend([
        creature(stage_id.0, cards::THESPIANS_STAGE, PlayerId::One),
        creature(10_001, cards::BLOOD_MOON, PlayerId::Two),
    ]);
    resolve_applied_effect_on_permanent(
        &mut game,
        stage_id,
        AppliedEffectDef::GrantAbility(&GRANTED_FLYING),
        EffectDurationDef::UntilEndOfTurn,
        20_000,
    );

    let stage = &game.battlefield[0];
    assert!(game.has_flying(stage));
    assert_eq!(intrinsic_mana_colors(&game, stage), vec![ManaColor::Red]);
    assert_eq!(
        game.effective_abilities(stage).len(),
        2,
        "Blood Moon removes Stage's rules abilities, not independently granted abilities",
    );

    resolve_applied_effect_on_permanent(
        &mut game,
        stage_id,
        AppliedEffectDef::RemoveAbilities(AbilityPredicateDef::Any),
        EffectDurationDef::UntilEndOfTurn,
        20_001,
    );
    assert!(game.effective_abilities(&game.battlefield[0]).is_empty());
    assert!(
        game.mana_ability_activations(&game.battlefield[0])
            .is_empty()
    );

    game.finish_cleanup();
    let stage = &game.battlefield[0];
    assert!(!game.has_flying(stage));
    assert_eq!(intrinsic_mana_colors(&game, stage), vec![ManaColor::Red]);
}

#[test]
fn resolved_ability_additions_and_removals_are_ordered_and_expire() {
    static GRANTED_ACTIVATED: AbilityDef = AbilityDef::activated(
        "Draw a card.",
        &[],
        EffectDef::DrawCards {
            recipient: EffectRecipientDef::Controller,
            amount: ValueDef::Constant(1),
        },
    );
    static GRANTED_FLYING: AbilityDef = abilities::flying();

    let mut game = ready_game();
    let target = CardInstanceId(10_000);
    game.battlefield
        .push(creature(target.0, cards::SERRA_ANGEL, PlayerId::One));
    resolve_applied_effect_on_permanent(
        &mut game,
        target,
        AppliedEffectDef::RemoveAbilities(AbilityPredicateDef::Any),
        EffectDurationDef::UntilEndOfTurn,
        20_000,
    );
    assert!(game.effective_abilities(&game.battlefield[0]).is_empty());

    resolve_applied_effect_on_permanent(
        &mut game,
        target,
        AppliedEffectDef::GrantAbility(&GRANTED_ACTIVATED),
        EffectDurationDef::UntilEndOfTurn,
        20_001,
    );
    assert!(
        game.effective_abilities(&game.battlefield[0])
            .iter()
            .any(|effective| matches!(
                effective.ability.definition,
                DeclarativeAbilityDef::Activated(_)
            ))
    );
    game.finish_cleanup();
    assert!(game.has_flying(&game.battlefield[0]));

    resolve_applied_effect_on_permanent(
        &mut game,
        target,
        AppliedEffectDef::GrantAbility(&GRANTED_FLYING),
        EffectDurationDef::UntilEndOfTurn,
        20_002,
    );
    resolve_applied_effect_on_permanent(
        &mut game,
        target,
        AppliedEffectDef::RemoveAbilities(AbilityPredicateDef::Keyword(KeywordAbility::Flying)),
        EffectDurationDef::UntilEndOfTurn,
        20_003,
    );
    assert!(!game.has_flying(&game.battlefield[0]));
    assert!(
        game.permanent_has_executable_keyword(&game.battlefield[0], KeywordAbility::Vigilance),
        "selective removal leaves unrelated abilities alone",
    );

    game.finish_cleanup();
    assert!(game.has_flying(&game.battlefield[0]));
    assert!(
        game.effective_abilities(&game.battlefield[0])
            .iter()
            .all(|effective| !matches!(
                effective.ability.definition,
                DeclarativeAbilityDef::Activated(_)
            ))
    );
}

#[test]
fn resolved_keyword_changes_are_visible_to_object_predicates() {
    static GRANTED_FLYING: AbilityDef = abilities::flying();

    let mut game = ready_game();
    let target = CardInstanceId(10_000);
    game.battlefield
        .push(creature(target.0, cards::SAVANNAH_LIONS, PlayerId::One));
    let has_flying = |game: &Game| {
        let event = game.trigger_event_object(&game.battlefield[0]);
        game.trigger_object_matches(
            ObjectPredicateDef::HasKeyword(KeywordAbility::Flying),
            &event,
            target,
            false,
        )
    };
    assert!(!has_flying(&game));

    resolve_applied_effect_on_permanent(
        &mut game,
        target,
        AppliedEffectDef::GrantAbility(&GRANTED_FLYING),
        EffectDurationDef::UntilEndOfTurn,
        20_000,
    );
    assert!(has_flying(&game));
    resolve_applied_effect_on_permanent(
        &mut game,
        target,
        AppliedEffectDef::RemoveAbilities(AbilityPredicateDef::Keyword(KeywordAbility::Flying)),
        EffectDurationDef::UntilEndOfTurn,
        20_001,
    );
    assert!(!has_flying(&game));
}

#[test]
fn blood_moon_strips_printed_keywords_from_object_predicates() {
    let definition_id = CardDefinitionId(10_090);
    let mut definition = CardDefinition::new(
        definition_id,
        "Flying Gate",
        CardSet::Magic2014,
        false,
        CardBehavior::Unsupported,
    );
    definition.rules = CardRules::new_creature_without_mana_cost(&["Gate", "Bird"], 1, 1)
        .with_type(CardType::Land)
        .with_ability(abilities::flying());
    synchronize_single_part_definition(&mut definition);

    let mut game = ready_game();
    let blood_moon = game.catalog.get(cards::BLOOD_MOON).unwrap().clone();
    game.catalog = CardCatalog::new([blood_moon, definition]).unwrap();
    game.battlefield.extend([
        creature(10_000, cards::BLOOD_MOON, PlayerId::One),
        creature(10_001, definition_id, PlayerId::Two),
    ]);
    let event = game.trigger_event_object(&game.battlefield[1]);
    assert!(!game.trigger_object_matches(
        ObjectPredicateDef::HasKeyword(KeywordAbility::Flying),
        &event,
        game.battlefield[1].card.id,
        false,
    ));
}

#[test]
fn resolved_ability_removal_suppresses_custom_behavior_until_it_expires() {
    let mut game = ready_game();
    let ape = CardInstanceId(10_001);
    game.battlefield.extend([
        creature(10_000, cards::TAIGA, PlayerId::One),
        creature(ape.0, cards::KIRD_APE, PlayerId::One),
    ]);
    assert_eq!(game.power(&game.battlefield[1]), Some(2));
    assert_eq!(game.toughness(&game.battlefield[1]), Some(3));

    resolve_applied_effect_on_permanent(
        &mut game,
        ape,
        AppliedEffectDef::RemoveAbilities(AbilityPredicateDef::Any),
        EffectDurationDef::UntilEndOfTurn,
        20_000,
    );
    assert_eq!(game.effective_behavior(&game.battlefield[1]), None);
    assert_eq!(game.power(&game.battlefield[1]), Some(1));
    assert_eq!(game.toughness(&game.battlefield[1]), Some(1));

    game.finish_cleanup();
    assert_eq!(game.power(&game.battlefield[1]), Some(2));
    assert_eq!(game.toughness(&game.battlefield[1]), Some(3));
}

#[test]
fn static_ability_additions_and_removals_follow_source_timestamps() {
    static FLYING: AbilityDef = abilities::flying();
    static GRANT: [AbilityDef; 1] = [AbilityDef::static_ability(
        "Creatures have flying.",
        EffectDef::Apply {
            recipient: EffectRecipientDef::MatchingObjects {
                object: ObjectPredicateDef::HasType(CardType::Creature),
                zones: &[ZoneKind::Battlefield],
                controller: PlayerRelation::Any,
            },
            effect: AppliedEffectDef::GrantAbility(&FLYING),
            duration: EffectDurationDef::WhileSourceRemainsInZone,
        },
    )];
    static REMOVE: [AbilityDef; 1] = [AbilityDef::static_ability(
        "Creatures lose all abilities.",
        EffectDef::Apply {
            recipient: EffectRecipientDef::MatchingObjects {
                object: ObjectPredicateDef::HasType(CardType::Creature),
                zones: &[ZoneKind::Battlefield],
                controller: PlayerRelation::Any,
            },
            effect: AppliedEffectDef::RemoveAbilities(AbilityPredicateDef::Any),
            duration: EffectDurationDef::WhileSourceRemainsInZone,
        },
    )];
    let grant_id = CardDefinitionId(10_090);
    let remove_id = CardDefinitionId(10_091);
    let mut grant = CardDefinition::new(
        grant_id,
        "Static ability grant test",
        CardSet::Magic2014,
        false,
        CardBehavior::Unsupported,
    );
    grant.rules = CardRules::new_enchantment(ManaCost::new(0, 0)).with_abilities(&GRANT);
    synchronize_single_part_definition(&mut grant);
    let mut remove = CardDefinition::new(
        remove_id,
        "Static ability removal test",
        CardSet::Magic2014,
        false,
        CardBehavior::Unsupported,
    );
    remove.rules = CardRules::new_enchantment(ManaCost::new(0, 0)).with_abilities(&REMOVE);
    synchronize_single_part_definition(&mut remove);

    let mut game = ready_game();
    let mut definitions = game
        .catalog
        .definitions()
        .into_iter()
        .cloned()
        .collect::<Vec<_>>();
    definitions.extend([grant.clone(), remove.clone()]);
    game.catalog = CardCatalog::new(definitions).unwrap();
    game.battlefield.extend([
        creature(10_000, grant_id, PlayerId::One),
        creature(10_001, remove_id, PlayerId::Two),
        creature(10_002, cards::SAVANNAH_LIONS, PlayerId::One),
    ]);
    assert!(!game.has_flying(&game.battlefield[2]));
    game.destroy_permanent(CardInstanceId(10_001));
    assert!(game.has_flying(&game.battlefield[1]));

    let mut reverse = ready_game();
    let mut definitions = reverse
        .catalog
        .definitions()
        .into_iter()
        .cloned()
        .collect::<Vec<_>>();
    definitions.extend([grant, remove]);
    reverse.catalog = CardCatalog::new(definitions).unwrap();
    reverse.battlefield.extend([
        creature(10_000, remove_id, PlayerId::Two),
        creature(10_001, grant_id, PlayerId::One),
        creature(10_002, cards::SAVANNAH_LIONS, PlayerId::One),
    ]);
    assert!(reverse.has_flying(&reverse.battlefield[2]));
}

/// A keyword a live static effect grants or removes reaches object predicates,
/// so target legality and the combat rules read one ability set rather than
/// two. Lord of Atlantis hands out islandwalk and Gravity Sphere takes flying
/// away; both answers have to match `permanent_has_executable_keyword`.
#[test]
fn static_keyword_grants_and_removals_reach_object_predicates() {
    let matches = |game: &Game, index: usize, keyword: KeywordAbility| {
        let permanent = &game.battlefield[index];
        let predicate = game.trigger_object_matches(
            ObjectPredicateDef::HasKeyword(keyword),
            &game.trigger_event_object(permanent),
            permanent.card.id,
            false,
        );
        assert_eq!(
            predicate,
            game.permanent_has_executable_keyword(permanent, keyword),
            "the predicate and the rules query disagree about {keyword:?}"
        );
        predicate
    };

    let islandwalk = KeywordAbility::Landwalk(BasicLandType::Island);
    let mut granted = ready_game();
    granted
        .battlefield
        .push(creature(10_000, cards::VODALIAN_MAGE, PlayerId::One));
    assert!(!matches(&granted, 0, islandwalk));
    granted
        .battlefield
        .push(creature(10_001, cards::LORD_OF_ATLANTIS, PlayerId::One));
    assert!(matches(&granted, 0, islandwalk));

    let mut removed = ready_game();
    removed
        .battlefield
        .push(creature(10_000, cards::SERRA_ANGEL, PlayerId::One));
    assert!(matches(&removed, 0, KeywordAbility::Flying));
    removed
        .battlefield
        .push(creature(10_001, cards::GRAVITY_SPHERE, PlayerId::Two));
    assert!(!matches(&removed, 0, KeywordAbility::Flying));
}

fn static_enchantment(
    id: CardDefinitionId,
    name: &'static str,
    abilities: &'static [AbilityDef],
) -> CardDefinition {
    let mut definition = CardDefinition::new(
        id,
        name,
        CardSet::Magic2014,
        false,
        CardBehavior::Unsupported,
    );
    definition.rules = CardRules::new_enchantment(ManaCost::new(0, 0)).with_abilities(abilities);
    synchronize_single_part_definition(&mut definition);
    definition
}

/// A board carrying "creatures have flying" plus whatever else is handed in,
/// with a vanilla 2/1 last so tests can read it back.
fn game_granting_flying(extra: Vec<CardDefinition>) -> Game {
    static FLYING: AbilityDef = abilities::flying();
    static GRANT_FLYING: [AbilityDef; 1] = [AbilityDef::static_ability(
        "Creatures have flying.",
        EffectDef::Apply {
            recipient: EffectRecipientDef::MatchingObjects {
                object: ObjectPredicateDef::HasType(CardType::Creature),
                zones: &[ZoneKind::Battlefield],
                controller: PlayerRelation::Any,
            },
            effect: AppliedEffectDef::GrantAbility(&FLYING),
            duration: EffectDurationDef::WhileSourceRemainsInZone,
        },
    )];

    let mut game = ready_game();
    let mut definitions = game
        .catalog
        .definitions()
        .into_iter()
        .cloned()
        .collect::<Vec<_>>();
    let grant = static_enchantment(
        CardDefinitionId(10_090),
        "Static flying grant test",
        &GRANT_FLYING,
    );
    let ids = std::iter::once(grant.id)
        .chain(extra.iter().map(|definition| definition.id))
        .collect::<Vec<_>>();
    definitions.push(grant);
    definitions.extend(extra);
    game.catalog = CardCatalog::new(definitions).unwrap();
    for (index, id) in ids.into_iter().enumerate() {
        let object = 10_000 + u32::try_from(index).unwrap();
        game.battlefield.push(creature(object, id, PlayerId::One));
    }
    game.battlefield
        .push(creature(10_100, cards::SAVANNAH_LIONS, PlayerId::One));
    game
}

static FLIERS: ObjectPredicateDef = ObjectPredicateDef::All(&[
    ObjectPredicateDef::HasType(CardType::Creature),
    ObjectPredicateDef::HasKeyword(KeywordAbility::Flying),
]);

fn lions_have_granted_flying(game: &Game) {
    let lions = game.battlefield.last().unwrap();
    assert!(
        game.trigger_object_matches(
            ObjectPredicateDef::HasKeyword(KeywordAbility::Flying),
            &game.trigger_event_object(lions),
            lions.card.id,
            false,
        ),
        "read from outside the layer-6 walk the Lions have the granted flying"
    );
}

/// Where the answer still stratifies, pinned so it cannot drift silently.
///
/// Gathering a permanent's abilities is the one query that cannot read itself,
/// so a static ability that grants or removes abilities picks its recipients
/// from the layer below: it sees printed, copied, and resolved keywords, not
/// ones another static ability hands out. Closing this needs the CR 613.8
/// dependency evaluator.
#[test]
fn a_static_ability_grant_picks_recipients_from_the_layer_below_itself() {
    static TRAMPLE: AbilityDef = abilities::trample();
    static GRANT_TRAMPLE: [AbilityDef; 1] = [AbilityDef::static_ability(
        "Creatures with flying have trample.",
        EffectDef::Apply {
            recipient: EffectRecipientDef::MatchingObjects {
                object: FLIERS,
                zones: &[ZoneKind::Battlefield],
                controller: PlayerRelation::Any,
            },
            effect: AppliedEffectDef::GrantAbility(&TRAMPLE),
            duration: EffectDurationDef::WhileSourceRemainsInZone,
        },
    )];

    let game = game_granting_flying(vec![static_enchantment(
        CardDefinitionId(10_091),
        "Static trample grant test",
        &GRANT_TRAMPLE,
    )]);
    lions_have_granted_flying(&game);
    assert!(
        !game.has_trample(game.battlefield.last().unwrap()),
        "but a grant keyed on flying picks its recipients from the layer below it"
    );
}

/// The stratification is confined to the ability layer. A static power and
/// toughness effect keyed on a keyword sits outside that walk, so One-Eyed
/// Scarecrow's shape reads flying another static effect granted.
#[test]
fn a_static_power_effect_keyed_on_a_keyword_sees_a_static_grant() {
    static SHRINK: [AbilityDef; 1] = [AbilityDef::static_ability(
        "Creatures with flying get -1/-0.",
        EffectDef::Apply {
            recipient: EffectRecipientDef::MatchingObjects {
                object: FLIERS,
                zones: &[ZoneKind::Battlefield],
                controller: PlayerRelation::Any,
            },
            effect: AppliedEffectDef::ModifyPowerToughness {
                power: ValueDef::Constant(-1),
                toughness: ValueDef::Constant(0),
            },
            duration: EffectDurationDef::WhileSourceRemainsInZone,
        },
    )];

    let game = game_granting_flying(vec![static_enchantment(
        CardDefinitionId(10_092),
        "Static flier penalty test",
        &SHRINK,
    )]);
    lions_have_granted_flying(&game);
    assert_eq!(
        game.power(game.battlefield.last().unwrap()),
        Some(1),
        "the penalty applies to a creature only a static grant made a flier"
    );
}
