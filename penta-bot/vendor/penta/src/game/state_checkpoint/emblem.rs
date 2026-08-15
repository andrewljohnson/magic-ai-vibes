use serde_json::Value;

use crate::{CardDefinitionId, CardPartId, GameObjectId};

use super::super::{CharacteristicSource, ContinuousEffectTimestamp, Game, Permanent};
use super::model::EmblemSnapshot;
use super::stack::parse_ability_origin;
use super::{array, card, field, seat_value, u32_field};

pub(super) fn emblem_snapshot(emblem: &Permanent) -> EmblemSnapshot {
    EmblemSnapshot {
        object_id: emblem.card.id.0,
        definition: emblem.card.definition.0,
        owner: emblem.card.owner.index(),
        presented_part_id: emblem.presented.0,
        timestamp: emblem.timestamp.0,
        entered_controller_turn: emblem.entered_controller_turn,
    }
}

pub(super) fn parse_emblems(
    observation: &Value,
    snapshots: &[EmblemSnapshot],
    game: &Game,
) -> Result<Vec<Permanent>, String> {
    let visible = array(field(observation, "emblems")?)?;
    if visible.len() != snapshots.len() {
        return Err("checkpoint emblems do not match observation".into());
    }
    visible
        .iter()
        .zip(snapshots)
        .map(|(shown, state)| {
            let id = GameObjectId(u32_field(shown, "objectId")?);
            if id.0 != state.object_id {
                return Err("checkpoint emblem id does not match observation".into());
            }
            let definition = CardDefinitionId(state.definition);
            let owner = player(state.owner)?;
            let controller = seat_value(field(shown, "controller")?)?;
            let presented = CardPartId(state.presented_part_id);
            let mut emblem = Permanent::entering(
                card(id, definition, owner, &game.catalog)?,
                presented,
                controller,
                state.entered_controller_turn,
            );
            emblem.card.characteristics = CharacteristicSource::Ability(definition);
            emblem.timestamp = ContinuousEffectTimestamp(state.timestamp);
            emblem.emblem_source = Some(parse_ability_origin(field(shown, "sourceAbility")?)?);
            Ok(emblem)
        })
        .collect()
}

fn player(index: usize) -> Result<crate::PlayerId, String> {
    match index {
        0 => Ok(crate::PlayerId::One),
        1 => Ok(crate::PlayerId::Two),
        _ => Err("seat index must be 0 or 1".into()),
    }
}
