use super::Policy;
use crate::{Action, PlayerObservation};

/// Selects uniformly from the non-concession legal actions using a seeded PRNG.
#[derive(Clone, Debug)]
pub struct RandomPolicy {
    state: u64,
}

impl RandomPolicy {
    #[must_use]
    pub const fn new(seed: u64) -> Self {
        Self { state: seed }
    }

    fn next_u64(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9e37_79b9_7f4a_7c15);
        let mut value = self.state;
        value = (value ^ (value >> 30)).wrapping_mul(0xbf58_476d_1ce4_e5b9);
        value = (value ^ (value >> 27)).wrapping_mul(0x94d0_49bb_1331_11eb);
        value ^ (value >> 31)
    }
}

impl Policy for RandomPolicy {
    fn choose_action(&mut self, observation: &PlayerObservation) -> Option<Action> {
        if let Some(decision) = observation.decision.as_ref() {
            let mut options = decision
                .options
                .iter()
                .map(|option| option.id)
                .collect::<Vec<_>>();
            if options.len() < decision.minimum {
                return None;
            }
            for index in (1..options.len()).rev() {
                let index_u64 = u64::try_from(index + 1).unwrap_or(u64::MAX);
                let offset = usize::try_from(self.next_u64() % index_u64).unwrap_or(0);
                options.swap(index, offset);
            }
            let count = if decision.minimum == decision.maximum {
                decision.minimum
            } else {
                let span = decision.maximum - decision.minimum + 1;
                let offset =
                    usize::try_from(self.next_u64() % u64::try_from(span).unwrap_or(u64::MAX))
                        .unwrap_or(0);
                decision.minimum + offset
            };
            return Some(Action::ChooseDecision {
                decision: decision.id,
                options: options.into_iter().take(count).collect(),
            });
        }
        let choices: Vec<_> = observation
            .legal_actions
            .iter()
            .filter(|action| !matches!(action, Action::Concede))
            .collect();
        if choices.is_empty() {
            return observation.legal_actions.first().cloned();
        }
        let choice_count = u64::try_from(choices.len()).unwrap_or(u64::MAX);
        let unbiased_range = u64::MAX - u64::MAX % choice_count;
        loop {
            let value = self.next_u64();
            if value < unbiased_range {
                let index = usize::try_from(value % choice_count).unwrap_or(0);
                return Some(choices[index].clone());
            }
        }
    }
}
