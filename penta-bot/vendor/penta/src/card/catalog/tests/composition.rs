use super::*;

#[test]
fn part_and_play_option_ids_are_unique_within_a_definition() {
    let mut duplicate_part = definition(1, "Test Card", CardSet::Alpha);
    duplicate_part.parts.push(duplicate_part.parts[0].clone());
    assert_eq!(
        error(duplicate_part),
        CatalogError::DuplicatePartId {
            definition: CardDefinitionId(1),
            part: CardPartId::PRIMARY,
        }
    );

    let mut duplicate_option = definition(1, "Test Card", CardSet::Alpha);
    duplicate_option
        .play_options
        .push(duplicate_option.play_options[0].clone());
    assert_eq!(
        error(duplicate_option),
        CatalogError::DuplicatePlayOptionId {
            definition: CardDefinitionId(1),
            option: PlayOptionId::DEFAULT,
        }
    );
}

#[test]
fn incoherent_rules_cannot_enter_the_catalog() {
    let invalid_rules = crate::CardRules::new_land(&[])
        .with_printed_mana_cost_for_test(PrintedManaCost::Cost(ManaCost::default()));

    let mut invalid_compatibility_view = definition(1, "Test Card", CardSet::Alpha);
    invalid_compatibility_view.rules = invalid_rules;
    assert_eq!(
        error(invalid_compatibility_view),
        CatalogError::IncoherentCardRules {
            definition: CardDefinitionId(1),
            part: CardPartId::PRIMARY,
            explanation: "a land cannot have a printed mana cost",
        }
    );

    let mut invalid_part = definition(1, "Test Card", CardSet::Alpha);
    invalid_part.parts[0].rules = invalid_rules;
    assert_eq!(
        error(invalid_part),
        CatalogError::IncoherentCardRules {
            definition: CardDefinitionId(1),
            part: CardPartId::PRIMARY,
            explanation: "a land cannot have a printed mana cost",
        }
    );
}
#[test]
fn compatibility_rules_must_match_the_primary_part() {
    let mut card = definition(1, "Test Card", CardSet::Alpha);
    card.rules = crate::CardRules::new_artifact(ManaCost::default());

    assert_eq!(
        error(card),
        CatalogError::MismatchedPrimaryRules {
            definition: CardDefinitionId(1),
            part: CardPartId::PRIMARY,
        }
    );
}
