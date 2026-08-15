use super::{
    AttackDefender, CardBehavior, GameObjectId, HandcraftedPolicy, PlayerObservation, Target,
};

impl HandcraftedPolicy {
    /// Which defender to point an attacker at. Damage that would finish the
    /// opponent goes at the opponent; short of that a planeswalker is the
    /// better target, because loyalty does not come back the way life can be
    /// gained and a resolved ultimate usually ends the game anyway.
    pub(super) fn defender_preference(
        observation: &PlayerObservation,
        attacker: GameObjectId,
        defender: AttackDefender,
    ) -> i32 {
        let committed: i16 = observation
            .battlefield
            .iter()
            .filter(|permanent| {
                permanent.controller == observation.viewer
                    && (permanent.attacking || permanent.id == attacker)
            })
            .filter_map(|permanent| permanent.power)
            .map(|power| power.max(0))
            .sum();
        let opponent_life = observation.life_totals[observation.viewer.opponent().index()];
        let lethal = committed >= opponent_life;
        match defender {
            AttackDefender::Player(_) if lethal => 400,
            AttackDefender::Player(_) => 0,
            AttackDefender::Planeswalker(_) if lethal => 0,
            AttackDefender::Planeswalker(_) => 200,
        }
    }

    pub(super) fn score_attack(
        &self,
        observation: &PlayerObservation,
        attacker: GameObjectId,
    ) -> i32 {
        let Some(attacker) = observation
            .battlefield
            .iter()
            .find(|permanent| permanent.id == attacker)
        else {
            return -1_000;
        };
        let attacker_power = attacker.power.unwrap_or(0).max(0);
        let attacker_toughness = attacker
            .toughness
            .unwrap_or(0)
            .saturating_sub(i16::try_from(attacker.damage).unwrap_or(i16::MAX));
        let already_attacking = observation
            .battlefield
            .iter()
            .filter(|permanent| permanent.controller == observation.viewer && permanent.attacking)
            .count();
        let blockers: Vec<_> = observation
            .battlefield
            .iter()
            .filter(|permanent| {
                permanent.controller == observation.viewer.opponent()
                    && !permanent.tapped
                    && permanent.power.is_some()
                    && (!attacker.flying || permanent.flying)
                    && !(self.behavior(permanent.definition) == Some(CardBehavior::IronclawOrcs)
                        && attacker_power >= 2)
            })
            .collect();
        if already_attacking >= blockers.len() {
            return 7_000;
        }
        let gets_eaten = blockers.iter().any(|blocker| {
            let blocker_power = blocker.power.unwrap_or(0).max(0);
            let blocker_toughness = blocker
                .toughness
                .unwrap_or(0)
                .saturating_sub(i16::try_from(blocker.damage).unwrap_or(i16::MAX));
            blocker_power >= attacker_toughness && blocker_toughness > attacker_power
        });
        if gets_eaten { 500 } else { 7_000 }
    }

    pub(super) fn score_block(
        observation: &PlayerObservation,
        blocker: GameObjectId,
        attacker: GameObjectId,
    ) -> i32 {
        let blocker = observation
            .battlefield
            .iter()
            .find(|permanent| permanent.id == blocker);
        let attacker = observation
            .battlefield
            .iter()
            .find(|permanent| permanent.id == attacker);
        let (Some(blocker), Some(attacker)) = (blocker, attacker) else {
            return 0;
        };
        let blocker_power = blocker.power.unwrap_or(0);
        let blocker_toughness =
            blocker.toughness.unwrap_or(0) - i16::try_from(blocker.damage).unwrap_or(i16::MAX);
        let attacker_power = attacker.power.unwrap_or(0);
        let attacker_toughness =
            attacker.toughness.unwrap_or(0) - i16::try_from(attacker.damage).unwrap_or(i16::MAX);
        let existing_power: i16 = observation
            .battlefield
            .iter()
            .filter(|permanent| permanent.blocking == Some(attacker.id))
            .filter_map(|permanent| permanent.power)
            .fold(0, i16::saturating_add);
        if existing_power >= attacker_toughness {
            return 0;
        }
        let kills = existing_power.saturating_add(blocker_power) >= attacker_toughness;
        let survives = blocker_toughness > attacker_power;
        match (kills, survives) {
            (true, true) => 7_000,
            (true, false) => 6_000,
            (false, true) => 4_000,
            (false, false) if attacker_power >= 4 => 2_000,
            (false, false) => 500,
        }
    }

    pub(super) fn score_assignment(
        observation: &PlayerObservation,
        assignments: &[crate::CombatDamageAssignment],
    ) -> i32 {
        assignments
            .iter()
            .map(|assignment| match assignment.recipient {
                Target::Player(player) if player == observation.viewer.opponent() => {
                    i32::from(assignment.amount) * 200
                }
                Target::Permanent(id) => observation
                    .battlefield
                    .iter()
                    .find(|permanent| permanent.id == id)
                    .map_or(0, |permanent| {
                        let remaining = permanent
                            .toughness
                            .unwrap_or(0)
                            .saturating_sub(i16::try_from(permanent.damage).unwrap_or(i16::MAX));
                        if i16::try_from(assignment.amount).unwrap_or(i16::MAX) >= remaining {
                            500
                        } else {
                            i32::from(assignment.amount)
                        }
                    }),
                Target::Player(_) | Target::Card(_) | Target::Spell(_) => 0,
            })
            .sum()
    }
}
