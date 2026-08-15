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
