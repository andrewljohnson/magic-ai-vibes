use super::super::normalize_name;
use crate::card;

#[test]
fn an_accented_name_is_found_by_either_spelling() {
    let catalog = card::catalog().expect("built-in catalog");
    let printed = catalog
        .find_by_name("Juzám Djinn")
        .expect("the name as printed on the card resolves");
    let typed = catalog
        .find_by_name("Juzam Djinn")
        .expect("the name as players type it resolves");

    assert_eq!(printed, typed);
    assert_eq!(
        catalog.get(printed).expect("definition").name,
        "Juzám Djinn",
        "the catalog stores the printed name; folding only affects lookup"
    );
}

#[test]
fn folding_spells_out_ligatures_instead_of_dropping_them() {
    // Æ is the case that a single-character fold gets wrong: mapping it to
    // "a" would make the unaccented spelling stop matching, which is the
    // opposite of what folding is for.
    assert_eq!(normalize_name("Æther Vial"), "aether vial");
    assert_eq!(normalize_name("Aether Vial"), "aether vial");
}

#[test]
fn folding_covers_the_accents_magic_actually_prints() {
    for (printed, plain) in [
        ("Juzám Djinn", "Juzam Djinn"),
        ("Márton Stromgald", "Marton Stromgald"),
        ("Lim-Dûl's Vault", "Lim-Dul's Vault"),
        ("Séance", "Seance"),
        ("Jötun Grunt", "Jotun Grunt"),
        ("Ærathi Berserker", "Aerathi Berserker"),
    ] {
        assert_eq!(
            normalize_name(printed),
            normalize_name(plain),
            "{printed} and {plain} must resolve to the same card"
        );
    }
}

#[test]
fn normalization_still_trims_and_lowercases() {
    assert_eq!(normalize_name("  Black Lotus  "), "black lotus");
    assert_eq!(normalize_name("BLACK LOTUS"), "black lotus");
}
