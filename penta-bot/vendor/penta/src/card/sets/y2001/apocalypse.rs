//! Apocalypse cards used by the staged Premodern deck tranche.

use super::{CardRecord, PrintingRecord};
use crate::card::{
    AbilityDef, AbilityTargetDef, AbilityTargetPredicate, CardArt, CardComposition,
    CardEffectStatus, CardPart, CardRules, CardSet, CardStructure, DividedTotal, EffectDef,
    EffectRecipientDef, ManaColor, ObjectPredicateDef, PlayOptionDef, PlayerRelation, SpellForm,
    TriggerEventDef, TurnStepDef, ValueDef, abilities, cards,
};
use crate::{CardPartId, PlayOptionId, TargetIndex, mana_cost};

// APC 47 — Phyrexian Arena
pub(in crate::card::sets) static PHYREXIAN_ARENA: CardRecord = CardRecord::new(
    cards::PHYREXIAN_ARENA,
    "Phyrexian Arena",
    CardArt::new("84e19975-e3e1-453b-b902-a1b1fc1d8504", "Pete Venters"),
    CardSet::Apocalypse,
    CardRules::new_enchantment(mana_cost!("{1}{B}{B}")).with_ability(AbilityDef::triggered(
        "At the beginning of your upkeep, you draw a card and you lose 1 life.",
        TriggerEventDef::StepBegins {
            step: TurnStepDef::Upkeep,
            player: PlayerRelation::You,
        },
        EffectDef::Sequence(&[
            EffectDef::DrawCards {
                recipient: EffectRecipientDef::Controller,
                amount: ValueDef::Constant(1),
            },
            EffectDef::LoseLife {
                recipient: EffectRecipientDef::Controller,
                amount: ValueDef::Constant(1),
            },
        ]),
    )),
);

// APC 126 — Vindicate
pub(in crate::card::sets) static VINDICATE: CardRecord = CardRecord::new(
    cards::VINDICATE,
    "Vindicate",
    CardArt::new("2a1bfefd-dae8-49e9-9d56-cc852e3dc93b", "Brian Snõddy"),
    CardSet::Apocalypse,
    CardRules::new_sorcery(mana_cost!("{1}{W}{B}")).with_ability(AbilityDef::destroy_target(
        "Destroy target permanent.",
        &AbilityTargetDef::exactly_one_permanent(ObjectPredicateDef::Any),
        true,
    )),
);

static FIRE_TARGETS: [AbilityTargetDef; 1] = [AbilityTargetDef {
    predicate: AbilityTargetPredicate::AnyTarget,
    minimum: 1,
    maximum: 2,
    divided_total: Some(DividedTotal::Fixed(2)),
}];

const fn fire_rules() -> CardRules {
    CardRules::new_instant(mana_cost!("{1}{R}")).with_ability(AbilityDef::spell_with_targets(
        "Fire deals 2 damage divided as you choose among one or two targets.",
        &FIRE_TARGETS,
        EffectDef::DealDamage {
            recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
            amount: ValueDef::DividedAmongTargets,
        },
    ))
}

static ICE_TARGETS: [AbilityTargetDef; 1] = [AbilityTargetDef::exactly_one_permanent(
    ObjectPredicateDef::Any,
)];

static ICE_EFFECTS: [EffectDef; 2] = [
    EffectDef::Tap {
        object: EffectRecipientDef::Target(TargetIndex::PRIMARY),
    },
    EffectDef::DrawCards {
        recipient: EffectRecipientDef::Controller,
        amount: ValueDef::Constant(1),
    },
];

const fn ice_rules() -> CardRules {
    CardRules::new_instant(mana_cost!("{1}{U}")).with_ability(AbilityDef::spell_with_targets(
        "Tap target permanent.\nDraw a card.",
        &ICE_TARGETS,
        EffectDef::Sequence(&ICE_EFFECTS),
    ))
}

fn fire_ice_composition() -> CardComposition {
    let fire = fire_rules();
    let ice = ice_rules();
    CardComposition {
        parts: vec![
            CardPart::new(CardPartId::PRIMARY, "Fire", fire),
            CardPart::new(CardPartId(1), "Ice", ice),
        ],
        structure: CardStructure::Split {
            parts: vec![CardPartId::PRIMARY, CardPartId(1)],
            fused: None,
        },
        play_options: vec![
            PlayOptionDef::cast(
                PlayOptionId::DEFAULT,
                "Fire",
                SpellForm::Part(CardPartId::PRIMARY),
                fire.mana_cost().expect("Fire has a printed mana cost"),
                CardEffectStatus::Implemented,
            ),
            PlayOptionDef::cast(
                PlayOptionId(1),
                "Ice",
                SpellForm::Part(CardPartId(1)),
                ice.mana_cost().expect("Ice has a printed mana cost"),
                CardEffectStatus::Implemented,
            ),
        ],
    }
    .with_derived_spell_targets()
}

// APC 128 — Fire // Ice
pub(in crate::card::sets) static FIRE_ICE: CardRecord = CardRecord::new(
    cards::FIRE_ICE,
    "Fire // Ice",
    CardArt::new(
        "f98f4538-5b5b-475d-b98f-49d01dae6f04",
        "David Martin & Franz Vohwinkel",
    ),
    CardSet::Apocalypse,
    fire_rules(),
)
.with_composition(fire_ice_composition);

// APC 140 — Caves of Koilos
pub(in crate::card::sets) static CAVES_OF_KOILOS: CardRecord = CardRecord::new(
    cards::CAVES_OF_KOILOS,
    "Caves of Koilos",
    CardArt::new("144dd08e-451e-4438-b572-7a138e1a15f3", "Jim Nelson"),
    CardSet::Apocalypse,
    CardRules::new_land(&[]).with_abilities(&abilities::pain_land(
        "{T}: Add {W} or {B}. This land deals 1 damage to you.",
        &[ManaColor::White, ManaColor::Black],
    )),
);

// APC 141 — Llanowar Wastes
pub(in crate::card::sets) static LLANOWAR_WASTES: CardRecord = CardRecord::new(
    cards::LLANOWAR_WASTES,
    "Llanowar Wastes",
    CardArt::new("610b7cd5-5532-45a9-acfe-24a818034d1c", "Rob Alexander"),
    CardSet::Apocalypse,
    CardRules::new_land(&[]).with_abilities(&abilities::pain_land(
        "{T}: Add {B} or {G}. This land deals 1 damage to you.",
        &[ManaColor::Black, ManaColor::Green],
    )),
);

// APC 143 — Yavimaya Coast
pub(in crate::card::sets) static YAVIMAYA_COAST: CardRecord = CardRecord::new(
    cards::YAVIMAYA_COAST,
    "Yavimaya Coast",
    CardArt::new("177ee102-d981-4fc3-9f09-9dd07755f22c", "Anthony S. Waters"),
    CardSet::Apocalypse,
    CardRules::new_land(&[]).with_abilities(&abilities::pain_land(
        "{T}: Add {G} or {U}. This land deals 1 damage to you.",
        &[ManaColor::Green, ManaColor::Blue],
    )),
);

pub(in crate::card::sets) static CARDS: &[&CardRecord] = &[
    &PHYREXIAN_ARENA,
    &VINDICATE,
    &FIRE_ICE,
    &CAVES_OF_KOILOS,
    &LLANOWAR_WASTES,
    &YAVIMAYA_COAST,
];

pub(in crate::card::sets) static ADDITIONAL_PRINTINGS: &[PrintingRecord] = &[];
