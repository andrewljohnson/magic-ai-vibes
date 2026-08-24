//! Permissions to play a card from exile, on the wire and back.
//!
//! Split out of the parent module for the source-size budget.

use super::model::ExilePlayPermissionSnapshot;
use super::{ExilePlayCost, ExilePlayPermission, GameObjectId, ManaCost, mana_cost_snapshot, wire};
use crate::card::ExilePlayConditionDef;

pub(super) fn permission_snapshot(permission: &ExilePlayPermission) -> ExilePlayPermissionSnapshot {
    ExilePlayPermissionSnapshot {
        card: permission.card.0,
        player: permission.player.index(),
        cost: permission.cost.label().to_owned(),
        until_end_of_turn: permission
            .until_end_of_turn
            .map(|(player, turn)| (player.index(), turn)),
        adventure_return_only: permission.adventure_return_only,
        surcharge: (permission.surcharge != ManaCost::default())
            .then(|| mana_cost_snapshot(permission.surcharge)),
        not_before_turn: permission
            .not_before_turn
            .map(|(player, turn)| (player.index(), turn)),
        face_down: permission.face_down,
        hidden_only: permission.hidden_only,
        spend_any_color: permission.spend_any_color,
        attacked_with_subtype: permission.condition.map(|condition| match condition {
            ExilePlayConditionDef::AttackedWithSubtypeThisTurn(subtype) => subtype.to_owned(),
        }),
        until_holder_end_step: permission
            .until_holder_end_step
            .map(|(player, turn)| (player.index(), turn)),
    }
}

pub(super) fn parse_permission(
    permission: &ExilePlayPermissionSnapshot,
) -> Result<ExilePlayPermission, String> {
    Ok(ExilePlayPermission {
        card: GameObjectId(permission.card),
        player: wire::player_from_index(permission.player)?,
        cost: ExilePlayCost::from_label(&permission.cost).ok_or("unknown exile-play cost")?,
        until_end_of_turn: match permission.until_end_of_turn {
            Some((player, turn)) => Some((wire::player_from_index(player)?, turn)),
            None => None,
        },
        adventure_return_only: permission.adventure_return_only,
        surcharge: permission
            .surcharge
            .as_ref()
            .map_or_else(ManaCost::default, super::mana_cost_from_snapshot),
        not_before_turn: match permission.not_before_turn {
            Some((player, turn)) => Some((wire::player_from_index(player)?, turn)),
            None => None,
        },
        face_down: permission.face_down,
        hidden_only: permission.hidden_only,
        spend_any_color: permission.spend_any_color,
        condition: match permission.attacked_with_subtype.as_deref() {
            // Read back as the catalog's own name for the type, which is
            // what the permission holds.
            Some(subtype) => Some(ExilePlayConditionDef::AttackedWithSubtypeThisTurn(
                crate::card::creature_type_name(subtype)
                    .ok_or("unknown creature type in an exile-play permission")?,
            )),
            None => None,
        },
        until_holder_end_step: match permission.until_holder_end_step {
            Some((player, turn)) => Some((wire::player_from_index(player)?, turn)),
            None => None,
        },
    })
}
