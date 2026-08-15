//! Reading one option of a pending decision back from a checkpoint.

use serde_json::Value;

use super::super::wire::{array, field, str_field, u32_field, usize_field};
use super::parse_decision_zone;
use crate::game::DecisionOption;
use crate::ids::{CardDefinitionId, GameObjectId};

pub(super) fn parse_option(value: &Value) -> Result<DecisionOption, String> {
    let parse_card = |value: &Value| {
        Ok((
            GameObjectId(u32_field(value, "objectId")?),
            CardDefinitionId(
                u16::try_from(usize_field(value, "definition")?)
                    .map_err(|_| "decision card definition is too large")?,
            ),
        ))
    };
    Ok(DecisionOption {
        id: u32_field(value, "id")?,
        label: str_field(value, "label")?.to_owned(),
        card: value
            .get("card")
            .filter(|value| !value.is_null())
            .map(parse_card)
            .transpose()?,
        members: array(field(value, "members")?)?
            .iter()
            .map(parse_card)
            .collect::<Result<Vec<_>, String>>()?,
        ability_text: value
            .get("abilityText")
            .and_then(Value::as_str)
            .map(str::to_owned),
        zone: parse_decision_zone(str_field(value, "zone")?)?,
    })
}
