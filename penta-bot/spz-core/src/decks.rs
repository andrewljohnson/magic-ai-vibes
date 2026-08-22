//! Built-in decklists for hypothesis sampling, loaded from
//! builtin-decklists.json (the same file the Python hosted policy uses).

use std::collections::HashMap;

pub type Decks = HashMap<String, HashMap<u16, u32>>;

pub fn load(path: &str) -> Result<Decks, String> {
    let raw: serde_json::Value =
        serde_json::from_str(&std::fs::read_to_string(path)
            .map_err(|e| e.to_string())?)
        .map_err(|e| e.to_string())?;
    let obj = raw.as_object().ok_or("decklists not an object")?;
    let mut out = Decks::new();
    for (name, zones) in obj {
        let main = zones.get("main").and_then(|m| m.as_object())
            .ok_or("deck has no main")?;
        let mut counts = HashMap::new();
        for (d, c) in main {
            counts.insert(d.parse::<u16>().map_err(|e| e.to_string())?,
                          c.as_u64().unwrap_or(0) as u32);
        }
        out.insert(name.clone(), counts);
    }
    Ok(out)
}

/// The decklists WITH their file order preserved, plus a name lookup.
///
/// `hosted_policy.classify_deck` scans the decklists in dict (= JSON
/// insertion) order and keeps the first best-overlap match on a strict
/// `>`, so two decks tied on overlap resolve to whichever appears first
/// in the file. A `HashMap` alone cannot reproduce that tie-break, hence
/// `order`. (`serde_json`'s `preserve_order` feature is what makes the
/// parsed object iterate in file order rather than alphabetically.)
pub struct DeckBook {
    pub order: Vec<(String, HashMap<u16, u32>)>,
    by_name: HashMap<String, usize>,
    empty: HashMap<u16, u32>,
}

impl DeckBook {
    pub fn load(path: &str) -> Result<Self, String> {
        let raw: serde_json::Value =
            serde_json::from_str(&std::fs::read_to_string(path)
                .map_err(|e| e.to_string())?)
            .map_err(|e| e.to_string())?;
        let obj = raw.as_object().ok_or("decklists not an object")?;
        let mut order = Vec::new();
        let mut by_name = HashMap::new();
        for (name, zones) in obj {
            let main = zones.get("main").and_then(|m| m.as_object())
                .ok_or("deck has no main")?;
            let mut counts = HashMap::new();
            for (d, c) in main {
                counts.insert(d.parse::<u16>().map_err(|e| e.to_string())?,
                              c.as_u64().unwrap_or(0) as u32);
            }
            by_name.insert(name.clone(), order.len());
            order.push((name.clone(), counts));
        }
        Ok(DeckBook { order, by_name, empty: HashMap::new() })
    }

    /// A deck's `{definition: count}` map; an unknown name reads as the
    /// empty decklist, matching Python's `decks.get(name, {})`.
    pub fn counts(&self, name: &str) -> &HashMap<u16, u32> {
        self.by_name.get(name).map_or(&self.empty, |&i| &self.order[i].1)
    }

    pub fn names(&self) -> impl Iterator<Item = &str> {
        self.order.iter().map(|(n, _)| n.as_str())
    }
}
