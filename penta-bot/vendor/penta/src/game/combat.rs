use super::{
    Action, AppliedEffectDef, AttackDefender, CardBehavior, CardRules, CardType,
    CombatDamageAssignment, CombatDamageStage, CommittedTriggerEvent, ControlFlow, CounterKind,
    EffectDef, Game, GameEvent, GameObjectId, KeywordAbility, Permanent, PlayerId, Target,
};

mod damage_delivery;

impl Game {
    pub(super) fn attacker_actions(&self, player: PlayerId, moat_active: bool) -> Vec<Action> {
        let mut defenders = vec![AttackDefender::Player(player.opponent())];
        defenders.extend(
            self.battlefield
                .iter()
                .filter(|permanent| {
                    permanent.controller == player.opponent()
                        && self
                            .permanent_types(permanent)
                            .is_some_and(|types| types.contains(CardType::Planeswalker))
                })
                .map(|permanent| AttackDefender::Planeswalker(permanent.card.id)),
        );
        self.battlefield
            .iter()
            .filter(|permanent| {
                permanent.controller == player
                    && !permanent.tapped
                    && !permanent.attacking
                    && permanent.detained_until_turn_of.is_none()
                    && self.can_attack_with_moat(permanent, moat_active)
            })
            .flat_map(|permanent| {
                defenders
                    .iter()
                    .copied()
                    .map(|defender| Action::DeclareAttacker {
                        attacker: permanent.card.id,
                        defender,
                    })
            })
            .collect()
    }

    #[cfg(test)]
    pub(super) fn can_attack(&self, permanent: &Permanent) -> bool {
        let moat_active = self.count_behavior(CardBehavior::Moat) > 0;
        self.can_attack_with_moat(permanent, moat_active)
    }

    pub(super) fn can_attack_with_moat(&self, permanent: &Permanent, moat_active: bool) -> bool {
        if self.base_stats(permanent).is_none() {
            return false;
        }
        let flying = moat_active && self.has_flying(permanent);
        self.can_attack_creature(permanent, moat_active, flying)
    }

    pub(super) fn can_attack_creature(
        &self,
        permanent: &Permanent,
        moat_active: bool,
        flying: bool,
    ) -> bool {
        if self.permanent_has_executable_keyword(permanent, KeywordAbility::Defender) {
            return false;
        }
        if moat_active && !flying {
            return false;
        }
        if !self.attack_restrictions_met(permanent) || self.cannot_attack(permanent) {
            return false;
        }
        self.permanent_has_executable_keyword(permanent, KeywordAbility::Haste)
            || self.turns_started[permanent.controller.index()] > permanent.entered_controller_turn
    }

    /// Whether every "can't attack unless ..." clause this creature prints is
    /// currently satisfied. The query carries its own controller relation, so
    /// "defending player" is read as the attacker's opponent -- which is the
    /// only defending player there is in a two-player game.
    fn attack_restrictions_met(&self, permanent: &Permanent) -> bool {
        self.effective_rules(permanent)
            .into_iter()
            .flat_map(CardRules::ability_clauses)
            .filter(|ability| ability.is_executable())
            .filter_map(|ability| match ability.declarative_effect()? {
                EffectDef::CannotAttackUnless(query) => Some(query),
                _ => None,
            })
            .all(|query| {
                self.any_battlefield_object_matches(query, permanent.card.id, permanent.controller)
            })
    }

    pub(super) fn declare_attacker(&mut self, attacker: GameObjectId, defender: AttackDefender) {
        let vigilance = self
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == attacker)
            .is_some_and(|permanent| {
                self.permanent_has_executable_keyword(permanent, KeywordAbility::Vigilance)
            });
        if let Some(permanent) = self
            .battlefield
            .iter_mut()
            .find(|permanent| permanent.card.id == attacker)
        {
            permanent.attacking = true;
            permanent.attack_defender = Some(defender);
            permanent.attacked_this_turn = true;
            permanent.attacks_this_turn = permanent.attacks_this_turn.saturating_add(1);
        }
        if !vigilance {
            let _ = self.tap_permanent(attacker);
        }
    }

    pub(super) fn finish_declaring_attackers(&mut self) {
        self.attackers_declared = true;
        self.priority = self.active_player;
        self.consecutive_passes = 0;
        let attackers = self
            .battlefield
            .iter()
            .filter(|permanent| permanent.controller == self.active_player && permanent.attacking)
            .map(|permanent| permanent.card.id)
            .collect::<Vec<_>>();
        if attackers.is_empty() {
            return;
        }
        self.events.push(GameEvent::AttackDeclared {
            player: self.active_player,
            attackers: attackers.clone(),
        });
        // CR 508.2: the whole declaration happens at once, so every attacker
        // is already attacking by the time any of these triggers is captured.
        let events = attackers
            .iter()
            .filter_map(|attacker| {
                self.battlefield
                    .iter()
                    .find(|permanent| permanent.card.id == *attacker)
                    .map(|permanent| CommittedTriggerEvent::Attacks {
                        object: self.trigger_event_object(permanent),
                    })
            })
            .collect::<Vec<_>>();
        // How many creatures attacked is decided by the declaration as a
        // whole, so it is known here and nowhere later. Every attacker gets
        // the same total; what varies is what each watching ability asks of
        // it.
        let total = u8::try_from(attackers.len()).unwrap_or(u8::MAX);
        let group = attackers
            .iter()
            .filter_map(|attacker| {
                self.battlefield
                    .iter()
                    .find(|permanent| permanent.card.id == *attacker)
                    .map(|permanent| CommittedTriggerEvent::AttacksInGroup {
                        object: self.trigger_event_object(permanent),
                        total,
                    })
            })
            .collect::<Vec<_>>();
        for event in &group {
            self.capture_battlefield_triggers(event);
        }
        for event in &events {
            self.capture_battlefield_triggers(event);
        }
    }

    /// Whether a static effect on `blocker` narrows what it may block, and
    /// this attacker is outside that. The other direction -- an attacker
    /// forbidding a blocker -- is `blocking_is_prevented`.
    pub(super) fn blocker_may_only_block(&self, blocker: &Permanent, attacker: &Permanent) -> bool {
        let characteristics = self.trigger_event_object(attacker);
        let mut restricted = false;
        let _ = self.visit_static_applied_effects(blocker, |applied| {
            if let AppliedEffectDef::CanBlockOnly(predicate) = applied.effect
                && !self.trigger_object_matches(predicate, &characteristics, blocker.card.id, false)
            {
                restricted = true;
                return ControlFlow::Break(());
            }
            ControlFlow::Continue(())
        });
        restricted
    }

    /// Whether a static effect on `attacker` forbids `blocker` from blocking
    /// it, as Juggernaut forbids Walls.
    pub(super) fn blocking_is_prevented(&self, attacker: &Permanent, blocker: &Permanent) -> bool {
        let characteristics = self.trigger_event_object(blocker);
        let mut prevented = false;
        let result = self.visit_static_applied_effects(attacker, |applied| {
            if let AppliedEffectDef::CannotBeBlockedBy(predicate) = applied.effect
                && self.trigger_object_matches(predicate, &characteristics, applied.source, false)
            {
                prevented = true;
                return ControlFlow::Break(());
            }
            ControlFlow::Continue(())
        });
        debug_assert!(result.is_continue() || prevented);
        prevented
    }

    /// Whether a continuous effect currently forbids this permanent from
    /// blocking. Asked afresh, so a turn-scoped prohibition stops applying
    /// when it expires and a static one when its source leaves.
    /// Whether a continuous effect currently stops anything blocking this
    /// permanent. The turn-scoped form is a flag beside it.
    fn cannot_be_blocked(&self, permanent: &Permanent) -> bool {
        self.visit_static_applied_effects(permanent, |applied| {
            if matches!(applied.effect, AppliedEffectDef::CannotBeBlocked) {
                ControlFlow::Break(())
            } else {
                ControlFlow::Continue(())
            }
        })
        .is_break()
    }

    /// Whether a continuous effect from anywhere forbids this creature from
    /// attacking. The printed "can't attack unless ..." clause is a separate
    /// question a creature asks about itself.
    pub(super) fn cannot_attack(&self, permanent: &Permanent) -> bool {
        self.visit_static_applied_effects(permanent, |applied| {
            if matches!(applied.effect, AppliedEffectDef::CannotAttack) {
                ControlFlow::Break(())
            } else {
                ControlFlow::Continue(())
            }
        })
        .is_break()
    }

    pub(super) fn cannot_block(&self, permanent: &Permanent) -> bool {
        if permanent.cannot_block_this_turn || permanent.detained_until_turn_of.is_some() {
            return true;
        }
        // Unleash: the counter is what stops it blocking, so a creature that
        // declined the counter blocks as normal and one that took it never
        // does again.
        if permanent.counters(CounterKind::PlusOnePlusOne) > 0
            && self.permanent_has_executable_keyword(permanent, KeywordAbility::Unleash)
        {
            return true;
        }
        self.visit_static_applied_effects(permanent, |applied| {
            if matches!(applied.effect, AppliedEffectDef::CannotBlock) {
                ControlFlow::Break(())
            } else {
                ControlFlow::Continue(())
            }
        })
        .is_break()
    }

    pub(super) fn blocker_actions(&self, player: PlayerId) -> Vec<Action> {
        let blockers: Vec<_> = self
            .battlefield
            .iter()
            .filter(|permanent| {
                permanent.controller == player
                    && !permanent.tapped
                    && permanent.blocking.is_none()
                    && self.power(permanent).is_some()
                    && !self.cannot_block(permanent)
            })
            .map(|permanent| permanent.card.id)
            .collect();
        let attackers: Vec<_> = self
            .battlefield
            .iter()
            .filter(|permanent| permanent.attacking)
            .map(|permanent| {
                (
                    permanent.card.id,
                    self.has_flying(permanent),
                    self.landwalk_beats(permanent, permanent.controller.opponent()),
                    self.power(permanent).unwrap_or(0),
                )
            })
            .collect();
        blockers
            .into_iter()
            .flat_map(|blocker| {
                let blocker_permanent = self
                    .battlefield
                    .iter()
                    .find(|permanent| permanent.card.id == blocker)
                    .expect("blocker is on the battlefield");
                let blocker_can_block_flying = self.has_flying(blocker_permanent)
                    || self
                        .permanent_has_executable_keyword(blocker_permanent, KeywordAbility::Reach);
                let ironclaw =
                    self.effective_behavior(blocker_permanent) == Some(CardBehavior::IronclawOrcs);
                attackers
                    .iter()
                    .filter_map(move |(attacker, flying, unblockable, power)| {
                        let attacker_permanent = self
                            .battlefield
                            .iter()
                            .find(|permanent| permanent.card.id == *attacker)
                            .expect("attacker is on the battlefield");
                        let intimidate = self.permanent_has_executable_keyword(
                            attacker_permanent,
                            KeywordAbility::Intimidate,
                        );
                        let shares_color = self
                            .permanent_colors(attacker_permanent)
                            .into_iter()
                            .zip(self.permanent_colors(blocker_permanent))
                            .any(|(attacker, blocker)| attacker && blocker);
                        let can_block = !(*unblockable
                            || attacker_permanent.unblockable_this_turn
                            || self.cannot_be_blocked(attacker_permanent)
                            || self.blocking_is_prevented(attacker_permanent, blocker_permanent)
                            || self.blocker_may_only_block(blocker_permanent, attacker_permanent)
                            || *flying && !blocker_can_block_flying
                            || intimidate
                                && !self.is_artifact_permanent(blocker_permanent)
                                && !shares_color
                            || ironclaw && *power >= 2
                            || self.combat_is_protected(blocker_permanent, attacker_permanent));
                        can_block.then_some(Action::DeclareBlocker {
                            blocker,
                            attacker: *attacker,
                        })
                    })
            })
            .collect()
    }

    pub(super) fn declare_blocker(&mut self, blocker: GameObjectId, attacker: GameObjectId) {
        if let Some(permanent) = self
            .battlefield
            .iter_mut()
            .find(|permanent| permanent.card.id == blocker)
        {
            permanent.blocking = Some(attacker);
        }
        if !self.combat_blocked_attackers.contains(&attacker) {
            self.combat_blocked_attackers.push(attacker);
        }
    }

    pub(super) fn finish_declaring_blockers(&mut self) {
        self.blockers_declared = true;
        self.priority = self.active_player;
        self.consecutive_passes = 0;
        let blocked = self
            .battlefield
            .iter()
            .filter_map(|permanent| permanent.blocking)
            .collect::<Vec<_>>();
        for permanent in &mut self.battlefield {
            permanent.blocked = blocked.contains(&permanent.card.id);
        }
        let assignments = self
            .battlefield
            .iter()
            .filter_map(|permanent| {
                permanent
                    .blocking
                    .map(|attacker| (permanent.card.id, attacker))
            })
            .collect::<Vec<_>>();
        if !assignments.is_empty() {
            self.events.push(GameEvent::BlockDeclared {
                player: self.active_player.opponent(),
                assignments,
            });
        }
        self.capture_becomes_blocked_triggers(&blocked);
        self.capture_blocking_relationship_triggers();
        self.capture_unblocked_attacker_triggers(&blocked);
    }

    /// CR 509.1h leaves an attacker nobody blocked unblocked, which is what
    /// these clauses read. It can only be answered once blocking is done.
    fn capture_unblocked_attacker_triggers(&mut self, blocked: &[GameObjectId]) {
        let unblocked = self
            .battlefield
            .iter()
            .filter(|permanent| permanent.attacking && !blocked.contains(&permanent.card.id))
            .map(|permanent| self.trigger_event_object(permanent))
            .collect::<Vec<_>>();
        for object in unblocked {
            self.capture_battlefield_triggers(&CommittedTriggerEvent::AttacksAndIsNotBlocked {
                object,
            });
        }
    }

    /// One event per ordered pair of a blocker and what it blocks, so a
    /// clause printed on either creature reads the other as the triggering
    /// object. "Blocks or becomes blocked by" is one clause, not two.
    fn capture_blocking_relationship_triggers(&mut self) {
        let pairs = self
            .battlefield
            .iter()
            .filter_map(|permanent| {
                permanent
                    .blocking
                    .map(|attacker| (permanent.card.id, attacker))
            })
            .collect::<Vec<_>>();
        for (blocker, attacker) in pairs {
            let Some((blocker, attacker)) = self
                .battlefield
                .iter()
                .find(|permanent| permanent.card.id == blocker)
                .map(|permanent| self.trigger_event_object(permanent))
                .zip(
                    self.battlefield
                        .iter()
                        .find(|permanent| permanent.card.id == attacker)
                        .map(|permanent| self.trigger_event_object(permanent)),
                )
            else {
                continue;
            };
            for (creature, other) in [(blocker.clone(), attacker.clone()), (attacker, blocker)] {
                self.capture_battlefield_triggers(&CommittedTriggerEvent::BlocksOrBecomesBlocked {
                    creature,
                    other,
                });
            }
        }
    }

    /// CR 509.1h. Each attacker becomes blocked once, however many creatures
    /// blocked it, so the event fires per attacker and carries the count the
    /// rampage-style clauses are written against.
    fn capture_becomes_blocked_triggers(&mut self, blocked: &[GameObjectId]) {
        let mut attackers = blocked.to_vec();
        attackers.sort_unstable();
        attackers.dedup();
        for attacker in attackers {
            let blockers = blocked.iter().filter(|id| **id == attacker).count();
            let Some(object) = self
                .battlefield
                .iter()
                .find(|permanent| permanent.card.id == attacker)
                .map(|permanent| self.trigger_event_object(permanent))
            else {
                continue;
            };
            self.capture_battlefield_triggers(&CommittedTriggerEvent::BecomesBlocked {
                object,
                blockers_beyond_first: u16::try_from(blockers.saturating_sub(1))
                    .unwrap_or(u16::MAX),
            });
        }
    }

    pub(super) fn start_combat_damage(&mut self) {
        // Tests and a few internal procedures can construct combat directly,
        // so also capture live blocking relationships here. During an ordinary
        // game, `declare_blocker` recorded them before either player received
        // priority and they therefore survive a blocker leaving the field.
        let newly_blocked = self
            .battlefield
            .iter()
            .filter_map(|permanent| permanent.blocking)
            .collect::<Vec<_>>();
        for attacker in newly_blocked {
            if !self.combat_blocked_attackers.contains(&attacker) {
                self.combat_blocked_attackers.push(attacker);
            }
        }

        let strike_wave_combatants = self
            .battlefield
            .iter()
            .filter(|permanent| permanent.attacking || permanent.blocking.is_some())
            .filter(|permanent| {
                self.permanent_has_executable_keyword(permanent, KeywordAbility::FirstStrike)
                    || self
                        .permanent_has_executable_keyword(permanent, KeywordAbility::DoubleStrike)
            })
            .map(|permanent| permanent.card.id)
            .collect::<Vec<_>>();
        self.combat_damage_stage = if strike_wave_combatants.is_empty() {
            CombatDamageStage::Single
        } else {
            CombatDamageStage::FirstStrike {
                strike_wave_combatants,
            }
        };
        self.begin_combat_damage_assignment();
    }

    pub(super) fn begin_regular_combat_damage_after_first_strike(&mut self) {
        let CombatDamageStage::FirstStrike {
            strike_wave_combatants,
        } = &self.combat_damage_stage
        else {
            return;
        };
        self.combat_damage_stage = CombatDamageStage::RegularAfterFirstStrike {
            strike_wave_combatants: strike_wave_combatants.clone(),
        };
        self.begin_combat_damage_assignment();
    }

    pub(super) fn deals_damage_in_current_combat_step(&self, permanent: &Permanent) -> bool {
        match &self.combat_damage_stage {
            CombatDamageStage::NotStarted | CombatDamageStage::Single => true,
            CombatDamageStage::FirstStrike {
                strike_wave_combatants,
            } => strike_wave_combatants.contains(&permanent.card.id),
            CombatDamageStage::RegularAfterFirstStrike {
                strike_wave_combatants,
            } => {
                !strike_wave_combatants.contains(&permanent.card.id)
                    || self
                        .permanent_has_executable_keyword(permanent, KeywordAbility::DoubleStrike)
            }
        }
    }

    pub(super) fn begin_combat_damage_assignment(&mut self) {
        for permanent in &mut self.battlefield {
            permanent.combat_damage_assignment.clear();
        }
        self.pending_combat_attackers = self
            .battlefield
            .iter()
            .filter(|attacker| {
                attacker.attacking && self.deals_damage_in_current_combat_step(attacker)
            })
            // Ask exactly when there is a real choice. One blocker and no
            // trample leaves a single legal distribution and no question; one
            // blocker plus trample is a genuine decision about how much to
            // spill past it.
            .filter(|attacker| self.combat_assignment_actions(attacker.card.id).len() > 1)
            .map(|attacker| attacker.card.id)
            .collect();
        if self.pending_combat_attackers.is_empty() {
            self.deal_combat_damage();
        }
    }

    /// Who assigns this attacker's combat damage. CR 702.22: banding moves
    /// that choice to the defending player, so a creature with banding
    /// blocking an attacker takes the decision away from the attacker's
    /// controller. Any one banding blocker is enough.
    pub(super) fn combat_damage_assigner(&self, attacker: GameObjectId) -> PlayerId {
        self.battlefield
            .iter()
            .find(|permanent| {
                permanent.blocking == Some(attacker)
                    && self.permanent_has_executable_keyword(permanent, KeywordAbility::Banding)
            })
            .map_or(self.active_player, |blocker| blocker.controller)
    }

    pub(super) fn combat_assignment_actions(&self, attacker_id: GameObjectId) -> Vec<Action> {
        let Some(attacker) = self
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == attacker_id)
        else {
            return Vec::new();
        };
        let power = self.power(attacker).unwrap_or(0).max(0).cast_unsigned();
        let trample = self.has_trample(attacker);
        let mut recipients: Vec<_> = self
            .battlefield
            .iter()
            .filter(|permanent| permanent.blocking == Some(attacker_id))
            .map(|permanent| Target::Permanent(permanent.card.id))
            .collect();
        recipients.sort_unstable();
        let blocker_count = recipients.len();
        let defender_index = trample
            .then(|| self.combat_defender_target(attacker))
            .flatten()
            .map(|defender| {
                let index = recipients.len();
                recipients.push(defender);
                index
            });

        damage_distributions(recipients.len(), power)
            .into_iter()
            .filter(|amounts| {
                let blockers = || {
                    recipients
                        .iter()
                        .take(blocker_count)
                        .zip(amounts)
                        .filter_map(|(target, amount)| match target {
                            Target::Permanent(id) => Some((*id, *amount)),
                            Target::Player(_) | Target::Card(_) | Target::Spell(_) => None,
                        })
                };
                // CR 702.19b: trample only spills once every blocker has
                // lethal damage assigned. Without defender damage, current
                // CR 510.1c permits any division among the blockers.
                let defender_damage = defender_index
                    .and_then(|index| amounts.get(index))
                    .copied()
                    .unwrap_or(0);
                if defender_damage == 0 {
                    return true;
                }
                blockers().all(|(id, amount)| amount >= self.lethal_damage_from(id, attacker_id))
            })
            .map(|amounts| Action::AssignCombatDamage {
                attacker: attacker_id,
                assignments: recipients
                    .iter()
                    .copied()
                    .zip(amounts)
                    .map(|(recipient, amount)| CombatDamageAssignment { recipient, amount })
                    .collect(),
            })
            .collect()
    }

    /// How an unassigned attacker spreads its damage: enough to kill each
    /// blocker in turn, then the remainder over the top when it can trample
    /// onto its defender, or onto the last blocker otherwise.
    pub(super) fn default_damage_split(
        &self,
        attacker_id: GameObjectId,
        blockers: &[GameObjectId],
    ) -> Vec<(Target, u16)> {
        let Some(attacker) = self
            .battlefield
            .iter()
            .find(|permanent| permanent.card.id == attacker_id)
        else {
            return Vec::new();
        };
        let mut remaining = self.power(attacker).unwrap_or(0).max(0).cast_unsigned();
        let trample = self.has_trample(attacker);
        let mut split = Vec::with_capacity(blockers.len() + 1);
        for blocker in blockers {
            let amount = self
                .lethal_damage_from(*blocker, attacker_id)
                .min(remaining);
            remaining -= amount;
            split.push((Target::Permanent(*blocker), amount));
        }
        if remaining > 0 {
            if trample && let Some(defender) = self.combat_defender_target(attacker) {
                split.push((defender, remaining));
            } else if let Some(last) = split.last_mut() {
                last.1 += remaining;
            }
        }
        split
    }

    pub(super) fn lethal_damage(&self, permanent_id: GameObjectId) -> u16 {
        self.battlefield
            .iter()
            .find(|permanent| permanent.card.id == permanent_id)
            .map_or(0, |permanent| {
                self.toughness(permanent)
                    .unwrap_or(0)
                    .max(0)
                    .cast_unsigned()
                    .saturating_sub(permanent.damage)
            })
    }

    pub(super) fn lethal_damage_from(
        &self,
        permanent_id: GameObjectId,
        source: GameObjectId,
    ) -> u16 {
        let ordinary = self.lethal_damage(permanent_id);
        if ordinary > 0
            && self
                .source_controller_with_keyword(source, KeywordAbility::Deathtouch)
                .is_some()
        {
            1
        } else {
            ordinary
        }
    }

    pub(super) fn assign_combat_damage(
        &mut self,
        attacker: GameObjectId,
        assignments: Vec<CombatDamageAssignment>,
    ) {
        if let Some(permanent) = self
            .battlefield
            .iter_mut()
            .find(|permanent| permanent.card.id == attacker)
        {
            permanent.combat_damage_assignment = assignments;
        }
        self.pending_combat_attackers.remove(0);
        if self.pending_combat_attackers.is_empty() {
            self.deal_combat_damage();
        }
    }

    /// Whether combat damage to this recipient is prevented for the turn.
    /// Only a permanent can carry the prevention; a player never does.
    pub(super) fn combat_damage_is_prevented_for(&self, recipient: Target) -> bool {
        if self.all_combat_damage_prevented {
            return true;
        }
        matches!(recipient, Target::Permanent(id)
            if self
                .battlefield
                .iter()
                .any(|permanent| permanent.card.id == id
                    && (permanent.combat_damage_prevented
                        || self.static_combat_damage_prevented(permanent, false))))
    }

    /// Whether combat damage from this permanent is prevented for the turn.
    pub(super) fn combat_damage_is_prevented_from(&self, source: GameObjectId) -> bool {
        if self.all_combat_damage_prevented {
            return true;
        }
        self.battlefield.iter().any(|permanent| {
            permanent.card.id == source
                && (permanent.combat_damage_prevented
                    || permanent.combat_damage_dealt_by_prevented
                    || permanent.damage_dealt_by_prevented
                    || self.static_combat_damage_prevented(permanent, true))
        })
    }

    /// Whether a continuous effect currently stops this permanent's combat
    /// damage in the given direction. The turn-scoped flags above are set
    /// once and cleared at cleanup; this is asked afresh, so an Aura leaving
    /// the battlefield mid-combat stops applying immediately.
    fn static_combat_damage_prevented(&self, permanent: &Permanent, dealt_by: bool) -> bool {
        self.visit_static_applied_effects(permanent, |applied| {
            let prevented = match applied.effect {
                AppliedEffectDef::PreventCombatDamage => true,
                // One direction only: the permanent still takes what its
                // blockers deal it.
                AppliedEffectDef::PreventCombatDamageDealtBy => dealt_by,
                _ => false,
            };
            if prevented {
                ControlFlow::Break(())
            } else {
                ControlFlow::Continue(())
            }
        })
        .is_break()
    }

    pub(super) fn deal_combat_damage(&mut self) {
        let attackers: Vec<_> = self
            .battlefield
            .iter()
            .filter(|permanent| permanent.attacking)
            .map(|permanent| permanent.card.id)
            .collect();
        for attacker_id in attackers {
            let Some(attacker_index) = self
                .battlefield
                .iter()
                .position(|permanent| permanent.card.id == attacker_id)
            else {
                continue;
            };
            let power = self
                .power(&self.battlefield[attacker_index])
                .unwrap_or(0)
                .max(0)
                .cast_unsigned();
            let attacker_deals_damage = self
                .deals_damage_in_current_combat_step(&self.battlefield[attacker_index])
                && !self.combat_damage_is_prevented_from(attacker_id);
            let blockers: Vec<_> = self
                .battlefield
                .iter()
                .filter(|permanent| permanent.blocking == Some(attacker_id))
                .map(|permanent| permanent.card.id)
                .collect();
            if attacker_deals_damage && blockers.is_empty() {
                let was_blocked = self.combat_blocked_attackers.contains(&attacker_id);
                if was_blocked && !self.has_trample(&self.battlefield[attacker_index]) {
                    continue;
                }
                let Some(defender) = self.combat_defender_target(&self.battlefield[attacker_index])
                else {
                    continue;
                };
                self.deal_combat_damage_to(attacker_id, defender, power);
            } else if !blockers.is_empty() {
                self.exchange_blocked_combat_damage(
                    attacker_id,
                    attacker_index,
                    &blockers,
                    attacker_deals_damage,
                );
            }
        }
        self.check_state_based_actions();
    }
}

pub(super) fn damage_distributions(recipient_count: usize, total: u16) -> Vec<Vec<u16>> {
    if recipient_count == 0 {
        return (total == 0).then_some(Vec::new()).into_iter().collect();
    }
    let mut result = Vec::new();
    for amount in 0..=total {
        for mut tail in damage_distributions(recipient_count - 1, total - amount) {
            let mut distribution = vec![amount];
            distribution.append(&mut tail);
            result.push(distribution);
        }
    }
    result
}
