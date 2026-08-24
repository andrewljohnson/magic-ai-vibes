//! Paying crew's and saddle's cost: "tap any number of other untapped
//! creatures you control with total power N or more".
//!
//! Named one creature at a time, for the reason a multiple sacrifice is: the
//! cost is bounded by what the creatures add up to rather than by how many
//! there are, and a board of ten offers a thousand ways to pay one cost.
//! Stopping is offered as soon as the total is reached, because the printed
//! number is a floor rather than a quota -- a player who wants a creature
//! tapped for its own reasons may keep going.

use super::{
    DecisionContinuation, DecisionOption, DecisionPreference, DecisionVisibility, DecisionZone,
    Game, GameObjectId, PendingActivation, PlayerId,
};

impl Game {
    /// The other untapped creatures this player controls, and what each one
    /// contributes. Negative power counts as nothing rather than subtracting.
    fn saddle_candidates(
        &self,
        player: PlayerId,
        source: GameObjectId,
        chosen: &[GameObjectId],
    ) -> Vec<(GameObjectId, i32)> {
        self.battlefield
            .iter()
            .filter(|permanent| permanent.controller == player)
            .filter(|permanent| permanent.card.id != source)
            .filter(|permanent| !permanent.tapped)
            .filter(|permanent| !chosen.contains(&permanent.card.id))
            .filter_map(|permanent| {
                self.creature_stats(permanent)
                    .map(|stats| (permanent.card.id, i32::from(stats.power).max(0)))
            })
            .collect()
    }

    /// Whether the board could pay at all, which is what makes the ability a
    /// legal action in the first place.
    pub(super) fn can_pay_total_power_tap(
        &self,
        player: PlayerId,
        source: GameObjectId,
        minimum: u8,
    ) -> bool {
        self.saddle_candidates(player, source, &[])
            .iter()
            .map(|(_, power)| power)
            .sum::<i32>()
            >= i32::from(minimum)
    }

    /// Asks for the next creature to tap, and finishes the activation once
    /// the payer stops.
    pub(super) fn queue_activation_saddle(
        &mut self,
        player: PlayerId,
        remaining: i32,
        pending: PendingActivation,
        chosen: Vec<GameObjectId>,
    ) {
        let candidates = self.saddle_candidates(player, pending.source, &chosen);
        // A board that shrank out from under the payment cannot finish it.
        // The activation is already committed, so what it paid stays paid
        // and the ability never reaches the stack.
        if candidates.is_empty() {
            if remaining <= 0 {
                self.finish_activation_saddle(pending, chosen);
            }
            return;
        }
        let mut options = Vec::new();
        if remaining <= 0 {
            options.push(DecisionOption {
                id: 0,
                label: "Stop".to_owned(),
                card: None,
                members: Vec::new(),
                ability_text: None,
                zone: DecisionZone::None,
            });
        }
        for (index, (permanent, _)) in candidates.iter().enumerate() {
            options.push(DecisionOption {
                id: u32::try_from(index + 1).unwrap_or(u32::MAX),
                label: format!(
                    "Tap {}",
                    self.permanent_card_name(*permanent)
                        .unwrap_or_else(|| "a creature".into())
                ),
                card: self
                    .battlefield
                    .iter()
                    .find(|candidate| candidate.card.id == *permanent)
                    .map(|candidate| (*permanent, Self::effective_rules_source(candidate))),
                members: Vec::new(),
                ability_text: None,
                zone: DecisionZone::Battlefield,
            });
        }
        self.queue_decision(
            player,
            "Tap creatures to pay",
            DecisionVisibility::Public,
            DecisionPreference::Neutral,
            1..=1,
            false,
            options,
            DecisionContinuation::ActivationCostTap {
                player,
                remaining,
                pending: Box::new(pending),
                chosen,
            },
        );
    }

    /// Records one answer and asks again while anything is still owed.
    pub(super) fn continue_activation_saddle(
        &mut self,
        player: PlayerId,
        remaining: i32,
        pending: PendingActivation,
        mut chosen: Vec<GameObjectId>,
        answer: Option<u32>,
    ) {
        let candidates = self.saddle_candidates(player, pending.source, &chosen);
        let Some(answer) = answer else {
            return;
        };
        // Zero is the offer to stop, which is only made once the total is
        // reached.
        if answer == 0 {
            if remaining <= 0 {
                self.finish_activation_saddle(pending, chosen);
            }
            return;
        }
        let Some((named, power)) = usize::try_from(answer)
            .ok()
            .and_then(|index| candidates.get(index - 1))
            .copied()
        else {
            return;
        };
        chosen.push(named);
        self.queue_activation_saddle(player, remaining - power, pending, chosen);
    }

    /// Taps what was named and hands the activation back to the ordinary
    /// cost path.
    fn finish_activation_saddle(&mut self, pending: PendingActivation, chosen: Vec<GameObjectId>) {
        let PendingActivation {
            source,
            source_card,
            controller,
            frozen,
            targets,
            chosen_permanents,
            remaining_sacrifices,
        } = pending;
        // Tapped one at a time through the ordinary path, so "whenever this
        // creature becomes tapped" sees each of them.
        for permanent in chosen {
            self.tap_permanent(permanent);
        }
        self.continue_activated_ability_costs(
            source,
            source_card,
            controller,
            frozen,
            targets,
            chosen_permanents,
            remaining_sacrifices,
        );
    }
}
