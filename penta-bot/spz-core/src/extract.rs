//! The 825-feature v2 schema from a typed observation -- a line-by-line
//! port of extractor.py's features() (v1 counts + 35 scalars +
//! castability/race block), against the PINNED catalog tables. The
//! castability block reads the same protocol action list the JSON
//! carries, via `protocol_actions`.

use penta::protocol::protocol_actions;
use penta::{Action, PlayerId, PlayerObservation};

use crate::tables::{Tables, N_COST_BUCKETS};
#[cfg(test)]
use crate::tables::STEPS;

fn step_index(step: penta::Step) -> usize {
    // Mirrors STEP_INDEX over the JSON step names; the enum order in
    // event.rs matches STEPS exactly (asserted in tests).
    step as usize
}

/// `decks` are the two seats' decklist count arrays in slot order, indexed
/// by SEAT (0 = p1, 1 = p2); only read when `t.belief` is on. Non-belief
/// callers may pass any placeholder (e.g. two empty vecs).
pub fn features(obs: &PlayerObservation, pregame: bool, t: &Tables,
                decks: &[Vec<i32>; 2]) -> Vec<f32> {
    let mut v = vec![0f32; t.size];
    let c = t.defs;
    let me = obs.viewer;
    let mi = if me == PlayerId::One { 0 } else { 1 };
    let oi = 1 - mi;

    // -- v1 per-definition counts ---------------------------------------
    for (_, def) in &obs.hand {
        if let Some(&i) = t.def_slot.get(&def.0) {
            v[i] += 0.25;
        }
    }
    for perm in &obs.battlefield {
        let base = if perm.controller == me { c } else { 2 * c };
        if let Some(&i) = t.def_slot.get(&perm.definition.0) {
            v[base + i] += 0.25;
        }
    }
    for (_, def) in &obs.graveyards[mi] {
        if let Some(&i) = t.def_slot.get(&def.0) {
            v[3 * c + i] += 0.25;
        }
    }
    for (_, def) in &obs.graveyards[oi] {
        if let Some(&i) = t.def_slot.get(&def.0) {
            v[4 * c + i] += 0.25;
        }
    }

    // -- 35 global scalars (extractor.py _scalars order) ----------------
    let mut agg = [[0f32; 6]; 2]; // [me,opp][creatures,power,tough,untapped,untapped_lands,attackers]
    for perm in &obs.battlefield {
        let side = if perm.controller == me { 0 } else { 1 };
        let kind = t.kind.get(&perm.definition.0).map(String::as_str).unwrap_or("");
        let is_creature = perm.power.is_some()
            || kind == "Creature" || kind == "ArtifactCreature";
        if is_creature {
            agg[side][0] += 1.0;
            agg[side][1] += f32::from(perm.power.unwrap_or(0));
            agg[side][2] += f32::from(perm.toughness.unwrap_or(0));
            if !perm.tapped {
                agg[side][3] += 1.0;
            }
            if perm.attacking {
                agg[side][5] += 1.0;
            }
        } else if kind == "Land" && !perm.tapped {
            agg[side][4] += 1.0;
        }
    }
    let s = 5 * c;
    let pool = |p: &penta::ManaPool| -> f32 {
        f32::from(p.white + p.blue + p.black + p.red + p.green + p.colorless)
    };
    v[s] = (obs.turn.min(40) as f32) / 20.0;
    v[s + 1] = if pregame { 1.0 } else { 0.0 };
    v[s + 2] = if obs.active_player == me { 1.0 } else { 0.0 };
    v[s + 3 + step_index(obs.step)] = 1.0;
    v[s + 14] = f32::from(obs.life_totals[mi]) / 20.0;
    v[s + 15] = f32::from(obs.life_totals[oi]) / 20.0;
    v[s + 16] = (obs.hand.len() as f32) / 7.0;
    v[s + 17] = (obs.opponent_hand_size as f32) / 7.0;
    v[s + 18] = (obs.library_sizes[mi] as f32) / 60.0;
    v[s + 19] = (obs.library_sizes[oi] as f32) / 60.0;
    v[s + 20] = pool(&obs.mana_pools[mi]).min(10.0) / 5.0;
    v[s + 21] = pool(&obs.mana_pools[oi]).min(10.0) / 5.0;
    v[s + 22] = (obs.stack.len() as f32) / 4.0;
    for side in 0..2 {
        let b = s + 23 + side * 6;
        v[b] = agg[side][0] / 8.0;
        v[b + 1] = agg[side][1] / 16.0;
        v[b + 2] = agg[side][2] / 16.0;
        v[b + 3] = agg[side][3] / 8.0;
        v[b + 4] = agg[side][4] / 8.0;
        v[b + 5] = agg[side][5] / 8.0;
    }

    // -- v2 castability + race block ------------------------------------
    let base = 5 * c + 35;
    let hand_def: std::collections::HashMap<u32, u16> = obs
        .hand
        .iter()
        .map(|(id, d)| (id.0, d.0))
        .collect();
    for (_, d) in &obs.hand {
        v[base + c + t.cost_bucket(d.0)] += 0.25;
    }
    let actions = if obs.legal_actions.is_empty() {
        Vec::new()
    } else {
        protocol_actions(obs)
    };
    let mut castable_defs: Vec<u16> = Vec::new();
    let mut seen: std::collections::HashSet<u32> = std::collections::HashSet::new();
    let mut attack_options = 0usize;
    for action in &actions {
        match action {
            Action::DeclareAttacker { .. } => attack_options += 1,
            Action::CastSpell { card, .. } => {
                if let Some(&d) = hand_def.get(&card.0) {
                    if seen.insert(card.0) {
                        castable_defs.push(d);
                        if let Some(&i) = t.def_slot.get(&d) {
                            v[base + i] += 0.25;
                        }
                        v[base + c + N_COST_BUCKETS + t.cost_bucket(d)] += 0.25;
                    }
                }
            }
            _ => {}
        }
    }
    let mut power = [0f32; 2];
    let mut flying = [0f32; 2];
    for perm in &obs.battlefield {
        let side = if perm.controller == me { 0 } else { 1 };
        if let Some(p) = perm.power {
            power[side] += f32::from(p);
            if t.flying.contains(&perm.definition.0) {
                flying[side] += f32::from(p);
            }
        }
    }
    let turns_to_kill = |attacker: f32, defender_life: i16| -> f32 {
        if attacker <= 0.0 {
            10.0
        } else {
            (f32::from(defender_life.max(0)) / attacker).ceil().min(10.0)
        }
    };
    let max_cost = castable_defs
        .iter()
        .map(|d| t.cost.get(d).copied().unwrap_or(0))
        .max()
        .unwrap_or(0);
    let e = base + c + 2 * N_COST_BUCKETS;
    v[e] = (castable_defs.len() as f32) / 4.0;
    v[e + 1] = (max_cost.min(6) as f32) / 6.0;
    v[e + 2] = f32::from(obs.life_totals[mi] - obs.life_totals[oi]) / 20.0;
    v[e + 3] = flying[0] / 8.0;
    v[e + 4] = flying[1] / 8.0;
    v[e + 5] = turns_to_kill(power[0], obs.life_totals[oi]) / 10.0;
    v[e + 6] = turns_to_kill(power[1], obs.life_totals[mi]) / 10.0;
    v[e + 7] = (attack_options as f32) / 8.0;

    // -- hidden-pool belief block (extractor.py _belief_block) ----------
    // For [me, opp]: that player's decklist counts minus every card of
    // theirs visible anywhere (own battlefield + graveyard + exile + stack
    // objects they control, plus the viewer's own hand), clamped >=0 and
    // scaled by 1/4. Definitions outside the decklist floor at 0.
    if t.belief {
        let my_deck = &decks[mi];
        let opp_deck = &decks[oi];
        let mut my_seen = vec![0i32; c];
        let mut opp_seen = vec![0i32; c];
        for (_, d) in &obs.hand {
            if let Some(&i) = t.def_slot.get(&d.0) { my_seen[i] += 1; }
        }
        for perm in &obs.battlefield {
            if let Some(&i) = t.def_slot.get(&perm.definition.0) {
                if perm.controller == me { my_seen[i] += 1; }
                else { opp_seen[i] += 1; }
            }
        }
        for (_, d) in &obs.graveyards[mi] {
            if let Some(&i) = t.def_slot.get(&d.0) { my_seen[i] += 1; }
        }
        for (_, d) in &obs.graveyards[oi] {
            if let Some(&i) = t.def_slot.get(&d.0) { opp_seen[i] += 1; }
        }
        for (_, d) in &obs.exiles[mi] {
            if let Some(&i) = t.def_slot.get(&d.0) { my_seen[i] += 1; }
        }
        for (_, d) in &obs.exiles[oi] {
            if let Some(&i) = t.def_slot.get(&d.0) { opp_seen[i] += 1; }
        }
        for obj in &obs.stack {
            if let Some(&i) = t.def_slot.get(&obj.definition.0) {
                if obj.controller == me { my_seen[i] += 1; }
                else { opp_seen[i] += 1; }
            }
        }
        let b = t.belief_base;
        for i in 0..c {
            let mv = my_deck.get(i).copied().unwrap_or(0);
            let ov = opp_deck.get(i).copied().unwrap_or(0);
            v[b + i] = ((mv - my_seen[i]).max(0) as f32) / 4.0;
            v[b + c + i] = ((ov - opp_seen[i]).max(0) as f32) / 4.0;
        }
    }
    v
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn step_enum_order_matches_json_names() {
        use penta::Step;
        let steps = [Step::Upkeep, Step::Draw, Step::PrecombatMain,
                     Step::BeginningOfCombat, Step::DeclareAttackers,
                     Step::DeclareBlockers, Step::CombatDamage,
                     Step::EndOfCombat, Step::PostcombatMain, Step::End,
                     Step::Cleanup];
        for (i, s) in steps.iter().enumerate() {
            assert_eq!(step_index(*s), i);
            assert_eq!(format!("{s:?}"), STEPS[i]);
        }
    }
}
