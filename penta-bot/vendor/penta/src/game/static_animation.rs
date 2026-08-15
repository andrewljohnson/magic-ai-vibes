//! Animation supplied by a static ability rather than by a resolved effect.
//!
//! "All Forests are 1/1 creatures that are still lands" has to keep applying
//! as Forests come and go, so it cannot be materialised onto a permanent the
//! way a resolving animation is. It is read live instead, which raises the
//! question a live read always raises: the effect changes characteristics,
//! and the permanents it applies to are chosen by characteristics.
//!
//! The stratification here is narrow rather than general. A static animation
//! may only add the creature type and stats -- never subtypes, never removed
//! abilities -- and may only be aimed by predicates that ask about land
//! types, the card types on the printed rules, and control. None of those can
//! read anything a static animation supplies, so the walk below cannot
//! reach back into itself. `runtime_support` holds cards to the same limits,
//! so a card that needs more is blocked rather than silently misread.

use super::{
    AnimationDef, AppliedEffectDef, BasicLandType, CardType, DeclarativeAbilityDef, EffectDef,
    EffectRecipientDef, Game, ObjectPredicateDef, Permanent, TriggerContext, ZoneKind,
};

impl Game {
    /// The animation applying to this permanent: the one a resolved effect
    /// put there, or failing that whatever a static ability supplies.
    pub(super) fn effective_animation(
        &self,
        permanent: &Permanent,
    ) -> Option<&'static AnimationDef> {
        permanent
            .animation
            .or_else(|| self.static_animation(permanent))
    }

    /// The latest static animation covering this permanent. Later timestamps
    /// win, the same way a second resolved animation overwrites the first.
    fn static_animation(&self, affected: &Permanent) -> Option<&'static AnimationDef> {
        let mut latest: Option<(&'static AnimationDef, super::ContinuousEffectTimestamp)> = None;
        for source in self.battlefield.iter().chain(self.emblems.iter()) {
            let Some(rules) = self.effective_rules(source) else {
                continue;
            };
            for ability in rules.ability_clauses() {
                if !ability.is_executable()
                    || !matches!(ability.definition, DeclarativeAbilityDef::Static(_))
                {
                    continue;
                }
                let Some(EffectDef::Apply {
                    recipient,
                    effect: AppliedEffectDef::Animate(animation),
                    ..
                }) = ability.declarative_effect()
                else {
                    continue;
                };
                if !Self::static_animation_is_additive(animation)
                    || !self.static_animation_applies(recipient, source, affected)
                {
                    continue;
                }
                if latest.is_none_or(|(_, timestamp)| timestamp <= source.timestamp) {
                    latest = Some((animation, source.timestamp));
                }
            }
        }
        latest.map(|(animation, _)| animation)
    }

    /// Whether an animation only adds to what the permanent already is. One
    /// that renamed subtypes or removed abilities would feed the layers that
    /// choose which permanents it applies to.
    #[must_use]
    pub fn static_animation_is_additive(animation: &AnimationDef) -> bool {
        animation.subtypes.is_empty()
            && !animation.all_creature_types
            && !animation.replaces_subtypes
            && !animation.loses_abilities
    }

    fn static_animation_applies(
        &self,
        recipient: EffectRecipientDef,
        source: &Permanent,
        affected: &Permanent,
    ) -> bool {
        let EffectRecipientDef::MatchingObjects {
            object,
            zones,
            controller,
        } = recipient
        else {
            return false;
        };
        zones.contains(&ZoneKind::Battlefield)
            && self.player_relation_matches(
                affected.controller,
                controller,
                source.controller,
                TriggerContext::empty(),
            )
            && self.static_animation_predicate_matches(object, affected)
    }

    /// The narrow predicate vocabulary a static animation may be aimed with.
    /// Anything outside it answers false here and is refused by the runtime
    /// boundary, so the two cannot disagree about which cards are supported.
    pub(super) fn static_animation_predicate_matches(
        &self,
        predicate: ObjectPredicateDef,
        affected: &Permanent,
    ) -> bool {
        match predicate {
            ObjectPredicateDef::Any => true,
            ObjectPredicateDef::HasType(card_type) => self
                .effective_rules(affected)
                .is_some_and(|rules| rules.types().contains(card_type)),
            ObjectPredicateDef::HasAnyBasicLandType(land_types) => {
                // Read from the subtype layer directly. `effective_land_types`
                // guards on the card types, which is the very layer a static
                // animation contributes to.
                let subtypes = self.effective_subtypes(affected);
                land_types.iter().any(|land_type| {
                    subtypes
                        .iter()
                        .any(|subtype| BasicLandType::from_subtype(subtype) == Some(*land_type))
                })
            }
            ObjectPredicateDef::All(predicates) => predicates
                .iter()
                .all(|predicate| self.static_animation_predicate_matches(*predicate, affected)),
            ObjectPredicateDef::AnyOf(predicates) => predicates
                .iter()
                .any(|predicate| self.static_animation_predicate_matches(*predicate, affected)),
            ObjectPredicateDef::Not(predicate) => {
                !self.static_animation_predicate_matches(*predicate, affected)
            }
            _ => false,
        }
    }

    /// Whether this predicate stays inside the vocabulary above. The boundary
    /// test asks the same question of every card that claims the shape.
    #[must_use]
    pub fn static_animation_predicate_is_supported(predicate: ObjectPredicateDef) -> bool {
        match predicate {
            ObjectPredicateDef::Any
            | ObjectPredicateDef::HasAnyBasicLandType(_)
            | ObjectPredicateDef::HasType(CardType::Land) => true,
            ObjectPredicateDef::All(predicates) | ObjectPredicateDef::AnyOf(predicates) => {
                predicates
                    .iter()
                    .copied()
                    .all(Self::static_animation_predicate_is_supported)
            }
            ObjectPredicateDef::Not(predicate) => {
                Self::static_animation_predicate_is_supported(*predicate)
            }
            _ => false,
        }
    }
}
