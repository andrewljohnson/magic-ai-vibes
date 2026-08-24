//! Attacking bands.
//!
//! CR 702.21b lets the attacking player declare a band: one or more attacking
//! creatures with banding plus up to one without. Everything the band does
//! afterwards -- being blocked as a group, and handing its controller the
//! blockers' damage assignment -- reads the membership recorded here.
//!
//! Bands are built a pair at a time. A single "declare this whole band"
//! action would have to enumerate every legal subset of the attack, which
//! grows unpleasantly and does not match how the rest of the declaration
//! works; merging two groups at a time reaches the same set of bands through
//! actions the legal-action list can hold.

use super::{Action, BandingQuality, GameObjectId, KeywordAbility, Permanent, PlayerId};

impl super::Game {
    /// Every creature banded with this one, itself included. An attacker with
    /// no band index is a group of one: the merge treats a lone attacker and
    /// an existing band the same way.
    pub(super) fn band_group(&self, attacker: GameObjectId) -> Vec<GameObjectId> {
        let Some(permanent) = self.attacking_permanent(attacker) else {
            return Vec::new();
        };
        let Some(band) = permanent.attacking_band else {
            return vec![attacker];
        };
        self.battlefield
            .iter()
            .filter(|other| other.attacking && other.attacking_band == Some(band))
            .map(|other| other.card.id)
            .collect()
    }

    /// Whether these two creatures are in the same band. Two lone attackers
    /// are not, however alike their state looks: neither is in a band at all.
    pub(super) fn share_a_band(&self, one: GameObjectId, other: GameObjectId) -> bool {
        self.attacking_permanent(one)
            .and_then(|permanent| permanent.attacking_band)
            .is_some_and(|band| {
                self.attacking_permanent(other)
                    .is_some_and(|permanent| permanent.attacking_band == Some(band))
            })
    }

    fn attacking_permanent(&self, attacker: GameObjectId) -> Option<&Permanent> {
        self.battlefield
            .iter()
            .find(|permanent| permanent.card.id == attacker && permanent.attacking)
    }

    fn has_banding(&self, attacker: GameObjectId) -> bool {
        self.attacking_permanent(attacker).is_some_and(|permanent| {
            self.permanent_has_executable_keyword(permanent, KeywordAbility::Banding)
        })
    }

    /// Whether this creature carries "bands with other" naming this quality.
    /// The two are separate questions: a creature can have one quality's
    /// ability and not another's, and neither implies plain banding.
    pub(super) fn has_bands_with_other(
        &self,
        creature: GameObjectId,
        quality: BandingQuality,
    ) -> bool {
        self.battlefield
            .iter()
            .find(|permanent| permanent.card.id == creature)
            .is_some_and(|permanent| {
                self.permanent_has_executable_keyword(
                    permanent,
                    KeywordAbility::BandsWithOther(quality),
                )
            })
    }

    /// Whether this creature is the kind of thing a band on this quality is
    /// made of. The ability names a quality every member must have, which is
    /// what replaces plain banding's single free passenger.
    pub(super) fn matches_banding_quality(
        &self,
        creature: GameObjectId,
        quality: BandingQuality,
    ) -> bool {
        self.battlefield
            .iter()
            .find(|permanent| permanent.card.id == creature)
            .is_some_and(|permanent| {
                let characteristics = self.targeting_event_object(permanent);
                self.trigger_object_matches(*quality.predicate(), &characteristics, creature, false)
            })
    }

    /// Whether these creatures could be a band on some quality: CR 702.21j
    /// wants every member to have it and at least one to carry the ability.
    fn qualify_as_a_band(&self, members: &[GameObjectId]) -> bool {
        BandingQuality::ALL.iter().any(|quality| {
            members
                .iter()
                .any(|member| self.has_bands_with_other(*member, *quality))
                && members
                    .iter()
                    .all(|member| self.matches_banding_quality(*member, *quality))
        })
    }

    /// Whether these two groups may be declared as one band.
    ///
    /// Plain banding allows at most one member without it, which also
    /// guarantees at least one with it, since a merged group always has two
    /// members. "Bands with other" is the other way round: no free passenger,
    /// but every member sharing the named quality.
    fn groups_may_band(&self, first: GameObjectId, second: GameObjectId) -> bool {
        let (Some(one), Some(other)) = (
            self.attacking_permanent(first),
            self.attacking_permanent(second),
        ) else {
            return false;
        };
        // A band attacks one defender as a unit, so creatures pointed at
        // different players or planeswalkers cannot be in one.
        if one.attack_defender != other.attack_defender || one.controller != other.controller {
            return false;
        }
        if self.share_a_band(first, second) {
            return false;
        }
        let members: Vec<_> = self
            .band_group(first)
            .into_iter()
            .chain(self.band_group(second))
            .collect();
        let plain = members
            .iter()
            .filter(|member| !self.has_banding(**member))
            .count()
            <= 1;
        plain || self.qualify_as_a_band(&members)
    }

    /// The bands this player may still form. Each pair is offered once, in
    /// object-id order, because merging one group into another is the same
    /// declaration whichever way round it is named.
    pub(super) fn band_actions(&self, player: PlayerId) -> Vec<Action> {
        let attackers: Vec<_> = self
            .battlefield
            .iter()
            .filter(|permanent| permanent.attacking && permanent.controller == player)
            .map(|permanent| permanent.card.id)
            .collect();
        let mut actions = Vec::new();
        for (index, first) in attackers.iter().enumerate() {
            for second in &attackers[index + 1..] {
                let (first, second) = if first.0 <= second.0 {
                    (*first, *second)
                } else {
                    (*second, *first)
                };
                if self.groups_may_band(first, second) {
                    actions.push(Action::BandAttackers { first, second });
                }
            }
        }
        actions
    }

    /// Merges the two groups, giving every member one index. Reusing an index
    /// already in play would silently join a third band, so the new one is
    /// chosen past every index the combat is using.
    pub(super) fn form_band(&mut self, first: GameObjectId, second: GameObjectId) {
        let mut members = self.band_group(first);
        members.extend(self.band_group(second));
        let band = self
            .battlefield
            .iter()
            .filter_map(|permanent| permanent.attacking_band)
            .max()
            .map_or(0, |highest| highest.saturating_add(1));
        for permanent in &mut self.battlefield {
            if members.contains(&permanent.card.id) {
                permanent.attacking_band = Some(band);
            }
        }
    }
}
