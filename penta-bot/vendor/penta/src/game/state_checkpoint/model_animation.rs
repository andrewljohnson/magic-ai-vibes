use serde::{Deserialize, Serialize};

use super::model_keyword::KeywordSnapshot;

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub(super) struct AnimationSnapshot {
    pub(super) power: i16,
    pub(super) toughness: i16,
    pub(super) types: String,
    pub(super) subtypes: Vec<String>,
    pub(super) all_creature_types: bool,
    pub(super) replaces_subtypes: bool,
    pub(super) loses_abilities: bool,
    pub(super) colors: Option<[bool; 5]>,
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub(super) struct UpkeepKeywordSnapshot {
    pub(super) seat: usize,
    pub(super) keyword: KeywordSnapshot,
}
