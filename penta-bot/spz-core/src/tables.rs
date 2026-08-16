//! Catalog-derived tables, loaded from the PINNED protocol-2 catalog
//! snapshot (pinned-catalog.json) so the 825-feature layout is
//! byte-compatible with every trained net. Every derivation mirrors
//! extractor.py exactly -- including choices the typed engine data
//! could improve on (e.g. rules-text flying), because LOCKSTEP with
//! the Python reference outranks elegance here.

use std::collections::{HashMap, HashSet};

pub const N_COST_BUCKETS: usize = 7;
pub const N_EXTRA_SCALARS: usize = 8;
pub const STEPS: [&str; 11] = [
    "Upkeep", "Draw", "PrecombatMain", "BeginningOfCombat",
    "DeclareAttackers", "DeclareBlockers", "CombatDamage", "EndOfCombat",
    "PostcombatMain", "End", "Cleanup",
];

pub struct Tables {
    pub def_slot: HashMap<u16, usize>,
    pub defs: usize,
    pub kind: HashMap<u16, String>,
    pub power: HashMap<u16, i64>,
    pub cost: HashMap<u16, i64>,
    pub flying: HashSet<u16>,
    pub invisible_upside: HashSet<u16>,
    pub size: usize, // v2 = 5*defs + 35 + defs + 14 + 8 (+ 2*defs if belief)
    pub v2_size: usize,     // the 825 base, before the belief block
    pub belief_base: usize, // where the belief block starts (== v2_size)
    pub belief: bool,       // append the 2*defs hidden-pool block?
}

fn converted_cost(cost: &serde_json::Value) -> i64 {
    if cost.is_null() {
        return 0;
    }
    let g = |k: &str| cost.get(k).and_then(|v| v.as_i64()).unwrap_or(0);
    let hybrid: i64 = cost
        .get("hybrid")
        .and_then(|h| h.as_array())
        .map(|arr| {
            arr.iter()
                .map(|e| e.get("count").and_then(|c| c.as_i64()).unwrap_or(0))
                .sum()
        })
        .unwrap_or(0);
    g("generic") + g("white") + g("blue") + g("black") + g("red")
        + g("green") + g("whiteRedHybrid") + hybrid
}

impl Tables {
    pub fn load(catalog_json: &str) -> Result<Self, String> {
        let cat: serde_json::Value =
            serde_json::from_str(catalog_json).map_err(|e| e.to_string())?;
        let cards = cat
            .get("cards")
            .and_then(|c| c.as_array())
            .ok_or("catalog has no cards")?;
        let mut legal: Vec<u16> = cards
            .iter()
            .filter(|c| c.get("legal").and_then(|l| l.as_bool()).unwrap_or(true))
            .map(|c| c["definition"].as_u64().unwrap() as u16)
            .collect();
        legal.sort_unstable();
        let def_slot: HashMap<u16, usize> =
            legal.iter().enumerate().map(|(i, d)| (*d, i)).collect();
        let defs = legal.len();
        let mut kind = HashMap::new();
        let mut power = HashMap::new();
        let mut cost = HashMap::new();
        let mut flying = HashSet::new();
        let mut invisible = HashSet::new();
        let markers = ["extra turn", "until end of turn", "prevent",
                       "protection from", "regenerate"];
        for c in cards {
            let d = c["definition"].as_u64().unwrap() as u16;
            kind.insert(d, c["kind"].as_str().unwrap_or("").to_string());
            power.insert(d, c.get("power").and_then(|p| p.as_i64()).unwrap_or(0));
            cost.insert(d, converted_cost(c.get("manaCost").unwrap_or(&serde_json::Value::Null)));
            let text = c.get("rulesText").and_then(|t| t.as_str()).unwrap_or("");
            if c.get("power").map(|p| !p.is_null()).unwrap_or(false)
                && text.starts_with("Flying")
            {
                flying.insert(d);
            }
            let low = text.to_lowercase();
            if markers.iter().any(|m| low.contains(m)) {
                invisible.insert(d);
            }
        }
        let v2_size = 5 * defs + 35 + defs + 2 * N_COST_BUCKETS + N_EXTRA_SCALARS;
        Ok(Tables { def_slot, defs, kind, power, cost, flying,
                    invisible_upside: invisible, size: v2_size, v2_size,
                    belief_base: v2_size, belief: false })
    }

    /// Turn the hidden-pool belief block on/off. On -> the vector grows by
    /// 2*defs (256 on the 128-def catalog), giving the 1081-feature schema
    /// that mirrors extractor.py's Extractor(version=2, belief=True).
    pub fn set_belief(&mut self, on: bool) {
        self.belief = on;
        self.belief_base = self.v2_size;
        self.size = if on { self.v2_size + 2 * self.defs } else { self.v2_size };
    }

    /// A defs-length count array in slot order from a {def: count} map,
    /// for the belief block's decklist input.
    pub fn deck_slots(&self, counts: &HashMap<u16, u32>) -> Vec<i32> {
        let mut v = vec![0i32; self.defs];
        for (d, c) in counts {
            if let Some(&i) = self.def_slot.get(d) {
                v[i] += *c as i32;
            }
        }
        v
    }

    pub fn cost_bucket(&self, def: u16) -> usize {
        (self.cost.get(&def).copied().unwrap_or(0).max(0) as usize)
            .min(N_COST_BUCKETS - 1)
    }

    pub fn is_creature_kind(&self, def: u16) -> bool {
        matches!(self.kind.get(&def).map(String::as_str),
                 Some("Creature") | Some("ArtifactCreature"))
    }
}
