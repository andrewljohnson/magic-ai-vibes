use super::{CardRecord, PrintingRecord};
use crate::card::{
    AbilityCostDef, AbilityDef, AbilityTargetDef, AbilityTargetPredicate, ActivationTimingDef,
    AddManaEffectDef, AnimationDef, AppliedEffectDef, BasicLandType,
    BattlefieldEntryModificationDef, CardArt, CardBehavior, CardRules, CardSet, CardType,
    ComparisonDef, CounterKind, DiscardSelectionDef, EffectDef, EffectDurationDef,
    EffectRecipientDef, LikelihoodDef, ManaColor, ObjectPredicateDef, ObjectQueryDef,
    PlayerRelation, ReplacementEffectDef, TriggerConditionDef, TriggerEventDef, TurnStepDef,
    ValueDef, ZoneKind, abilities, cards,
};
use crate::ids::TargetIndex;
use crate::mana_cost;

static DEFENDER_CONTROLS_AN_ISLAND: ObjectQueryDef = ObjectQueryDef {
    object: ObjectPredicateDef::HasAnyBasicLandType(&[BasicLandType::Island]),
    zones: &[ZoneKind::Battlefield],
    controller: PlayerRelation::Opponent,
};

static YOU_CONTROL_NO_ISLANDS: TriggerConditionDef = TriggerConditionDef::ObjectCount {
    query: ObjectQueryDef {
        object: ObjectPredicateDef::HasAnyBasicLandType(&[BasicLandType::Island]),
        zones: &[ZoneKind::Battlefield],
        controller: PlayerRelation::You,
    },
    comparison: ComparisonDef::Equal,
    amount: 0,
};

static REMOVE_THREE_SPORES: [AbilityCostDef; 1] = [AbilityCostDef::RemoveCountersFromSource {
    kind: CounterKind::Spore,
    amount: 3,
}];

static FUNGUS_TARGET: [AbilityTargetDef; 1] = [AbilityTargetDef::exactly_one_permanent(
    ObjectPredicateDef::Subtype("Fungus"),
)];

// FEM 1a — Combat Medic
pub(in crate::card::sets) static COMBAT_MEDIC: CardRecord = CardRecord::new(
    cards::COMBAT_MEDIC,
    "Combat Medic",
    CardArt::new(
        "9cfd96cb-03d6-4845-8595-50bf17b35726",
        "Edward P. Beard, Jr.",
    ),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{2}{W}"), &["Human", "Cleric", "Soldier"], 0, 2)
        .with_ability(AbilityDef::activated_with_targets(
            "{1}{W}: Prevent the next 1 damage that would be dealt to any target this turn.",
            &[AbilityCostDef::Mana(mana_cost!("{1}{W}"))],
            &[AbilityTargetDef::exactly_one(
                AbilityTargetPredicate::AnyTarget,
            )],
            EffectDef::PreventNextDamage {
                object: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                amount: ValueDef::Constant(1),
            },
        )),
);

// FEM 2 — Farrel's Mantle
// Audit: blocked — Needs a combat declaration or damage-assignment constraint for “Whenever enchanted creature attacks and isn't blocked, its controller may have it deal damage equal to its power plus 2 to another target creature. If that player does, the attacking…”.

// FEM 3a — Farrel's Zealot
// Audit: blocked — Needs a combat declaration or damage-assignment constraint for “Whenever this creature attacks and isn't blocked, you may have it deal 3 damage to target creature. If you do, this creature assigns no combat damage this turn”.

// FEM 4 — Farrelite Priest
// Audit: blocked — Needs the mana-ability runtime to pay this ability's mana activation cost for “{1}: Add {W}. If this ability has been activated four or more times this turn, sacrifice this creature at the beginning of the next end step”.

// FEM 5 — Hand of Justice
// Audit: blocked — Needs a persistent tap/untap restriction or event relation for “{T}, Tap three untapped white creatures you control: Destroy target creature”.

// FEM 6 — Heroism
// Audit: blocked — Needs a per-creature optional payment offered to the opposing controller, repeated for each attacking red creature; preventing one creature's combat damage is already expressible.

// FEM 7a — Icatian Infantry
// Audit: blocked — Needs band formation: creatures with banding cannot yet attack as a group, and a band is not blocked as one. Blocking with banding is implemented.

// FEM 8a — Icatian Javelineers
pub(in crate::card::sets) static ICATIAN_JAVELINEERS: CardRecord = CardRecord::new(
    cards::ICATIAN_JAVELINEERS,
    "Icatian Javelineers",
    CardArt::new("f04b8356-2384-4743-80dd-f15ca7ec65f7", "Melissa A. Benson"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{W}"), &["Human", "Soldier"], 1, 1).with_abilities(&[
        AbilityDef::as_enters(
            "This creature enters with a javelin counter on it.",
            ReplacementEffectDef::ModifyBattlefieldEntry(
                BattlefieldEntryModificationDef::AddCounters {
                    kind: CounterKind::Javelin,
                    amount: 1,
                },
            ),
        ),
        AbilityDef::activated_with_targets(
            "{T}, Remove a javelin counter from this creature: It deals 1 damage to any target.",
            &[
                AbilityCostDef::TapSource,
                AbilityCostDef::RemoveCountersFromSource {
                    kind: CounterKind::Javelin,
                    amount: 1,
                },
            ],
            &[AbilityTargetDef::exactly_one(
                AbilityTargetPredicate::AnyTarget,
            )],
            EffectDef::DealDamage {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                amount: ValueDef::Constant(1),
            },
        ),
    ]),
);

// FEM 9 — Icatian Lieutenant
pub(in crate::card::sets) static ICATIAN_LIEUTENANT: CardRecord = CardRecord::new(
    cards::ICATIAN_LIEUTENANT,
    "Icatian Lieutenant",
    CardArt::new("39fec59a-4ade-4c6f-ae7d-911fbe6da26d", "Pete Venters"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{W}{W}"), &["Human", "Soldier"], 1, 2).with_abilities(&[
        AbilityDef::activated_with_targets(
            "{1}{W}: Target Soldier creature gets +1/+0 until end of turn.",
            &[AbilityCostDef::Mana(mana_cost!("{1}{W}"))],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::All(&[
                    ObjectPredicateDef::HasType(CardType::Creature),
                    ObjectPredicateDef::Subtype("Soldier"),
                ]),
            )],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                effect: AppliedEffectDef::ModifyPowerToughness {
                    power: ValueDef::Constant(1),
                    toughness: ValueDef::Constant(0),
                },
                duration: EffectDurationDef::UntilEndOfTurn,
            },
        ),
    ]),
);

// FEM 10a — Icatian Moneychanger
pub(in crate::card::sets) static ICATIAN_MONEYCHANGER: CardRecord = CardRecord::new(
    cards::ICATIAN_MONEYCHANGER,
    "Icatian Moneychanger",
    CardArt::new("b3d502d4-4a96-47b3-ae26-8b2c9f36623d", "Drew Tucker"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{W}"), &["Human"], 0, 2).with_abilities(&[
        AbilityDef::as_enters(
            "This creature enters with three credit counters on it.",
            ReplacementEffectDef::ModifyBattlefieldEntry(
                BattlefieldEntryModificationDef::AddCounters {
                    kind: CounterKind::Credit,
                    amount: 3,
                },
            ),
        ),
        AbilityDef::triggered(
            "When this creature enters, it deals 3 damage to you.",
            TriggerEventDef::ZoneChanged {
                object: ObjectPredicateDef::Source,
                from: None,
                to: Some(ZoneKind::Battlefield),
            },
            EffectDef::DealDamage {
                recipient: EffectRecipientDef::Controller,
                amount: ValueDef::Constant(3),
            },
        ),
        AbilityDef::triggered(
            "At the beginning of your upkeep, put a credit counter on this creature.",
            TriggerEventDef::StepBegins {
                step: TurnStepDef::Upkeep,
                player: PlayerRelation::You,
            },
            EffectDef::AddCounters {
                object: EffectRecipientDef::Source,
                kind: CounterKind::Credit,
                amount: ValueDef::Constant(1),
            },
        ),
        AbilityDef::activated(
            "Sacrifice this creature: You gain 1 life for each credit counter on this creature. \
             Activate only during your upkeep.",
            &[AbilityCostDef::SacrificeSource],
            EffectDef::GainLife {
                recipient: EffectRecipientDef::Controller,
                amount: ValueDef::CountersOnSource(CounterKind::Credit),
            },
        )
        .with_activation_timing(ActivationTimingDef::YourUpkeep),
    ]),
);

// FEM 11 — Icatian Phalanx
// Audit: blocked — Needs band formation: creatures with banding cannot yet attack as a group, and a band is not blocked as one. Blocking with banding is implemented.

// FEM 12 — Icatian Priest
pub(in crate::card::sets) static ICATIAN_PRIEST: CardRecord = CardRecord::new(
    cards::ICATIAN_PRIEST,
    "Icatian Priest",
    CardArt::new("d7690cdd-6610-4310-9e93-60dc4db2ae8d", "Drew Tucker"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{W}"), &["Human", "Cleric"], 1, 1).with_abilities(&[
        AbilityDef::activated_with_targets(
            "{1}{W}{W}: Target creature gets +1/+1 until end of turn.",
            &[AbilityCostDef::Mana(mana_cost!("{1}{W}{W}"))],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::HasType(CardType::Creature),
            )],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                effect: AppliedEffectDef::ModifyPowerToughness {
                    power: ValueDef::Constant(1),
                    toughness: ValueDef::Constant(1),
                },
                duration: EffectDurationDef::UntilEndOfTurn,
            },
        ),
    ]),
);

// FEM 13a — Icatian Scout
pub(in crate::card::sets) static ICATIAN_SCOUT: CardRecord = CardRecord::new(
    cards::ICATIAN_SCOUT,
    "Icatian Scout",
    CardArt::new(
        "86bf4aaa-a9b1-4798-a96b-c3e35afb77f7",
        "Richard Kane Ferguson",
    ),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{W}"), &["Human", "Soldier", "Scout"], 1, 1)
        .with_abilities(&[AbilityDef::activated_with_targets(
            "{1}, {T}: Target creature gains first strike until end of turn.",
            &[
                AbilityCostDef::Mana(mana_cost!("{1}")),
                AbilityCostDef::TapSource,
            ],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::HasType(CardType::Creature),
            )],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                effect: AppliedEffectDef::GrantAbility(&abilities::first_strike()),
                duration: EffectDurationDef::UntilEndOfTurn,
            },
        )]),
);

// FEM 14 — Icatian Skirmishers
// Audit: blocked — Needs band formation: creatures with banding cannot yet attack as a group, and a band is not blocked as one. Blocking with banding is implemented.

// FEM 15 — Icatian Town
pub(in crate::card::sets) static ICATIAN_TOWN: CardRecord = CardRecord::new(
    cards::ICATIAN_TOWN,
    "Icatian Town",
    CardArt::new("cbb7c28d-0366-4d01-84a2-f1bc9f38aa4a", "Tom Wänerstrand"),
    CardSet::FallenEmpires,
    CardRules::new_sorcery(mana_cost!("{5}{W}")).with_abilities(&[AbilityDef::spell(
        "Create four 1/1 white Citizen creature tokens.",
        EffectDef::CreateToken {
            token: cards::CITIZEN_TOKEN_1_1_WHITE,
            count: ValueDef::Constant(4),
            tapped: false,
        },
    )]),
);

// FEM 16a — Order of Leitbur
pub(in crate::card::sets) static ORDER_OF_LEITBUR: CardRecord = CardRecord::new(
    cards::ORDER_OF_LEITBUR,
    "Order of Leitbur",
    CardArt::new("ebd6e51e-f042-4673-a898-291607105829", "Bryon Wackwitz"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{W}{W}"), &["Human", "Cleric", "Knight"], 2, 1)
        .with_abilities(&[
            abilities::protection_from(ManaColor::Black),
            AbilityDef::activated(
                "{W}: This creature gains first strike until end of turn.",
                &[AbilityCostDef::Mana(mana_cost!("{W}"))],
                EffectDef::Apply {
                    recipient: EffectRecipientDef::Source,
                    effect: AppliedEffectDef::GrantAbility(&abilities::first_strike()),
                    duration: EffectDurationDef::UntilEndOfTurn,
                },
            ),
            AbilityDef::activated(
                "{W}{W}: This creature gets +1/+0 until end of turn.",
                &[AbilityCostDef::Mana(mana_cost!("{W}{W}"))],
                EffectDef::Apply {
                    recipient: EffectRecipientDef::Source,
                    effect: AppliedEffectDef::ModifyPowerToughness {
                        power: ValueDef::Constant(1),
                        toughness: ValueDef::Constant(0),
                    },
                    duration: EffectDurationDef::UntilEndOfTurn,
                },
            ),
        ]),
);

// FEM 17 — Deep Spawn
// Audit: blocked — Needs an unless-clause whose cost is milling rather than mana for “At the beginning of your upkeep, sacrifice this creature unless you mill two cards”. Its shroud ability is available.

// FEM 18a — High Tide
// Audit: blocked — Needs cost/mana provenance or dynamic payment support for “Until end of turn, whenever a player taps an Island for mana, that player adds an additional {U}”.

static HOMARID_ONE_TIDE: TriggerConditionDef = TriggerConditionDef::SourceCounters {
    kind: CounterKind::Tide,
    comparison: ComparisonDef::Equal,
    amount: 1,
};

static HOMARID_THREE_TIDE: TriggerConditionDef = TriggerConditionDef::SourceCounters {
    kind: CounterKind::Tide,
    comparison: ComparisonDef::Equal,
    amount: 3,
};

static HOMARID_FOUR_TIDE: TriggerConditionDef = TriggerConditionDef::SourceCounters {
    kind: CounterKind::Tide,
    comparison: ComparisonDef::GreaterOrEqual,
    amount: 4,
};

static HOMARID_SHRINK: EffectDef = EffectDef::Apply {
    recipient: EffectRecipientDef::Source,
    effect: AppliedEffectDef::ModifyPowerToughness {
        power: ValueDef::Constant(-1),
        toughness: ValueDef::Constant(-1),
    },
    duration: EffectDurationDef::WhileSourceRemainsInZone,
};

static HOMARID_GROW: EffectDef = EffectDef::Apply {
    recipient: EffectRecipientDef::Source,
    effect: AppliedEffectDef::ModifyPowerToughness {
        power: ValueDef::Constant(1),
        toughness: ValueDef::Constant(1),
    },
    duration: EffectDurationDef::WhileSourceRemainsInZone,
};

// FEM 19a — Homarid
pub(in crate::card::sets) static HOMARID: CardRecord = CardRecord::new(
    cards::HOMARID,
    "Homarid",
    CardArt::new("d6ffeab4-83b1-4414-ae72-e59a2354ea15", "Quinton Hoover"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{2}{U}"), &["Homarid"], 2, 2).with_abilities(&[
        AbilityDef::as_enters(
            "This creature enters with a tide counter on it.",
            ReplacementEffectDef::ModifyBattlefieldEntry(
                BattlefieldEntryModificationDef::AddCounters {
                    kind: CounterKind::Tide,
                    amount: 1,
                },
            ),
        ),
        AbilityDef::triggered(
            "At the beginning of your upkeep, put a tide counter on this creature.",
            TriggerEventDef::StepBegins {
                step: TurnStepDef::Upkeep,
                player: PlayerRelation::You,
            },
            EffectDef::AddCounters {
                object: EffectRecipientDef::Source,
                kind: CounterKind::Tide,
                amount: ValueDef::Constant(1),
            },
        ),
        AbilityDef::static_ability(
            "As long as there is exactly one tide counter on this creature, it gets -1/-1.",
            EffectDef::IfCondition {
                condition: &HOMARID_ONE_TIDE,
                then: &HOMARID_SHRINK,
            },
        ),
        AbilityDef::static_ability(
            "As long as there are exactly three tide counters on this creature, it gets +1/+1.",
            EffectDef::IfCondition {
                condition: &HOMARID_THREE_TIDE,
                then: &HOMARID_GROW,
            },
        ),
        AbilityDef::triggered_if(
            "Whenever there are four or more tide counters on this creature, remove all tide \
             counters from it.",
            TriggerEventDef::StateCondition,
            &HOMARID_FOUR_TIDE,
            EffectDef::RemoveAllCounters {
                object: EffectRecipientDef::Source,
                kind: CounterKind::Tide,
            },
        ),
    ]),
);

// FEM 20 — Homarid Shaman
pub(in crate::card::sets) static HOMARID_SHAMAN: CardRecord = CardRecord::new(
    cards::HOMARID_SHAMAN,
    "Homarid Shaman",
    CardArt::new("c17c6416-86d6-46ea-aea1-41b98a66b250", "Amy Weber"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{2}{U}{U}"), &["Homarid", "Shaman"], 2, 1).with_abilities(
        &[AbilityDef::activated_with_targets(
            "{U}: Tap target green creature.",
            &[AbilityCostDef::Mana(mana_cost!("{U}"))],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::All(&[
                    ObjectPredicateDef::HasType(CardType::Creature),
                    ObjectPredicateDef::Color(ManaColor::Green),
                ]),
            )],
            EffectDef::Tap {
                object: EffectRecipientDef::Target(TargetIndex::PRIMARY),
            },
        )],
    ),
);

// FEM 21 — Homarid Spawning Bed
// Audit: blocked — Needs Camarid token creation whose count is the sacrificed creature's mana value.

// FEM 22a — Homarid Warrior
pub(in crate::card::sets) static HOMARID_WARRIOR: CardRecord = CardRecord::new(
    cards::HOMARID_WARRIOR,
    "Homarid Warrior",
    CardArt::new("627ca588-917f-4768-a69d-3d93c1210390", "Daniel Gelon"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{4}{U}"), &["Homarid", "Warrior"], 3, 3).with_ability(
        AbilityDef::activated(
            "{U}: This creature gains shroud until end of turn and doesn't untap during your \
             next untap step. Tap it.",
            &[AbilityCostDef::Mana(mana_cost!("{U}"))],
            EffectDef::Sequence(&[
                EffectDef::Apply {
                    recipient: EffectRecipientDef::Source,
                    effect: AppliedEffectDef::GrantAbility(&SHROUD),
                    duration: EffectDurationDef::UntilEndOfTurn,
                },
                EffectDef::SkipNextUntapSteps {
                    object: EffectRecipientDef::Source,
                    count: 1,
                },
                EffectDef::Tap {
                    object: EffectRecipientDef::Source,
                },
            ]),
        ),
    ),
);

// FEM 23a — Merseine
// Audit: blocked — Needs card-specific counter state and counter-consuming effects for “Enchanted creature doesn't untap during its controller's untap step if this Aura has a net counter on it”.

// FEM 24 — River Merfolk
pub(in crate::card::sets) static RIVER_MERFOLK: CardRecord = CardRecord::new(
    cards::RIVER_MERFOLK,
    "River Merfolk",
    CardArt::new("27d7fa54-4b89-4a9a-b088-4b89c525c1ea", "Douglas Shuler"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{U}{U}"), &["Merfolk"], 2, 1).with_abilities(&[
        AbilityDef::activated(
            "{U}: This creature gains mountainwalk until end of turn.",
            &[AbilityCostDef::Mana(mana_cost!("{U}"))],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Source,
                effect: AppliedEffectDef::GrantAbility(&abilities::mountainwalk()),
                duration: EffectDurationDef::UntilEndOfTurn,
            },
        ),
    ]),
);

// FEM 25 — Seasinger
// Audit: blocked — Needs duration-aware control-changing continuous effects for “{T}: Gain control of target creature whose controller controls an Island for as long as you control this creature and this creature remains tapped”.

// FEM 26 — Svyelunite Priest
pub(in crate::card::sets) static SVYELUNITE_PRIEST: CardRecord = CardRecord::new(
    cards::SVYELUNITE_PRIEST,
    "Svyelunite Priest",
    CardArt::new("316d25ae-7ac6-4f5b-93ab-0e0e28ec104b", "Ron Spencer"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{1}{U}"), &["Merfolk", "Cleric"], 1, 1).with_ability(
        AbilityDef::activated_with_targets(
            "{U}{U}, {T}: Target creature gains shroud until end of turn. Activate only during \
             your upkeep.",
            &[
                AbilityCostDef::Mana(mana_cost!("{U}{U}")),
                AbilityCostDef::TapSource,
            ],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::HasType(CardType::Creature),
            )],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                effect: AppliedEffectDef::GrantAbility(&SHROUD),
                duration: EffectDurationDef::UntilEndOfTurn,
            },
        )
        .with_activation_timing(ActivationTimingDef::YourUpkeep),
    ),
);

static SHROUD: AbilityDef = abilities::shroud();

// FEM 27a — Tidal Flats
// Audit: blocked — Needs a combat declaration or damage-assignment constraint for “{U}{U}: For each attacking creature without flying, its controller may pay {1}. If that player doesn't, creatures you control blocking that creature gain first strike until end of turn”.

// FEM 28 — Tidal Influence
// Audit: blocked — Needs card-specific counter state and counter-consuming effects for “As long as there are exactly three tide counters on this enchantment, all blue creatures get +2/+0”.

// FEM 29 — Vodalian Knights
pub(in crate::card::sets) static VODALIAN_KNIGHTS: CardRecord = CardRecord::new(
    cards::VODALIAN_KNIGHTS,
    "Vodalian Knights",
    CardArt::new("68d97e1b-2526-4740-b354-f158734d1f72", "Susan Van Camp"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{1}{U}{U}"), &["Merfolk", "Knight"], 2, 2).with_abilities(
        &[
            abilities::first_strike(),
            AbilityDef::static_ability(
                "This creature can't attack unless defending player controls an Island.",
                EffectDef::CannotAttackUnless(&DEFENDER_CONTROLS_AN_ISLAND),
            ),
            AbilityDef::triggered_if(
                "When you control no Islands, sacrifice this creature.",
                TriggerEventDef::StateCondition,
                &YOU_CONTROL_NO_ISLANDS,
                EffectDef::Sacrifice {
                    object: EffectRecipientDef::Source,
                },
            ),
        ],
    ),
);

// FEM 30a — Vodalian Mage
pub(in crate::card::sets) static VODALIAN_MAGE: CardRecord = CardRecord::new(
    cards::VODALIAN_MAGE,
    "Vodalian Mage",
    CardArt::new("c107e82b-134a-4f2b-98c2-6537fae6a50d", "Susan Van Camp"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{2}{U}"), &["Merfolk", "Wizard"], 1, 1).with_abilities(&[
        AbilityDef::activated_with_targets(
            "{U}, {T}: Counter target spell unless its controller pays {1}.",
            &[
                AbilityCostDef::Mana(mana_cost!("{U}")),
                AbilityCostDef::TapSource,
            ],
            &[AbilityTargetDef::exactly_one_spell(ObjectPredicateDef::Any)],
            EffectDef::CounterUnlessPaid {
                object: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                amount: ValueDef::Constant(1),
                zone: ZoneKind::Graveyard,
            },
        ),
    ]),
);

// FEM 31a — Vodalian Soldiers
pub(in crate::card::sets) static VODALIAN_SOLDIERS: CardRecord = CardRecord::new(
    cards::VODALIAN_SOLDIERS,
    "Vodalian Soldiers",
    CardArt::new("7eb50256-9113-4b03-bcef-9aea24be8493", "Melissa A. Benson"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{1}{U}"), &["Merfolk", "Soldier"], 1, 2),
);

// FEM 32 — Vodalian War Machine
// Audit: blocked — Needs a combat declaration or damage-assignment constraint for “Tap an untapped Merfolk you control: This creature can attack this turn as though it didn't have defender”.

// FEM 33a — Armor Thrull
pub(in crate::card::sets) static ARMOR_THRULL: CardRecord = CardRecord::new(
    cards::ARMOR_THRULL,
    "Armor Thrull",
    CardArt::new("a98384d1-8e7d-4c41-9f23-47bc2ae2ad6a", "Pete Venters"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{2}{B}"), &["Thrull"], 1, 3).with_ability(
        AbilityDef::activated_with_targets(
            "{T}, Sacrifice this creature: Put a +1/+2 counter on target creature.",
            &[AbilityCostDef::TapSource, AbilityCostDef::SacrificeSource],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::HasType(CardType::Creature),
            )],
            EffectDef::AddCounters {
                object: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                kind: CounterKind::PlusOnePlusTwo,
                amount: ValueDef::Constant(1),
            },
        ),
    ),
);

// FEM 34a — Basal Thrull
pub(in crate::card::sets) static BASAL_THRULL: CardRecord = CardRecord::new(
    cards::BASAL_THRULL,
    "Basal Thrull",
    CardArt::new("0c1d5d13-0160-48cb-8fac-dd86102569b4", "Kaja Foglio"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{B}{B}"), &["Thrull"], 1, 2).with_abilities(&[
        AbilityDef::activated_mana(
            "{T}, Sacrifice this creature: Add {B}{B}.",
            &[AbilityCostDef::TapSource, AbilityCostDef::SacrificeSource],
            EffectDef::AddMana(AddManaEffectDef::one(ManaColor::Black).with_amount(2)),
        ),
    ]),
);

// FEM 35 — Breeding Pit
pub(in crate::card::sets) static BREEDING_PIT: CardRecord = CardRecord::new(
    cards::BREEDING_PIT,
    "Breeding Pit",
    CardArt::new("a0d7e85f-eba5-4fc5-9fc0-109109d368aa", "Anson Maddocks"),
    CardSet::FallenEmpires,
    CardRules::new_enchantment(mana_cost!("{3}{B}")).with_abilities(&[
        AbilityDef::triggered(
            "At the beginning of your upkeep, sacrifice this enchantment unless you pay {B}{B}.",
            TriggerEventDef::StepBegins {
                step: TurnStepDef::Upkeep,
                player: PlayerRelation::You,
            },
            EffectDef::UnlessPaid {
                cost: mana_cost!("{B}{B}"),
                otherwise: &EffectDef::Sacrifice {
                    object: EffectRecipientDef::Source,
                },
            },
        ),
        AbilityDef::triggered(
            "At the beginning of your end step, create a 0/1 black Thrull creature token.",
            TriggerEventDef::StepBegins {
                step: TurnStepDef::End,
                player: PlayerRelation::You,
            },
            EffectDef::CreateToken {
                token: cards::THRULL_TOKEN_0_1_BLACK,
                count: ValueDef::Constant(1),
                tapped: false,
            },
        ),
    ]),
);

// FEM 36 — Derelor
// Audit: blocked — Needs a spell-color predicate in trigger capture for “Black spells you cast cost {B} more to cast”.

// FEM 37 — Ebon Praetor
// Audit: blocked — Needs card-specific counter state and counter-consuming effects for “Sacrifice a creature: Remove a -2/-2 counter from this creature. If the sacrificed creature was a Thrull, put a +1/+0 counter on this creature. Activate only during your upkeep and only…”.

// FEM 38a — Hymn to Tourach
pub(in crate::card::sets) static HYMN_TO_TOURACH: CardRecord = CardRecord::new(
    cards::HYMN_TO_TOURACH,
    "Hymn to Tourach",
    CardArt::new("eb9273ea-9a41-42e3-8c9c-0d50b127a818", "Susan Van Camp"),
    CardSet::FallenEmpires,
    CardRules::new_sorcery(mana_cost!("{B}{B}")).with_abilities(&[AbilityDef::spell_with_targets(
        "Target player discards two cards at random.",
        &[AbilityTargetDef::exactly_one(
            AbilityTargetPredicate::Player(PlayerRelation::Any),
        )],
        EffectDef::Discard {
            recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
            amount: ValueDef::Constant(2),
            selection: DiscardSelectionDef::Random,
        },
    )]),
);

// FEM 39a — Initiates of the Ebon Hand
// Audit: blocked — Needs the mana-ability runtime to pay this ability's mana activation cost for “{1}: Add {B}. If this ability has been activated four or more times this turn, sacrifice this creature at the beginning of the next end step”.

static MINDSTAB_THRULL_STRIKE: [EffectDef; 2] = [
    EffectDef::Sacrifice {
        object: EffectRecipientDef::Source,
    },
    EffectDef::Discard {
        recipient: EffectRecipientDef::Opponent,
        amount: ValueDef::Constant(3),
        selection: DiscardSelectionDef::RecipientChooses,
    },
];

// FEM 40a — Mindstab Thrull
pub(in crate::card::sets) static MINDSTAB_THRULL: CardRecord = CardRecord::new(
    cards::MINDSTAB_THRULL,
    "Mindstab Thrull",
    CardArt::new(
        "499a791f-ac4f-4a96-b59b-37043686a79a",
        "Richard Kane Ferguson",
    ),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{1}{B}{B}"), &["Thrull"], 2, 2).with_ability(
        AbilityDef::triggered(
            "Whenever this creature attacks and isn't blocked, you may sacrifice it. If you do, \
             defending player discards three cards.",
            TriggerEventDef::AttacksAndIsNotBlocked {
                attacker: ObjectPredicateDef::Source,
            },
            EffectDef::May {
                player: EffectRecipientDef::Controller,
                effect: &EffectDef::Sequence(&MINDSTAB_THRULL_STRIKE),
            },
        ),
    ),
);

static NECRITE_STRIKE: [EffectDef; 2] = [
    EffectDef::Sacrifice {
        object: EffectRecipientDef::Source,
    },
    // "It can't be regenerated" is the destruction's own flag rather than a
    // separate prohibition: nothing else this turn is being denied a shield.
    EffectDef::Destroy {
        object: EffectRecipientDef::Target(TargetIndex::PRIMARY),
        can_regenerate: false,
    },
];

static NECRITE_TARGET: [AbilityTargetDef; 1] = [AbilityTargetDef::exactly_one(
    AbilityTargetPredicate::Object {
        object: ObjectPredicateDef::HasType(CardType::Creature),
        zones: &[ZoneKind::Battlefield],
        controller: Some(PlayerRelation::Opponent),
        owner: None,
    },
)];

// FEM 41a — Necrite
pub(in crate::card::sets) static NECRITE: CardRecord = CardRecord::new(
    cards::NECRITE,
    "Necrite",
    CardArt::new("311d752a-ce8a-44cb-8aeb-1ed66705eb09", "Ron Spencer"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{1}{B}{B}"), &["Thrull"], 2, 2).with_ability(
        AbilityDef::triggered_with_targets(
            "Whenever this creature attacks and isn't blocked, you may sacrifice it. If you do, \
             destroy target creature defending player controls. It can't be regenerated.",
            TriggerEventDef::AttacksAndIsNotBlocked {
                attacker: ObjectPredicateDef::Source,
            },
            &NECRITE_TARGET,
            EffectDef::May {
                player: EffectRecipientDef::Controller,
                effect: &EffectDef::Sequence(&NECRITE_STRIKE),
            },
        ),
    ),
);

// FEM 42a — Order of the Ebon Hand
pub(in crate::card::sets) static ORDER_OF_THE_EBON_HAND: CardRecord = CardRecord::new(
    cards::ORDER_OF_THE_EBON_HAND,
    "Order of the Ebon Hand",
    CardArt::new("9e51f5d8-a7cc-4720-8af5-e002bcfd78a0", "Melissa A. Benson"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{B}{B}"), &["Cleric", "Knight"], 2, 1).with_abilities(&[
        abilities::protection_from(ManaColor::White),
        AbilityDef::activated(
            "{B}: This creature gains first strike until end of turn.",
            &[AbilityCostDef::Mana(mana_cost!("{B}"))],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Source,
                effect: AppliedEffectDef::GrantAbility(&abilities::first_strike()),
                duration: EffectDurationDef::UntilEndOfTurn,
            },
        ),
        AbilityDef::activated(
            "{B}{B}: This creature gets +1/+0 until end of turn.",
            &[AbilityCostDef::Mana(mana_cost!("{B}{B}"))],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Source,
                effect: AppliedEffectDef::ModifyPowerToughness {
                    power: ValueDef::Constant(1),
                    toughness: ValueDef::Constant(0),
                },
                duration: EffectDurationDef::UntilEndOfTurn,
            },
        ),
    ]),
);

// FEM 43 — Soul Exchange
// Audit: blocked — Needs a zone-object query and identity-preserving continuation for “As an additional cost to cast this spell, exile a creature you control”.

// FEM 44 — Thrull Champion
pub(in crate::card::sets) static THRULL_CHAMPION: CardRecord = CardRecord::new(
    cards::THRULL_CHAMPION,
    "Thrull Champion",
    CardArt::new("4d3cafdd-a03b-4b08-b9c1-c776f8450d3a", "Daniel Gelon"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{4}{B}"), &["Thrull"], 2, 2).with_abilities(&[
        AbilityDef::static_ability(
            "Thrull creatures get +1/+1.",
            EffectDef::Apply {
                recipient: EffectRecipientDef::MatchingObjects {
                    object: ObjectPredicateDef::All(&[
                        ObjectPredicateDef::HasType(CardType::Creature),
                        ObjectPredicateDef::Subtype("Thrull"),
                    ]),
                    zones: &[ZoneKind::Battlefield],
                    controller: PlayerRelation::Any,
                },
                effect: AppliedEffectDef::ModifyPowerToughness {
                    power: ValueDef::Constant(1),
                    toughness: ValueDef::Constant(1),
                },
                duration: EffectDurationDef::WhileSourceRemainsInZone,
            },
        ),
        AbilityDef::activated_with_targets(
            "{T}: Gain control of target Thrull for as long as you control this creature.",
            &[AbilityCostDef::TapSource],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::Subtype("Thrull"),
            )],
            EffectDef::GainControlWhileSourceRemains {
                object: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                while_tapped: false,
            },
        ),
    ]),
);

// FEM 45 — Thrull Retainer
pub(in crate::card::sets) static THRULL_RETAINER: CardRecord = CardRecord::new(
    cards::THRULL_RETAINER,
    "Thrull Retainer",
    CardArt::new("d800512b-1492-41d2-931d-57c625044454", "Ron Spencer"),
    CardSet::FallenEmpires,
    CardRules::new_enchantment(mana_cost!("{B}"))
        .with_subtypes(&["Aura"])
        .with_abilities(&[
            abilities::aura_spell("Enchant creature", &abilities::ENCHANT_CREATURE_TARGET),
            AbilityDef::static_ability(
                "Enchanted creature gets +1/+1.",
                EffectDef::Apply {
                    recipient: EffectRecipientDef::AttachedPermanent,
                    effect: AppliedEffectDef::ModifyPowerToughness {
                        power: ValueDef::Constant(1),
                        toughness: ValueDef::Constant(1),
                    },
                    duration: EffectDurationDef::WhileSourceRemainsInZone,
                },
            ),
            AbilityDef::activated(
                "Sacrifice this Aura: Regenerate enchanted creature.",
                &[AbilityCostDef::SacrificeSource],
                EffectDef::Regenerate {
                    object: EffectRecipientDef::AttachedPermanent,
                },
            ),
        ]),
);

// FEM 46 — Thrull Wizard
// Audit: blocked — Needs a spell-color predicate in trigger capture for “{1}{B}: Counter target black spell unless that spell's controller pays {B} or {3}”.

// FEM 47 — Tourach's Chant
// Audit: blocked — Needs card-specific counter state and counter-consuming effects for “Whenever a player puts a Forest onto the battlefield, this enchantment deals 3 damage to that player unless they put a -1/-1 counter on a creature they control”.

// FEM 48 — Tourach's Gate
// Audit: blocked — Needs the clause's conditional recipient set or dynamic modifier value for “Tap enchanted land: Attacking creatures you control get +2/-1 until end of turn. Activate only if enchanted land is untapped”.

// FEM 49a — Brassclaw Orcs
// Audit: blocked — Needs a combat declaration or damage-assignment constraint for “This creature can't block creatures with power 2 or greater”.

// FEM 50 — Dwarven Armorer
// Audit: blocked — Needs card-specific counter state and counter-consuming effects for “{R}, {T}, Discard a card: Put a +0/+1 counter or a +1/+0 counter on target creature”.

// FEM 51 — Dwarven Catapult
// Audit: blocked — Needs damage divided evenly with downward rounding across a dynamically counted opponent creature set.

// FEM 52 — Dwarven Lieutenant
pub(in crate::card::sets) static DWARVEN_LIEUTENANT: CardRecord = CardRecord::new(
    cards::DWARVEN_LIEUTENANT,
    "Dwarven Lieutenant",
    CardArt::new("ea9a38b1-4676-425a-b40d-4fb478966024", "Jeff A. Menges"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{R}{R}"), &["Dwarf", "Soldier"], 1, 2).with_abilities(&[
        AbilityDef::activated_with_targets(
            "{1}{R}: Target Dwarf creature gets +1/+0 until end of turn.",
            &[AbilityCostDef::Mana(mana_cost!("{1}{R}"))],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::All(&[
                    ObjectPredicateDef::HasType(CardType::Creature),
                    ObjectPredicateDef::Subtype("Dwarf"),
                ]),
            )],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                effect: AppliedEffectDef::ModifyPowerToughness {
                    power: ValueDef::Constant(1),
                    toughness: ValueDef::Constant(0),
                },
                duration: EffectDurationDef::UntilEndOfTurn,
            },
        ),
    ]),
);

// FEM 53a — Dwarven Soldier
// Audit: blocked — Needs a combat declaration or damage-assignment constraint for “Whenever this creature blocks or becomes blocked by one or more Orcs, this creature gets +0/+2 until end of turn”.

// FEM 54a — Goblin Chirurgeon
pub(in crate::card::sets) static GOBLIN_CHIRURGEON: CardRecord = CardRecord::new(
    cards::GOBLIN_CHIRURGEON,
    "Goblin Chirurgeon",
    CardArt::new("2b710c21-e9f5-4660-80f6-2104ec65f63f", "Daniel Gelon"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{R}"), &["Goblin", "Shaman"], 0, 2).with_abilities(&[
        AbilityDef::activated_with_targets(
            "Sacrifice a Goblin: Regenerate target creature.",
            &[AbilityCostDef::SacrificePermanent {
                object: ObjectPredicateDef::Subtype("Goblin"),
                controller: PlayerRelation::You,
            }],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::HasType(CardType::Creature),
            )],
            EffectDef::Regenerate {
                object: EffectRecipientDef::Target(TargetIndex::PRIMARY),
            },
        ),
    ]),
);

// FEM 55 — Goblin Flotilla
// Audit: blocked — Needs a combat declaration or damage-assignment constraint for “At the beginning of each combat, unless you pay {R}, whenever this creature blocks or becomes blocked by a creature this combat, that creature gains first strike until end of turn”.

// FEM 56a — Goblin Grenade
pub(in crate::card::sets) static GOBLIN_GRENADE: CardRecord = CardRecord::new(
    cards::GOBLIN_GRENADE,
    "Goblin Grenade",
    CardArt::new("8837eaba-9602-4f63-9897-85583fcdcf51", "Ron Spencer"),
    CardSet::FallenEmpires,
    CardRules::new_sorcery(mana_cost!("{R}")).with_abilities(&[
        AbilityDef::custom_full(
            "As an additional cost to cast this spell, sacrifice a Goblin.\nGoblin Grenade deals 5 damage to any target.",
            CardBehavior::GoblinGrenade,
            "The additional cost, target selection, and damage are implemented by the legacy spell resolver.",
        ),
    ]),
);

// FEM 57 — Goblin Kites
// Audit: blocked — Needs a deterministic recorded coin-flip choice and both result branches for “{R}: Target creature you control with toughness 2 or less gains flying until end of turn. Flip a coin at the beginning of the next end step. If you lose the flip, sacrifice that creature”.

// FEM 58a — Goblin War Drums
// Audit: blocked — Needs menace as an executable minimum-blocker constraint and external keyword grant for “Creatures you control have menace”.

// FEM 59 — Goblin Warrens
// Audit: blocked — Needs an activated cost that selects and sacrifices two Goblins; only one chosen permanent can currently be sacrificed as a cost.

// FEM 60 — Orcish Captain
pub(in crate::card::sets) static ORCISH_CAPTAIN: CardRecord = CardRecord::new(
    cards::ORCISH_CAPTAIN,
    "Orcish Captain",
    CardArt::new("e43cf61d-b4d6-4461-a228-47fd8b026d33", "Mark Tedin"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{R}"), &["Orc", "Warrior"], 1, 1).with_ability(
        AbilityDef::activated_with_targets(
            "{1}: Flip a coin. If you win the flip, target Orc creature gets +2/+0 until end of \
             turn. If you lose the flip, it gets -0/-2 until end of turn.",
            &[AbilityCostDef::Mana(mana_cost!("{1}"))],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::All(&[
                    ObjectPredicateDef::HasType(CardType::Creature),
                    ObjectPredicateDef::Subtype("Orc"),
                ]),
            )],
            EffectDef::Randomized {
                likelihood: COIN_FLIP,
                on_success: &ORCISH_CAPTAIN_WON,
                on_failure: &ORCISH_CAPTAIN_LOST,
            },
        ),
    ),
);

/// A coin is an even chance, which is the whole of what "flip a coin" means
/// to the seeded randomiser.
const COIN_FLIP: LikelihoodDef = LikelihoodDef::new(0.5);

static ORCISH_CAPTAIN_WON: EffectDef = orcish_captain_pump(2, 0);
static ORCISH_CAPTAIN_LOST: EffectDef = orcish_captain_pump(0, -2);

const fn orcish_captain_pump(power: i32, toughness: i32) -> EffectDef {
    EffectDef::Apply {
        recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
        effect: AppliedEffectDef::ModifyPowerToughness {
            power: ValueDef::Constant(power),
            toughness: ValueDef::Constant(toughness),
        },
        duration: EffectDurationDef::UntilEndOfTurn,
    }
}

// FEM 61a — Orcish Spy
// Audit: blocked — Needs ordered-library inspection, selection, and visibility handling for “{T}: Look at the top three cards of target player's library”.

// FEM 62a — Orcish Veteran
// Audit: blocked — Needs a combat declaration or damage-assignment constraint for “This creature can't block white creatures with power 2 or greater”.

// FEM 63 — Orgg
// Audit: blocked — Needs a combat declaration or damage-assignment constraint for “This creature can't attack if defending player controls an untapped creature with power 3 or greater”.

// FEM 64 — Raiding Party
// Audit: blocked — Needs a persistent tap/untap restriction or event relation for “Sacrifice an Orc: Each player may tap any number of untapped white creatures they control. For each creature tapped this way, that player chooses up to two Plains. Then destroy all…”.

// FEM 65a — Elven Fortress
pub(in crate::card::sets) static ELVEN_FORTRESS: CardRecord = CardRecord::new(
    cards::ELVEN_FORTRESS,
    "Elven Fortress",
    CardArt::new("9387105d-46d0-4db0-8980-dd0fded15eef", "Pete Venters"),
    CardSet::FallenEmpires,
    CardRules::new_enchantment(mana_cost!("{G}")).with_abilities(&[
        AbilityDef::activated_with_targets(
            "{1}{G}: Target blocking creature gets +0/+1 until end of turn.",
            &[AbilityCostDef::Mana(mana_cost!("{1}{G}"))],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::All(&[
                    ObjectPredicateDef::HasType(CardType::Creature),
                    ObjectPredicateDef::AttackingOrBlocking,
                    ObjectPredicateDef::Not(&ObjectPredicateDef::Attacking),
                ]),
            )],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                effect: AppliedEffectDef::ModifyPowerToughness {
                    power: ValueDef::Constant(0),
                    toughness: ValueDef::Constant(1),
                },
                duration: EffectDurationDef::UntilEndOfTurn,
            },
        ),
    ]),
);

static THELONITE_DRUID_ANIMATION: AnimationDef = AnimationDef::new(2, 3);

// FEM 66 — Elvish Farmer
pub(in crate::card::sets) static ELVISH_FARMER: CardRecord = CardRecord::new(
    cards::ELVISH_FARMER,
    "Elvish Farmer",
    CardArt::new(
        "40a9710e-b2f8-4746-8640-d450f58a6e49",
        "Richard Kane Ferguson",
    ),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{1}{G}"), &["Elf"], 0, 2).with_abilities(&[
            AbilityDef::triggered(
                "At the beginning of your upkeep, put a spore counter on this creature.",
                TriggerEventDef::StepBegins {
                    step: TurnStepDef::Upkeep,
                    player: PlayerRelation::You,
                },
                EffectDef::AddCounters {
                    object: EffectRecipientDef::Source,
                    kind: CounterKind::Spore,
                    amount: ValueDef::Constant(1),
                },
            ),
            AbilityDef::activated(
                "Remove three spore counters from this creature: Create a 1/1 green Saproling creature token.",
                &REMOVE_THREE_SPORES,
                EffectDef::CreateToken {
                    token: cards::SAPROLING_TOKEN_1_1_GREEN,
                    count: ValueDef::Constant(1),
                    tapped: false,
                },
            ),
            AbilityDef::activated(
                "Sacrifice a Saproling: You gain 2 life.",
            &[AbilityCostDef::SacrificePermanent {
                object: ObjectPredicateDef::Subtype("Saproling"),
                controller: PlayerRelation::You,
            }],
                EffectDef::GainLife {
                    recipient: EffectRecipientDef::Controller,
                    amount: ValueDef::Constant(2),
                },
            ),
    ]),
);

// FEM 67a — Elvish Hunter
pub(in crate::card::sets) static ELVISH_HUNTER: CardRecord = CardRecord::new(
    cards::ELVISH_HUNTER,
    "Elvish Hunter",
    CardArt::new("e00455ac-c7ce-4916-98ed-cca9354e3f22", "Mark Poole"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{1}{G}"), &["Elf", "Archer"], 1, 1).with_ability(
        AbilityDef::activated_with_targets(
            "{1}{G}, {T}: Target creature doesn't untap during its controller's next untap step.",
            &[
                AbilityCostDef::Mana(mana_cost!("{1}{G}")),
                AbilityCostDef::TapSource,
            ],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::HasType(CardType::Creature),
            )],
            EffectDef::SkipNextUntapSteps {
                object: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                count: 1,
            },
        ),
    ),
);

// FEM 68a — Elvish Scout
// Audit: blocked — Needs a duration-scoped replacement/prevention effect for “{G}, {T}: Untap target attacking creature you control. Prevent all combat damage that would be dealt to and dealt by it this turn”.

// FEM 69 — Feral Thallid
pub(in crate::card::sets) static FERAL_THALLID: CardRecord = CardRecord::new(
    cards::FERAL_THALLID,
    "Feral Thallid",
    CardArt::new("e585241e-c647-456d-b3b1-3d48dd78c372", "Rob Alexander"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{3}{G}{G}{G}"), &["Fungus"], 6, 3).with_abilities(&[
        AbilityDef::triggered(
            "At the beginning of your upkeep, put a spore counter on this creature.",
            TriggerEventDef::StepBegins {
                step: TurnStepDef::Upkeep,
                player: PlayerRelation::You,
            },
            EffectDef::AddCounters {
                object: EffectRecipientDef::Source,
                kind: CounterKind::Spore,
                amount: ValueDef::Constant(1),
            },
        ),
        AbilityDef::activated(
            "Remove three spore counters from this creature: Regenerate this creature.",
            &REMOVE_THREE_SPORES,
            EffectDef::Regenerate {
                object: EffectRecipientDef::Source,
            },
        ),
    ]),
);

// FEM 70 — Fungal Bloom
pub(in crate::card::sets) static FUNGAL_BLOOM: CardRecord = CardRecord::new(
    cards::FUNGAL_BLOOM,
    "Fungal Bloom",
    CardArt::new("cf1a2cb2-9a6b-41f7-96f7-ec457c69c16c", "Daniel Gelon"),
    CardSet::FallenEmpires,
    CardRules::new_enchantment(mana_cost!("{G}{G}")).with_abilities(&[
        AbilityDef::activated_with_targets(
            "{G}{G}: Put a spore counter on target Fungus.",
            &[AbilityCostDef::Mana(mana_cost!("{G}{G}"))],
            &FUNGUS_TARGET,
            EffectDef::AddCounters {
                object: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                kind: CounterKind::Spore,
                amount: ValueDef::Constant(1),
            },
        ),
    ]),
);

// FEM 71a — Night Soil
// Audit: blocked — Needs a zone-object query and identity-preserving continuation for “{1}, Exile two creature cards from a single graveyard: Create a 1/1 green Saproling creature token”.

// FEM 72a — Spore Cloud
// Audit: blocked — Needs a next-untap-step restriction applied to every creature in combat; tapping the blockers and preventing the turn's combat damage are both expressible.

// FEM 73 — Spore Flower
pub(in crate::card::sets) static SPORE_FLOWER: CardRecord = CardRecord::new(
    cards::SPORE_FLOWER,
    "Spore Flower",
    CardArt::new("f9681dc0-d0fc-4d5b-a23c-63ec1cc8343d", "Margaret Organ-Kean"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{G}{G}"), &["Fungus"], 0, 1)
        .with_abilities(&[
            AbilityDef::triggered(
                "At the beginning of your upkeep, put a spore counter on this creature.",
                TriggerEventDef::StepBegins {
                    step: TurnStepDef::Upkeep,
                    player: PlayerRelation::You,
                },
                EffectDef::AddCounters {
                    object: EffectRecipientDef::Source,
                    kind: CounterKind::Spore,
                    amount: ValueDef::Constant(1),
                },
            ),
            AbilityDef::activated(
                "Remove three spore counters from this creature: Prevent all combat damage that would be dealt this turn.",
                &REMOVE_THREE_SPORES,
                EffectDef::PreventAllCombatDamageThisTurn,
            ),
        ]),
);

// FEM 74a — Thallid
pub(in crate::card::sets) static THALLID: CardRecord = CardRecord::new(
    cards::THALLID,
    "Thallid",
    CardArt::new("4caaf31b-86a9-485b-8da7-d5b526ed1233", "Edward P. Beard, Jr."),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{G}"), &["Fungus"], 1, 1)
        .with_abilities(&[
            AbilityDef::triggered(
                "At the beginning of your upkeep, put a spore counter on this creature.",
                TriggerEventDef::StepBegins {
                    step: TurnStepDef::Upkeep,
                    player: PlayerRelation::You,
                },
                EffectDef::AddCounters {
                    object: EffectRecipientDef::Source,
                    kind: CounterKind::Spore,
                    amount: ValueDef::Constant(1),
                },
            ),
            AbilityDef::activated(
                "Remove three spore counters from this creature: Create a 1/1 green Saproling creature token.",
                &REMOVE_THREE_SPORES,
                EffectDef::CreateToken {
                    token: cards::SAPROLING_TOKEN_1_1_GREEN,
                    count: ValueDef::Constant(1),
                    tapped: false,
                },
            ),
        ]),
);

// FEM 75 — Thallid Devourer
pub(in crate::card::sets) static THALLID_DEVOURER: CardRecord = CardRecord::new(
    cards::THALLID_DEVOURER,
    "Thallid Devourer",
    CardArt::new("aa533845-4c4b-4072-aa39-8e56ce7ec325", "Ron Spencer"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{1}{G}{G}"), &["Fungus"], 2, 2).with_abilities(&[
            AbilityDef::triggered(
                "At the beginning of your upkeep, put a spore counter on this creature.",
                TriggerEventDef::StepBegins {
                    step: TurnStepDef::Upkeep,
                    player: PlayerRelation::You,
                },
                EffectDef::AddCounters {
                    object: EffectRecipientDef::Source,
                    kind: CounterKind::Spore,
                    amount: ValueDef::Constant(1),
                },
            ),
            AbilityDef::activated(
                "Remove three spore counters from this creature: Create a 1/1 green Saproling creature token.",
                &REMOVE_THREE_SPORES,
                EffectDef::CreateToken {
                    token: cards::SAPROLING_TOKEN_1_1_GREEN,
                    count: ValueDef::Constant(1),
                    tapped: false,
                },
            ),
            AbilityDef::activated(
                "Sacrifice a Saproling: This creature gets +1/+2 until end of turn.",
            &[AbilityCostDef::SacrificePermanent {
                object: ObjectPredicateDef::Subtype("Saproling"),
                controller: PlayerRelation::You,
            }],
                EffectDef::Apply {
                    recipient: EffectRecipientDef::Source,
                    effect: AppliedEffectDef::ModifyPowerToughness {
                        power: ValueDef::Constant(1),
                        toughness: ValueDef::Constant(2),
                    },
                    duration: EffectDurationDef::UntilEndOfTurn,
                },
            ),
    ]),
);

// FEM 76 — Thelon's Chant
// Audit: blocked — Needs card-specific counter state and counter-consuming effects for “Whenever a player puts a Swamp onto the battlefield, this enchantment deals 3 damage to that player unless the player puts a -1/-1 counter on a creature they control”.

// FEM 77 — Thelon's Curse
// Audit: blocked — Needs a persistent tap/untap restriction or event relation for “At the beginning of each player's upkeep, that player may choose any number of tapped blue creatures they control and pay {U} for each creature chosen this way. If the player does, untap…”.

// FEM 78 — Thelonite Druid
pub(in crate::card::sets) static THELONITE_DRUID: CardRecord = CardRecord::new(
    cards::THELONITE_DRUID,
    "Thelonite Druid",
    CardArt::new(
        "cd8772dd-513d-4dd0-a5db-5214dc8da4e0",
        "Margaret Organ-Kean",
    ),
    CardSet::FallenEmpires,
    CardRules::new_creature(
        mana_cost!("{2}{G}"),
        &["Human", "Cleric", "Druid"],
        1,
        1,
    )
    .with_ability(AbilityDef::activated(
        "{1}{G}, {T}, Sacrifice a creature: Forests you control become 2/3 creatures until end of turn. They're still lands.",
        &[
            AbilityCostDef::Mana(mana_cost!("{1}{G}")),
            AbilityCostDef::TapSource,
            AbilityCostDef::SacrificePermanent {
                object: ObjectPredicateDef::HasType(CardType::Creature),
                controller: PlayerRelation::You,
            },
        ],
        EffectDef::Apply {
            recipient: EffectRecipientDef::MatchingObjects {
                object: ObjectPredicateDef::HasAnyBasicLandType(&[BasicLandType::Forest]),
                zones: &[ZoneKind::Battlefield],
                controller: PlayerRelation::You,
            },
            effect: AppliedEffectDef::Animate(&THELONITE_DRUID_ANIMATION),
            duration: EffectDurationDef::UntilEndOfTurn,
        },
    )),
);

// FEM 79 — Thelonite Monk
// Audit: blocked — Needs a resolving land-type-setting operation; SetLandTypes currently runs only as a static continuous effect.

// FEM 80a — Thorn Thallid
pub(in crate::card::sets) static THORN_THALLID: CardRecord = CardRecord::new(
    cards::THORN_THALLID,
    "Thorn Thallid",
    CardArt::new("16e61c00-3e94-4f6f-8515-65b430829e91", "Daniel Gelon"),
    CardSet::FallenEmpires,
    CardRules::new_creature(mana_cost!("{1}{G}{G}"), &["Fungus"], 2, 2).with_abilities(&[
        AbilityDef::triggered(
            "At the beginning of your upkeep, put a spore counter on this creature.",
            TriggerEventDef::StepBegins {
                step: TurnStepDef::Upkeep,
                player: PlayerRelation::You,
            },
            EffectDef::AddCounters {
                object: EffectRecipientDef::Source,
                kind: CounterKind::Spore,
                amount: ValueDef::Constant(1),
            },
        ),
        AbilityDef::activated_with_targets(
            "Remove three spore counters from this creature: It deals 1 damage to any target.",
            &REMOVE_THREE_SPORES,
            &[AbilityTargetDef::exactly_one(
                AbilityTargetPredicate::AnyTarget,
            )],
            EffectDef::DealDamage {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                amount: ValueDef::Constant(1),
            },
        ),
    ]),
);

// FEM 81 — Aeolipile
pub(in crate::card::sets) static AEOLIPILE: CardRecord = CardRecord::new(
    cards::AEOLIPILE,
    "Aeolipile",
    CardArt::new("a09030ee-415c-45af-bf08-7623197a314f", "Heather Hudson"),
    CardSet::FallenEmpires,
    CardRules::new_artifact(mana_cost!("{2}")).with_abilities(&[
        AbilityDef::activated_with_targets(
            "{1}, {T}, Sacrifice this artifact: It deals 2 damage to any target.",
            &[
                AbilityCostDef::Mana(mana_cost!("{1}")),
                AbilityCostDef::TapSource,
                AbilityCostDef::SacrificeSource,
            ],
            &[AbilityTargetDef::exactly_one(
                AbilityTargetPredicate::AnyTarget,
            )],
            EffectDef::DealDamage {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                amount: ValueDef::Constant(2),
            },
        ),
    ]),
);

// FEM 82 — Balm of Restoration
// Audit: blocked — Needs modal activated abilities: modes are chosen only while casting a spell, so an activated ability has no mode selection to freeze. Both of its modes are available.

// FEM 83 — Conch Horn
// Audit: blocked — Needs ordered-library inspection, selection, and visibility handling for “{1}, {T}, Sacrifice this artifact: Draw two cards, then put a card from your hand on top of your library”.

// FEM 84 — Delif's Cone
// Audit: blocked — Needs a combat declaration or damage-assignment constraint for “{T}, Sacrifice this artifact: This turn, when target creature you control attacks and isn't blocked, you may gain life equal to its power. If you do, it assigns no combat damage this turn”.

// FEM 85 — Delif's Cube
// Audit: blocked — Needs a combat declaration or damage-assignment constraint for “{2}, {T}: This turn, when target creature you control attacks and isn't blocked, it assigns no combat damage this turn and you put a cube counter on this artifact”.

// FEM 86 — Draconian Cylix
// Audit: blocked — Needs a random discard as an activation cost; the discard cost lets its payer choose which cards leave hand.

// FEM 87 — Elven Lyre
pub(in crate::card::sets) static ELVEN_LYRE: CardRecord = CardRecord::new(
    cards::ELVEN_LYRE,
    "Elven Lyre",
    CardArt::new("c3a8cd72-04c0-46f7-a249-f1cecddfdc26", "Kaja Foglio"),
    CardSet::FallenEmpires,
    CardRules::new_artifact(mana_cost!("{2}")).with_abilities(&[
        AbilityDef::activated_with_targets(
            "{1}, {T}, Sacrifice this artifact: Target creature gets +2/+2 until end of turn.",
            &[
                AbilityCostDef::Mana(mana_cost!("{1}")),
                AbilityCostDef::TapSource,
                AbilityCostDef::SacrificeSource,
            ],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::HasType(CardType::Creature),
            )],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                effect: AppliedEffectDef::ModifyPowerToughness {
                    power: ValueDef::Constant(2),
                    toughness: ValueDef::Constant(2),
                },
                duration: EffectDurationDef::UntilEndOfTurn,
            },
        ),
    ]),
);

// FEM 88 — Implements of Sacrifice
pub(in crate::card::sets) static IMPLEMENTS_OF_SACRIFICE: CardRecord = CardRecord::new(
    cards::IMPLEMENTS_OF_SACRIFICE,
    "Implements of Sacrifice",
    CardArt::new(
        "aa5deb95-79a6-4398-b82a-c1df169550d9",
        "Margaret Organ-Kean",
    ),
    CardSet::FallenEmpires,
    CardRules::new_artifact(mana_cost!("{2}")).with_ability(AbilityDef::activated_mana(
        "{1}, {T}, Sacrifice this artifact: Add two mana of any one color.",
        &[
            AbilityCostDef::Mana(mana_cost!("{1}")),
            AbilityCostDef::TapSource,
            AbilityCostDef::SacrificeSource,
        ],
        EffectDef::AddMana(AddManaEffectDef::any_color().with_amount(2)),
    )),
);

// FEM 89 — Ring of Renewal
pub(in crate::card::sets) static RING_OF_RENEWAL: CardRecord = CardRecord::new(
    cards::RING_OF_RENEWAL,
    "Ring of Renewal",
    CardArt::new("a532d38a-809b-4132-8690-be15fe23afab", "Douglas Shuler"),
    CardSet::FallenEmpires,
    CardRules::new_artifact(mana_cost!("{5}")).with_abilities(&[AbilityDef::activated(
        "{5}, {T}: Discard a card at random, then draw two cards.",
        &[
            AbilityCostDef::Mana(mana_cost!("{5}")),
            AbilityCostDef::TapSource,
        ],
        EffectDef::Sequence(&[
            EffectDef::Discard {
                recipient: EffectRecipientDef::Controller,
                amount: ValueDef::Constant(1),
                selection: DiscardSelectionDef::Random,
            },
            EffectDef::DrawCards {
                recipient: EffectRecipientDef::Controller,
                amount: ValueDef::Constant(2),
            },
        ]),
    )]),
);

// FEM 90 — Spirit Shield
pub(in crate::card::sets) static SPIRIT_SHIELD: CardRecord = CardRecord::new(
    cards::SPIRIT_SHIELD,
    "Spirit Shield",
    CardArt::new("213d6e0d-5ec9-441e-a38d-50ce44583e4b", "Scott Kirschner"),
    CardSet::FallenEmpires,
    CardRules::new_artifact(mana_cost!("{3}")).with_abilities(&[
        AbilityDef::static_ability(
            "You may choose not to untap this artifact during your untap step.",
            EffectDef::Apply {
                recipient: EffectRecipientDef::Source,
                effect: AppliedEffectDef::MayChooseNotToUntap,
                duration: EffectDurationDef::WhileSourceRemainsInZone,
            },
        ),
        AbilityDef::activated_with_targets(
            "{2}, {T}: Target creature gets +0/+2 for as long as this artifact remains \
             tapped.",
            &[
                AbilityCostDef::Mana(mana_cost!("{2}")),
                AbilityCostDef::TapSource,
            ],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::HasType(CardType::Creature),
            )],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                effect: AppliedEffectDef::ModifyPowerToughness {
                    power: ValueDef::Constant(0),
                    toughness: ValueDef::Constant(2),
                },
                duration: EffectDurationDef::WhileSourceTapped,
            },
        ),
    ]),
);

// FEM 91 — Zelyon Sword
pub(in crate::card::sets) static ZELYON_SWORD: CardRecord = CardRecord::new(
    cards::ZELYON_SWORD,
    "Zelyon Sword",
    CardArt::new("4137160b-5248-4fbd-8ae8-25e9afd8fb5c", "Scott Kirschner"),
    CardSet::FallenEmpires,
    CardRules::new_artifact(mana_cost!("{3}")).with_abilities(&[
        AbilityDef::static_ability(
            "You may choose not to untap this artifact during your untap step.",
            EffectDef::Apply {
                recipient: EffectRecipientDef::Source,
                effect: AppliedEffectDef::MayChooseNotToUntap,
                duration: EffectDurationDef::WhileSourceRemainsInZone,
            },
        ),
        AbilityDef::activated_with_targets(
            "{3}, {T}: Target creature gets +2/+0 for as long as this artifact remains \
             tapped.",
            &[
                AbilityCostDef::Mana(mana_cost!("{3}")),
                AbilityCostDef::TapSource,
            ],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::HasType(CardType::Creature),
            )],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                effect: AppliedEffectDef::ModifyPowerToughness {
                    power: ValueDef::Constant(2),
                    toughness: ValueDef::Constant(0),
                },
                duration: EffectDurationDef::WhileSourceTapped,
            },
        ),
    ]),
);

// FEM 92 — Bottomless Vault
// Audit: blocked — Needs storage counters plus an arbitrary remove-any-number counter cost whose chosen count determines the mana produced.

// FEM 93 — Dwarven Hold
// Audit: blocked — Needs storage counters plus an arbitrary remove-any-number counter cost whose chosen count determines the mana produced.

// FEM 94 — Dwarven Ruins
pub(in crate::card::sets) static DWARVEN_RUINS: CardRecord = CardRecord::new(
    cards::DWARVEN_RUINS,
    "Dwarven Ruins",
    CardArt::new("0dfe1352-27be-4c99-a58f-b961f911f270", "Mark Poole"),
    CardSet::FallenEmpires,
    CardRules::new_land(&[]).with_abilities(&[
        AbilityDef::as_enters(
            "This land enters tapped.",
            ReplacementEffectDef::ModifyBattlefieldEntry(BattlefieldEntryModificationDef::Tapped),
        ),
        AbilityDef::activated_mana(
            "{T}: Add {R}.",
            &[AbilityCostDef::TapSource],
            EffectDef::AddMana(AddManaEffectDef::one(ManaColor::Red)),
        ),
        AbilityDef::activated_mana(
            "{T}, Sacrifice this land: Add {R}{R}.",
            &[AbilityCostDef::TapSource, AbilityCostDef::SacrificeSource],
            EffectDef::AddMana(AddManaEffectDef::one(ManaColor::Red).with_amount(2)),
        ),
    ]),
);

// FEM 95 — Ebon Stronghold
pub(in crate::card::sets) static EBON_STRONGHOLD: CardRecord = CardRecord::new(
    cards::EBON_STRONGHOLD,
    "Ebon Stronghold",
    CardArt::new("3fb2a11f-a8e4-4acf-871a-11171e3304ef", "Mark Poole"),
    CardSet::FallenEmpires,
    CardRules::new_land(&[]).with_abilities(&[
        AbilityDef::as_enters(
            "This land enters tapped.",
            ReplacementEffectDef::ModifyBattlefieldEntry(BattlefieldEntryModificationDef::Tapped),
        ),
        AbilityDef::activated_mana(
            "{T}: Add {B}.",
            &[AbilityCostDef::TapSource],
            EffectDef::AddMana(AddManaEffectDef::one(ManaColor::Black)),
        ),
        AbilityDef::activated_mana(
            "{T}, Sacrifice this land: Add {B}{B}.",
            &[AbilityCostDef::TapSource, AbilityCostDef::SacrificeSource],
            EffectDef::AddMana(AddManaEffectDef::one(ManaColor::Black).with_amount(2)),
        ),
    ]),
);

// FEM 96 — Havenwood Battleground
pub(in crate::card::sets) static HAVENWOOD_BATTLEGROUND: CardRecord = CardRecord::new(
    cards::HAVENWOOD_BATTLEGROUND,
    "Havenwood Battleground",
    CardArt::new("9028f200-80dd-4c53-877f-ea380ff417cb", "Mark Poole"),
    CardSet::FallenEmpires,
    CardRules::new_land(&[]).with_abilities(&[
        AbilityDef::as_enters(
            "This land enters tapped.",
            ReplacementEffectDef::ModifyBattlefieldEntry(BattlefieldEntryModificationDef::Tapped),
        ),
        AbilityDef::activated_mana(
            "{T}: Add {G}.",
            &[AbilityCostDef::TapSource],
            EffectDef::AddMana(AddManaEffectDef::one(ManaColor::Green)),
        ),
        AbilityDef::activated_mana(
            "{T}, Sacrifice this land: Add {G}{G}.",
            &[AbilityCostDef::TapSource, AbilityCostDef::SacrificeSource],
            EffectDef::AddMana(AddManaEffectDef::one(ManaColor::Green).with_amount(2)),
        ),
    ]),
);

// FEM 97 — Hollow Trees
// Audit: blocked — Needs storage counters plus an arbitrary remove-any-number counter cost whose chosen count determines the mana produced.

// FEM 98 — Icatian Store
// Audit: blocked — Needs storage counters plus an arbitrary remove-any-number counter cost whose chosen count determines the mana produced.

// FEM 99 — Rainbow Vale
// Audit: blocked — Needs duration-aware control-changing continuous effects for “{T}: Add one mana of any color. An opponent gains control of this land at the beginning of the next end step”.

// FEM 100 — Ruins of Trokair
pub(in crate::card::sets) static RUINS_OF_TROKAIR: CardRecord = CardRecord::new(
    cards::RUINS_OF_TROKAIR,
    "Ruins of Trokair",
    CardArt::new("4ce2e734-8cff-4bfe-85f8-17b3e1903f18", "Mark Poole"),
    CardSet::FallenEmpires,
    CardRules::new_land(&[]).with_abilities(&[
        AbilityDef::as_enters(
            "This land enters tapped.",
            ReplacementEffectDef::ModifyBattlefieldEntry(BattlefieldEntryModificationDef::Tapped),
        ),
        AbilityDef::activated_mana(
            "{T}: Add {W}.",
            &[AbilityCostDef::TapSource],
            EffectDef::AddMana(AddManaEffectDef::one(ManaColor::White)),
        ),
        AbilityDef::activated_mana(
            "{T}, Sacrifice this land: Add {W}{W}.",
            &[AbilityCostDef::TapSource, AbilityCostDef::SacrificeSource],
            EffectDef::AddMana(AddManaEffectDef::one(ManaColor::White).with_amount(2)),
        ),
    ]),
);

// FEM 101 — Sand Silos
// Audit: blocked — Needs storage counters plus an arbitrary remove-any-number counter cost whose chosen count determines the mana produced.

// FEM 102 — Svyelunite Temple
pub(in crate::card::sets) static SVYELUNITE_TEMPLE: CardRecord = CardRecord::new(
    cards::SVYELUNITE_TEMPLE,
    "Svyelunite Temple",
    CardArt::new("8b3fde62-ab21-459b-9c5d-01aa6fe1d08e", "Mark Poole"),
    CardSet::FallenEmpires,
    CardRules::new_land(&[]).with_abilities(&[
        AbilityDef::as_enters(
            "This land enters tapped.",
            ReplacementEffectDef::ModifyBattlefieldEntry(BattlefieldEntryModificationDef::Tapped),
        ),
        AbilityDef::activated_mana(
            "{T}: Add {U}.",
            &[AbilityCostDef::TapSource],
            EffectDef::AddMana(AddManaEffectDef::one(ManaColor::Blue)),
        ),
        AbilityDef::activated_mana(
            "{T}, Sacrifice this land: Add {U}{U}.",
            &[AbilityCostDef::TapSource, AbilityCostDef::SacrificeSource],
            EffectDef::AddMana(AddManaEffectDef::one(ManaColor::Blue).with_amount(2)),
        ),
    ]),
);

pub(in crate::card::sets) static CARDS: &[&CardRecord] = &[
    &COMBAT_MEDIC,
    &ICATIAN_JAVELINEERS,
    &ICATIAN_LIEUTENANT,
    &ICATIAN_MONEYCHANGER,
    &ICATIAN_PRIEST,
    &ICATIAN_SCOUT,
    &ICATIAN_TOWN,
    &ORDER_OF_LEITBUR,
    &HOMARID,
    &HOMARID_SHAMAN,
    &HOMARID_WARRIOR,
    &RIVER_MERFOLK,
    &SVYELUNITE_PRIEST,
    &VODALIAN_KNIGHTS,
    &VODALIAN_MAGE,
    &VODALIAN_SOLDIERS,
    &ARMOR_THRULL,
    &BASAL_THRULL,
    &BREEDING_PIT,
    &HYMN_TO_TOURACH,
    &MINDSTAB_THRULL,
    &NECRITE,
    &ORDER_OF_THE_EBON_HAND,
    &THRULL_CHAMPION,
    &THRULL_RETAINER,
    &DWARVEN_LIEUTENANT,
    &GOBLIN_CHIRURGEON,
    &GOBLIN_GRENADE,
    &ORCISH_CAPTAIN,
    &ELVEN_FORTRESS,
    &ELVISH_FARMER,
    &ELVISH_HUNTER,
    &FERAL_THALLID,
    &FUNGAL_BLOOM,
    &SPORE_FLOWER,
    &THALLID,
    &THALLID_DEVOURER,
    &THELONITE_DRUID,
    &THORN_THALLID,
    &AEOLIPILE,
    &ELVEN_LYRE,
    &IMPLEMENTS_OF_SACRIFICE,
    &RING_OF_RENEWAL,
    &SPIRIT_SHIELD,
    &ZELYON_SWORD,
    &DWARVEN_RUINS,
    &EBON_STRONGHOLD,
    &HAVENWOOD_BATTLEGROUND,
    &RUINS_OF_TROKAIR,
    &SVYELUNITE_TEMPLE,
];

pub(in crate::card::sets) static ADDITIONAL_PRINTINGS: &[PrintingRecord] = &[];
