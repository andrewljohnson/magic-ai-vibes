//! Endure (CR 702.183a).
//!
//! "Endure N" is a choice between two whole effects: N +1/+1 counters on the
//! object, or an N/N white Spirit creature token. Its controller picks as
//! the ability resolves, which is why it is a procedure of its own rather
//! than a composition -- nothing else in the effect vocabulary offers a
//! branch between two effects at resolution time.
//!
//! The token is not authored beside the number because the keyword fixes it:
//! an N/N white Spirit, whatever N turns out to be.

use crate::card::TokenCharacteristics;
use crate::ids::{GameObjectId, PlayerId};

use super::{
    CounterKind, DecisionContinuation, DecisionOption, DecisionPreference, DecisionVisibility,
    DecisionZone, Game, ManaColor,
};

/// The option id for the counters branch, and for the token branch. They are
/// stable because a checkpoint restores the decision by them.
const COUNTERS: u32 = 0;
const TOKEN: u32 = 1;

impl Game {
    pub(super) fn queue_endure(&mut self, player: PlayerId, permanent: GameObjectId, amount: u16) {
        // Enduring nothing is not a choice: no counters and a 0/0 that dies
        // immediately are both nothing, so the clause simply does not ask.
        if amount == 0 {
            return;
        }
        let name = self
            .object_card_name(permanent)
            .map_or_else(|| "this creature".to_owned(), std::borrow::Cow::into_owned);
        self.queue_decision(
            player,
            format!("Endure {amount}"),
            DecisionVisibility::Public,
            DecisionPreference::PreferOption(COUNTERS),
            1..=1,
            false,
            vec![
                DecisionOption {
                    id: COUNTERS,
                    label: format!("Put {amount} +1/+1 counters on {name}"),
                    card: None,
                    members: Vec::new(),
                    ability_text: None,
                    zone: DecisionZone::Battlefield,
                },
                DecisionOption {
                    id: TOKEN,
                    label: format!("Create a {amount}/{amount} white Spirit creature token"),
                    card: None,
                    members: Vec::new(),
                    ability_text: None,
                    zone: DecisionZone::None,
                },
            ],
            DecisionContinuation::Endure {
                player,
                permanent,
                amount,
            },
        );
    }

    pub(super) fn finish_endure(
        &mut self,
        player: PlayerId,
        permanent: GameObjectId,
        amount: u16,
        chosen: &[u32],
    ) {
        if chosen.first().copied() == Some(TOKEN) {
            let power = i16::try_from(amount).unwrap_or(i16::MAX);
            self.create_token_from(
                player,
                TokenCharacteristics::creature(&["Spirit"], &[ManaColor::White], power, power),
                None,
            );
            return;
        }
        // The counters go on whatever is still there. A creature that left
        // in response takes the choice with it, which is what putting
        // counters on a departed object always comes to.
        self.add_counters_to_permanent(permanent, CounterKind::PlusOnePlusOne, amount);
    }
}
