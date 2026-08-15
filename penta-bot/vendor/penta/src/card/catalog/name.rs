/// Folds a printed card name to the key both the catalog and a lookup use.
///
/// Magic prints accented names — Juzám Djinn, Márton Stromgald, Lim-Dûl —
/// and decklists, search boxes, and bot authors overwhelmingly type them
/// without the accents. Folding here means the catalog can store the name as
/// printed while every spelling still resolves to the same card.
pub(super) fn normalize_name(name: &str) -> String {
    let mut normalized = String::with_capacity(name.len());

    for lowered in name.trim().chars().flat_map(char::to_lowercase) {
        match ascii_fold(lowered) {
            Some(replacement) => normalized.push_str(replacement),
            None => normalized.push(lowered),
        }
    }

    normalized
}

/// The ASCII spelling of one lowercase Latin-1 letter, or `None` when the
/// character is already the key form.
///
/// Ligatures and the letters that conventionally spell out are handled as
/// strings rather than single characters, so Æther folds to `aether` and not
/// to `ather` — otherwise typing the unaccented name would stop matching,
/// which is the whole point of folding.
fn ascii_fold(lowered: char) -> Option<&'static str> {
    Some(match lowered {
        'à' | 'á' | 'â' | 'ã' | 'ä' | 'å' => "a",
        'æ' => "ae",
        'ç' => "c",
        'è' | 'é' | 'ê' | 'ë' => "e",
        'ì' | 'í' | 'î' | 'ï' => "i",
        'ð' => "d",
        'ñ' => "n",
        'ò' | 'ó' | 'ô' | 'õ' | 'ö' | 'ø' => "o",
        'ù' | 'ú' | 'û' | 'ü' => "u",
        'ý' | 'ÿ' => "y",
        'þ' => "th",
        'ß' => "ss",
        _ => return None,
    })
}
