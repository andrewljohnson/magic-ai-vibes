//! The keyword tag carried by a checkpoint.
//!
//! Its variants are wire tags, so they are named for the printed keywords a
//! reader recognizes rather than for the engine's internal shape. Landwalk is
//! one parameterized keyword inside the engine and five separate tags here.

use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub(super) enum KeywordSnapshot {
    Flying,
    Trample,
    Haste,
    FirstStrike,
    DoubleStrike,
    Banding,
    Vigilance,
    Defender,
    Deathtouch,
    Lifelink,
    Reach,
    Flash,
    Hexproof,
    Shroud,
    Unleash,
    Intimidate,
    Undying,
    Indestructible,
    AttacksEachCombatIfAble,
    Mountainwalk,
    Forestwalk,
    Plainswalk,
    LegendaryLandwalk,
    Islandwalk,
    Swampwalk,
    ProtectionFromWhite,
    ProtectionFromBlue,
    ProtectionFromBlack,
    ProtectionFromRed,
    ProtectionFromGreen,
    ProtectionFromColorless,
}
