//! Permission to play a card from exile.
//!
//! Two shapes reach this: a card on an adventure, which its owner may cast
//! later as the creature half and which never lapses; and a card somebody
//! else's effect exiled and handed to a player for a while, which is played
//! for free and expires.

use super::{CardDefinition, Game, GameObjectId, PlayOptionDef, PlayerId};
use crate::card::ManaCost;

/// What a card in exile costs the player who may play it.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) enum ExilePlayCost {
    /// Its own cost, as printed.
    Printed,
    /// Waived entirely (CR 118.5): "you may play those cards without paying
    /// their mana costs".
    Free,
    /// "By paying an amount of {E} equal to its mana value rather than paying
    /// its mana cost." The mana cost goes away and the energy takes its
    /// place, so a card nobody has the energy for is not castable at all.
    EnergyEqualToManaValue,
    /// Foretell: the card's own foretell cost, which the card prints as an
    /// alternative cast. A foretold card lies face down, so until it is cast
    /// only its owner knows what it is.
    Foretell,
}

/// One card in exile somebody may play from there.
///
/// Several of the flags below are independent facts about one permission --
/// what it costs, whether the card lies face down, whether it may be played
/// at all yet -- so they stay separate rather than collapsing into a kind.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[allow(clippy::struct_excessive_bools)]
pub(super) struct ExilePlayPermission {
    pub(super) card: GameObjectId,
    /// Who may play it. An adventure returns to its owner; a card taken off
    /// the top of somebody's library is played by whoever took it.
    pub(super) player: PlayerId,
    /// What playing it costs, which need not be what the card prints.
    pub(super) cost: ExilePlayCost,
    /// The turn this permission belongs to, as the turn count of the player
    /// whose turn it was. `None` never lapses, which is what an adventure
    /// means; anything else is gone once that turn is over.
    pub(super) until_end_of_turn: Option<(PlayerId, u32)>,
    /// Whether only the main half of an Adventure card may be played, which
    /// is what "as the creature, never as the adventure again" means
    /// (CR 715.3d).
    pub(super) adventure_return_only: bool,
    /// What a spell played under this permission costs on top of whatever
    /// [`Self::cost`] already says. Empty for every permission that adds
    /// nothing, which is all of them but Elite Spellbinder's.
    pub(super) surcharge: ManaCost,
    /// The earliest turn this permission may be used, as the turn count of
    /// the player whose turn it will be. Foretell is the only thing that
    /// sets it: a card exiled this turn is castable on a later one, and
    /// "later" is the whole cost of the two mana.
    pub(super) not_before_turn: Option<(PlayerId, u32)>,
    /// Whether the card lies face down in exile. Only its owner sees what it
    /// is; everybody may count how many there are.
    pub(super) face_down: bool,
    /// Whether mana spent on this card may be of any colour, which is a
    /// property of the permission rather than of the card.
    pub(super) spend_any_color: bool,
    /// What has to be true where the card is played, asked then rather than
    /// where the permission was granted.
    pub(super) condition: Option<crate::card::ExilePlayConditionDef>,
    /// A permission to look and nothing more. Hideaway hides a card its
    /// controller may see and nobody may play until the land's own second
    /// ability says so, so the two halves are separate: this one records
    /// that the card is theirs to look at.
    pub(super) hidden_only: bool,
    /// The holder's turn whose end step this permission runs to, as their
    /// turn count. Unlike [`Self::until_end_of_turn`] this survives the turn
    /// it was granted on when that turn was somebody else's: "until your
    /// next end step" reaches across to the holder's own.
    pub(super) until_holder_end_step: Option<(PlayerId, u32)>,
}

impl ExilePlayCost {
    /// The stable wire label for this cost. Written out rather than derived,
    /// because a `Debug` rendering is not a wire contract.
    pub(super) const fn label(self) -> &'static str {
        match self {
            Self::Printed => "printed",
            Self::Free => "free",
            Self::EnergyEqualToManaValue => "energyEqualToManaValue",
            Self::Foretell => "foretell",
        }
    }

    /// The cost a label names. An unknown one is a refusal rather than a
    /// guess: reading it wrong would let a card be cast for nothing.
    pub(super) fn from_label(label: &str) -> Option<Self> {
        match label {
            "printed" => Some(Self::Printed),
            "free" => Some(Self::Free),
            "energyEqualToManaValue" => Some(Self::EnergyEqualToManaValue),
            "foretell" => Some(Self::Foretell),
            _ => None,
        }
    }
}

impl Game {
    /// Whether `player` may presently play `card` from exile with `option`.
    pub(super) fn exile_play_is_permitted(
        &self,
        definition: &CardDefinition,
        option: &PlayOptionDef,
        card: GameObjectId,
        player: PlayerId,
    ) -> bool {
        self.exile_play_permission(card, player)
            .is_some_and(|permission| {
                !permission.adventure_return_only
                    || Self::is_adventure_return_option(definition, option)
            })
    }

    /// The live permission `player` holds over `card`, if any.
    pub(super) fn exile_play_permission(
        &self,
        card: GameObjectId,
        player: PlayerId,
    ) -> Option<ExilePlayPermission> {
        self.exile_play_permissions
            .iter()
            .copied()
            .find(|permission| {
                permission.card == card
                    && permission.player == player
                    // A look is not a permission to play.
                    && !permission.hidden_only
                    // "During any turn you attacked with a Rogue": asked
                    // here, because the permission outlives the turn it was
                    // granted on.
                    && permission
                        .condition
                        .is_none_or(|condition| self.exile_play_condition_holds(condition, player))
                    && permission.until_end_of_turn.is_none_or(|(owner, turn)| {
                        self.turns_started[owner.index()] == turn && self.active_player == owner
                    })
                    // "Cast it on a later turn": the turn it was exiled on
                    // is not one of them, however long that turn runs.
                    && permission.not_before_turn.is_none_or(|(owner, turn)| {
                        self.turns_started[owner.index()] > turn || self.active_player != owner
                    })
                    // Live until the holder's own turn `turn` is over, which
                    // is what "until your next end step" reaches.
                    && permission
                        .until_holder_end_step
                        .is_none_or(|(holder, turn)| self.turns_started[holder.index()] <= turn)
            })
    }

    /// "You may cast that card", with nothing about the turn bounding it:
    /// what limits the permission is the condition the clause attaches
    /// below, asked again every time the card could be played.
    pub(super) fn permit_conditional_cast_while_exiled(
        &mut self,
        card: GameObjectId,
        player: PlayerId,
    ) {
        self.exile_play_permissions.push(ExilePlayPermission {
            card,
            player,
            cost: ExilePlayCost::Printed,
            until_end_of_turn: None,
            adventure_return_only: false,
            surcharge: ManaCost::default(),
            not_before_turn: None,
            face_down: false,
            hidden_only: false,
            spend_any_color: false,
            condition: None,
            until_holder_end_step: None,
        });
    }

    /// Records what a permission just granted asks for and allows: the
    /// colours its mana may be spent as, and what has to be true when the
    /// card is played.
    pub(super) fn qualify_exile_permission(
        &mut self,
        card: GameObjectId,
        spend_any_color: bool,
        condition: Option<crate::card::ExilePlayConditionDef>,
    ) {
        if let Some(permission) = self
            .exile_play_permissions
            .iter_mut()
            .rev()
            .find(|permission| permission.card == card)
        {
            permission.spend_any_color = spend_any_color;
            permission.condition = condition;
        }
    }

    /// Whether a permission's own condition is satisfied right now.
    fn exile_play_condition_holds(
        &self,
        condition: crate::card::ExilePlayConditionDef,
        player: PlayerId,
    ) -> bool {
        match condition {
            crate::card::ExilePlayConditionDef::AttackedWithSubtypeThisTurn(subtype) => self
                .battlefield
                .iter()
                .filter(|permanent| permanent.controller == player)
                .filter(|permanent| permanent.attacked_this_turn)
                .any(|permanent| self.effective_subtypes(permanent).contains(&subtype)),
        }
    }

    /// "Look at the top four cards of your library, exile one face down."
    /// What it buys is the looking: playing it waits for whatever clause
    /// hid it to say so.
    pub(super) fn permit_look_while_exiled(&mut self, card: GameObjectId, player: PlayerId) {
        self.exile_play_permissions.push(ExilePlayPermission {
            card,
            player,
            cost: ExilePlayCost::Printed,
            until_end_of_turn: None,
            adventure_return_only: false,
            surcharge: ManaCost::default(),
            not_before_turn: None,
            face_down: true,
            hidden_only: true,
            spend_any_color: false,
            condition: None,
            until_holder_end_step: None,
        });
    }

    /// Records that a card on an adventure may come back as the creature it
    /// is. The permission never lapses: a crown that never moves keeps it,
    /// and so does an adventure nobody takes.
    pub(super) fn permit_adventure_return(&mut self, card: GameObjectId, player: PlayerId) {
        self.exile_play_permissions.push(ExilePlayPermission {
            card,
            player,
            cost: ExilePlayCost::Printed,
            until_end_of_turn: None,
            adventure_return_only: true,
            surcharge: ManaCost::default(),
            not_before_turn: None,
            face_down: false,
            hidden_only: false,
            spend_any_color: false,
            condition: None,
            until_holder_end_step: None,
        });
    }

    /// "Until end of turn, you may play those cards without paying their
    /// mana costs."
    pub(super) fn permit_free_play_this_turn(&mut self, card: GameObjectId, player: PlayerId) {
        let active = self.active_player;
        self.exile_play_permissions.push(ExilePlayPermission {
            card,
            player,
            cost: ExilePlayCost::Free,
            until_end_of_turn: Some((active, self.turns_started[active.index()])),
            adventure_return_only: false,
            surcharge: ManaCost::default(),
            not_before_turn: None,
            face_down: false,
            hidden_only: false,
            spend_any_color: false,
            condition: None,
            until_holder_end_step: None,
        });
    }

    /// "Exile the top card of your library face down. You may look at and
    /// play that card this turn." Lying face down is the whole difference
    /// from the permission below: the cost is still owed either way, and
    /// only its owner knows what the card is.
    pub(super) fn permit_face_down_play_this_turn(&mut self, card: GameObjectId, player: PlayerId) {
        let active = self.active_player;
        self.exile_play_permissions.push(ExilePlayPermission {
            card,
            player,
            cost: ExilePlayCost::Printed,
            until_end_of_turn: Some((active, self.turns_started[active.index()])),
            adventure_return_only: false,
            surcharge: ManaCost::default(),
            not_before_turn: None,
            face_down: true,
            hidden_only: false,
            spend_any_color: false,
            condition: None,
            until_holder_end_step: None,
        });
    }

    /// "You may play that card until your next end step."
    ///
    /// Longer than a turn when the card was exiled on somebody else's: the
    /// end step it runs to is the holder's own, so a discard on their turn
    /// buys the whole of yours. Recorded as the holder's turn count at which
    /// it lapses, which is this turn when it is already theirs.
    pub(super) fn permit_play_until_your_next_end_step(
        &mut self,
        card: GameObjectId,
        player: PlayerId,
    ) {
        self.exile_play_permissions.push(ExilePlayPermission {
            card,
            player,
            cost: ExilePlayCost::Printed,
            until_end_of_turn: None,
            adventure_return_only: false,
            surcharge: ManaCost::default(),
            not_before_turn: None,
            face_down: false,
            hidden_only: false,
            spend_any_color: false,
            condition: None,
            until_holder_end_step: Some((
                player,
                if self.active_player == player {
                    self.turns_started[player.index()]
                } else {
                    // Their turn: "your next end step" is the one in the
                    // turn after this, so the permission outlives it.
                    self.turns_started[player.index()].saturating_add(1)
                },
            )),
        });
    }

    /// "You may cast that card." Unlike the free play above, the cost is
    /// still owed; what the permission grants is only that exile is a legal
    /// place to cast it from.
    pub(super) fn permit_cast_this_turn(&mut self, card: GameObjectId, player: PlayerId) {
        let active = self.active_player;
        self.exile_play_permissions.push(ExilePlayPermission {
            card,
            player,
            cost: ExilePlayCost::Printed,
            until_end_of_turn: Some((active, self.turns_started[active.index()])),
            adventure_return_only: false,
            surcharge: ManaCost::default(),
            not_before_turn: None,
            face_down: false,
            hidden_only: false,
            spend_any_color: false,
            condition: None,
            until_holder_end_step: None,
        });
    }

    /// "You may cast that card by paying an amount of {E} equal to its mana
    /// value rather than paying its mana cost." Nothing states a duration, so
    /// the permission lasts as long as the card sits in exile.
    pub(super) fn permit_energy_cast(&mut self, card: GameObjectId, player: PlayerId) {
        self.exile_play_permissions.push(ExilePlayPermission {
            card,
            player,
            cost: ExilePlayCost::EnergyEqualToManaValue,
            until_end_of_turn: None,
            adventure_return_only: false,
            surcharge: ManaCost::default(),
            not_before_turn: None,
            face_down: false,
            hidden_only: false,
            spend_any_color: false,
            condition: None,
            until_holder_end_step: None,
        });
    }

    /// "For as long as that card remains exiled, its owner may play it. A
    /// spell cast this way costs `surcharge` more to cast."
    ///
    /// The permission is the owner's rather than the exiler's, and it has no
    /// duration: nothing takes it back, so it lapses only when the card
    /// leaves exile by being played.
    pub(super) fn permit_owner_play_while_exiled(
        &mut self,
        card: GameObjectId,
        owner: PlayerId,
        surcharge: ManaCost,
    ) {
        self.exile_play_permissions.push(ExilePlayPermission {
            card,
            player: owner,
            cost: ExilePlayCost::Printed,
            until_end_of_turn: None,
            adventure_return_only: false,
            surcharge,
            not_before_turn: None,
            face_down: false,
            hidden_only: false,
            spend_any_color: false,
            condition: None,
            until_holder_end_step: None,
        });
    }

    /// "Exile this card from your hand face down. Cast it on a later turn
    /// for its foretell cost."
    pub(super) fn permit_foretold_cast(&mut self, card: GameObjectId, owner: PlayerId) {
        let turn = self.turns_started[owner.index()];
        self.exile_play_permissions.push(ExilePlayPermission {
            card,
            player: owner,
            cost: ExilePlayCost::Foretell,
            until_end_of_turn: None,
            adventure_return_only: false,
            surcharge: ManaCost::default(),
            not_before_turn: Some((owner, turn)),
            face_down: true,
            hidden_only: false,
            spend_any_color: false,
            condition: None,
            until_holder_end_step: None,
        });
    }

    /// "Exile this card from your hand. Cast it as a sorcery on a later turn
    /// without paying its mana cost." The plot cost was paid to get it here,
    /// so what remains is a free cast that has to wait for another turn. The
    /// card lies face up: everybody can see what is coming.
    pub(super) fn permit_plotted_cast(&mut self, card: GameObjectId, owner: PlayerId) {
        let turn = self.turns_started[owner.index()];
        self.exile_play_permissions.push(ExilePlayPermission {
            card,
            player: owner,
            cost: ExilePlayCost::Free,
            until_end_of_turn: None,
            adventure_return_only: false,
            surcharge: ManaCost::default(),
            not_before_turn: Some((owner, turn)),
            face_down: false,
            hidden_only: false,
            spend_any_color: false,
            condition: None,
            until_holder_end_step: None,
        });
    }

    /// Whether this exiled card is lying face down, which today means it was
    /// foretold. Its owner knows what it is; nobody else does.
    pub(super) fn exiled_card_is_face_down(&self, card: GameObjectId) -> bool {
        self.exile_play_permissions
            .iter()
            .any(|permission| permission.card == card && permission.face_down)
    }

    /// One player's exile as another sees it. A card lying face down is
    /// absent from the list rather than shown, unless the viewer is the one
    /// who put it there.
    pub(super) fn observed_exile(
        &self,
        owner: PlayerId,
        viewer: PlayerId,
    ) -> Vec<super::PublicCard> {
        self.players[owner.index()]
            .exile
            .iter()
            .filter(|card| viewer == owner || !self.exiled_card_is_face_down(card.id))
            .map(|card| (card.id, card.definition))
            .collect()
    }

    /// How many cards are lying face down in one player's exile. Both
    /// players may count them; only their owner knows what they are.
    pub(super) fn face_down_exile_size(&self, owner: PlayerId) -> usize {
        self.players[owner.index()]
            .exile
            .iter()
            .filter(|card| self.exiled_card_is_face_down(card.id))
            .count()
    }

    /// What this player owes on top of a card's own cost to play it out of
    /// exile, which is nothing unless a permission says otherwise.
    pub(super) fn exile_play_surcharge(&self, card: GameObjectId, player: PlayerId) -> ManaCost {
        self.exile_play_permission(card, player)
            .map_or_else(ManaCost::default, |permission| permission.surcharge)
    }

    /// The energy `player` owes to cast `card` from exile, if that is how the
    /// permission they hold over it is paid.
    pub(super) fn exile_energy_cost(&self, card: GameObjectId, player: PlayerId) -> Option<u16> {
        let permission = self.exile_play_permission(card, player)?;
        if permission.cost != ExilePlayCost::EnergyEqualToManaValue {
            return None;
        }
        let (_, instance) = self.card_in_nonbattlefield_zone(card)?;
        Some(
            self.catalog
                .get(instance.definition)?
                .rules
                .printed_mana_cost()
                .mana_value(),
        )
    }

    /// Drops the permission a play has just consumed.
    pub(super) fn consume_exile_play_permission(&mut self, card: GameObjectId) {
        self.exile_play_permissions
            .retain(|permission| permission.card != card);
    }
}
