use std::collections::HashSet;

use crate::card::catalog::CatalogError;
use crate::card::{
    CardDefinition, CardStructure, ModeSetDef, PlayActionKind, PlayOptionDef, SpellForm,
    TargetSlotDef,
};
use crate::{AdditionalCostId, CardPartId, ModeId, TargetSlotId};

pub(super) fn structure_parts(
    definition: &CardDefinition,
) -> Result<Vec<CardPartId>, CatalogError> {
    let parts = match &definition.structure {
        CardStructure::Single { main } => vec![*main],
        CardStructure::Split { parts, .. } => {
            if parts.len() < 2 {
                return Err(CatalogError::InvalidSplitPartCount {
                    definition: definition.id,
                    actual: parts.len(),
                });
            }
            parts.clone()
        }
        CardStructure::Room {
            doors,
            combined,
            locked,
        } => {
            let mut parts = doors.clone();
            parts.push(*combined);
            parts.push(*locked);
            parts
        }
        CardStructure::Flip { normal, flipped } => vec![*normal, *flipped],
        CardStructure::DoubleFaced { front, back, .. } => vec![*front, *back],
        CardStructure::AlternateSpell {
            main, alternate, ..
        } => vec![*main, *alternate],
        CardStructure::MeldPart { front, .. } => vec![*front],
    };

    let mut seen = HashSet::new();
    for part in &parts {
        if !seen.insert(*part) {
            return Err(CatalogError::DuplicateStructurePart {
                definition: definition.id,
                part: *part,
            });
        }
    }
    Ok(parts)
}

pub(super) fn validate_spell_form(
    definition: &CardDefinition,
    option: &PlayOptionDef,
    defined_parts: &HashSet<CardPartId>,
    structure_parts: &[CardPartId],
) -> Result<(), CatalogError> {
    let form_parts = match &option.form {
        SpellForm::Part(part) => vec![*part],
        SpellForm::Combined(parts) => {
            if parts.is_empty() {
                return Err(CatalogError::EmptySpellForm {
                    definition: definition.id,
                    option: option.id,
                });
            }
            let mut seen = HashSet::new();
            for part in parts {
                if !seen.insert(*part) {
                    return Err(CatalogError::DuplicateSpellFormPart {
                        definition: definition.id,
                        option: option.id,
                        part: *part,
                    });
                }
            }
            parts.clone()
        }
    };

    for part in form_parts {
        if !defined_parts.contains(&part) {
            return Err(CatalogError::UndefinedSpellFormPart {
                definition: definition.id,
                option: option.id,
                part,
            });
        }
        if !structure_parts.contains(&part) {
            return Err(CatalogError::SpellFormPartOutsideStructure {
                definition: definition.id,
                option: option.id,
                part,
            });
        }
    }
    Ok(())
}

pub(super) fn validate_cost_ids(
    definition: &CardDefinition,
    option: &PlayOptionDef,
    additional_costs: &mut HashSet<AdditionalCostId>,
) -> Result<(), CatalogError> {
    // Alternative identities are interpreted together with their play option.
    // In particular, alternative-cast clauses on two split-card parts may
    // have the same positional AbilityId and therefore the same projected ID.
    let mut alternative_costs = HashSet::new();
    for cost in &option.alternative_costs {
        if !alternative_costs.insert(cost.id) {
            return Err(CatalogError::DuplicateAlternativeCostId {
                definition: definition.id,
                option: option.id,
                cost: cost.id,
            });
        }
    }
    for cost in &option.additional_costs {
        if !additional_costs.insert(cost.id) {
            return Err(CatalogError::DuplicateAdditionalCostId {
                definition: definition.id,
                cost: cost.id,
            });
        }
    }
    Ok(())
}

pub(super) fn validate_modes_and_targets(
    definition: &CardDefinition,
    option: &PlayOptionDef,
) -> Result<(), CatalogError> {
    validate_target_slots(definition, option, None, &option.targets)?;

    let Some(mode_set) = &option.modes else {
        return Ok(());
    };
    validate_mode_selection_bounds(definition, option, mode_set)?;

    let mut option_modes = HashSet::new();
    for mode in &mode_set.modes {
        if !option_modes.insert(mode.id) {
            return Err(CatalogError::DuplicateModeId {
                definition: definition.id,
                option: option.id,
                mode: mode.id,
            });
        }
    }

    for (index, mode) in mode_set.modes.iter().enumerate() {
        let expected = ModeId::from_index(index)
            .expect("validated mode sets cannot exceed the positional ID range");
        if mode.id != expected {
            return Err(CatalogError::NonPositionalModeId {
                definition: definition.id,
                option: option.id,
                expected,
                actual: mode.id,
            });
        }
        validate_target_slots(definition, option, Some(mode.id), &mode.targets)?;
    }

    let mut mode_target_counts = mode_set
        .modes
        .iter()
        .map(|mode| mode.targets.len())
        .collect::<Vec<_>>();
    let selected_target_count = if mode_set.may_repeat {
        mode_target_counts
            .into_iter()
            .max()
            .unwrap_or(0)
            .saturating_mul(usize::from(mode_set.maximum))
    } else {
        mode_target_counts.sort_unstable_by(|left, right| right.cmp(left));
        mode_target_counts
            .into_iter()
            .take(usize::from(mode_set.maximum))
            .fold(0, usize::saturating_add)
    };
    let instantiated = option.targets.len().saturating_add(selected_target_count);
    if instantiated > usize::from(u8::MAX) + 1 {
        return Err(CatalogError::TooManyInstantiatedTargets {
            definition: definition.id,
            option: option.id,
            count: instantiated,
        });
    }
    Ok(())
}

fn validate_mode_selection_bounds(
    definition: &CardDefinition,
    option: &PlayOptionDef,
    mode_set: &ModeSetDef,
) -> Result<(), CatalogError> {
    if mode_set.modes.is_empty() {
        return Err(CatalogError::EmptyModeSet {
            definition: definition.id,
            option: option.id,
        });
    }
    if mode_set.modes.len() > usize::from(u8::MAX) + 1 {
        return Err(CatalogError::TooManyModes {
            definition: definition.id,
            option: option.id,
            count: mode_set.modes.len(),
        });
    }
    if mode_set.minimum > mode_set.maximum {
        return Err(CatalogError::InvalidModeBounds {
            definition: definition.id,
            option: option.id,
            minimum: mode_set.minimum,
            maximum: mode_set.maximum,
        });
    }
    if mode_set.maximum == 0 {
        return Err(CatalogError::ZeroModeMaximum {
            definition: definition.id,
            option: option.id,
        });
    }
    // The conditional maximum is the one a "you may choose two instead"
    // clause can actually reach, so it is what has to fit the printed modes.
    let maximum = mode_set
        .conditional_maximum
        .map_or(mode_set.maximum, |conditional| {
            mode_set.maximum.max(conditional.maximum)
        });
    if !mode_set.may_repeat && usize::from(maximum) > mode_set.modes.len() {
        return Err(CatalogError::TooManyModesWithoutRepetition {
            definition: definition.id,
            option: option.id,
            maximum,
            available: mode_set.modes.len(),
        });
    }
    Ok(())
}

#[allow(clippy::too_many_lines)]
fn validate_target_slots(
    definition: &CardDefinition,
    option: &PlayOptionDef,
    mode: Option<ModeId>,
    slots: &[TargetSlotDef],
) -> Result<(), CatalogError> {
    if slots.len() > usize::from(u8::MAX) + 1 {
        return Err(CatalogError::TooManyTargetSlots {
            definition: definition.id,
            option: option.id,
            mode,
            count: slots.len(),
        });
    }
    for (position, slot) in slots.iter().enumerate() {
        let expected = TargetSlotId::from_index(position)
            .expect("the target slot count was validated before assigning positional IDs");
        if slot.id != expected {
            return Err(CatalogError::NonPositionalTargetSlot {
                definition: definition.id,
                option: option.id,
                mode,
                expected,
                actual: slot.id,
            });
        }
        if slot.minimum > slot.maximum {
            return Err(CatalogError::InvalidTargetBounds {
                definition: definition.id,
                option: option.id,
                mode,
                slot: slot.id,
                minimum: slot.minimum,
                maximum: slot.maximum,
            });
        }
    }
    Ok(())
}

pub(super) fn validate_fused_option(definition: &CardDefinition) -> Result<(), CatalogError> {
    let CardStructure::Split { parts, fused } = &definition.structure else {
        if let Some(option) = definition
            .play_options
            .iter()
            .find(|option| matches!(option.form, SpellForm::Combined(_)))
        {
            return Err(CatalogError::UnexpectedCombinedSpellForm {
                definition: definition.id,
                option: option.id,
            });
        }
        return Ok(());
    };

    for option in &definition.play_options {
        if matches!(option.form, SpellForm::Combined(_)) && Some(option.id) != *fused {
            return Err(CatalogError::UnexpectedCombinedSpellForm {
                definition: definition.id,
                option: option.id,
            });
        }
    }

    let Some(fused) = fused else {
        return Ok(());
    };
    let Some(option) = definition.play_option(*fused) else {
        return Err(CatalogError::MissingFusedPlayOption {
            definition: definition.id,
            option: *fused,
        });
    };
    if option.action != PlayActionKind::CastSpell
        || option.form != SpellForm::Combined(parts.clone())
    {
        return Err(CatalogError::InvalidFusedPlayOption {
            definition: definition.id,
            option: *fused,
            expected: parts.clone(),
            actual: option.form.clone(),
            actual_action: option.action,
        });
    }
    Ok(())
}
