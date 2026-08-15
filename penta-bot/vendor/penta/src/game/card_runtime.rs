use super::{
    DecisionOption, Game, GameObjectId, PlayerId, Target, TargetIndex, TargetSelection, fmt,
};

/// One card-owned stack-ability entry point. Equality and hashing use the
/// stable key rather than a function address so frozen rules remain
/// deterministic across builds and platforms.
#[derive(Clone, Copy)]
pub(crate) struct CardAbilityResolver {
    key: &'static str,
    start: for<'game> fn(&mut CardRuntime<'game>, &ResolvedAbility),
}

impl CardAbilityResolver {
    #[must_use]
    pub(crate) const fn new(
        key: &'static str,
        start: for<'game> fn(&mut CardRuntime<'game>, &ResolvedAbility),
    ) -> Self {
        Self { key, start }
    }

    pub(super) fn resolve(self, runtime: &mut CardRuntime<'_>, ability: &ResolvedAbility) {
        (self.start)(runtime, ability);
    }
}

impl fmt::Debug for CardAbilityResolver {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("CardAbilityResolver")
            .field("key", &self.key)
            .finish_non_exhaustive()
    }
}

impl PartialEq for CardAbilityResolver {
    fn eq(&self, other: &Self) -> bool {
        self.key == other.key
    }
}

impl Eq for CardAbilityResolver {}

impl std::hash::Hash for CardAbilityResolver {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        std::hash::Hash::hash(&self.key, state);
    }
}

/// Frozen public facts supplied to a card-owned resolver after target fizzle
/// checking has completed.
#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct ResolvedAbility {
    pub(super) controller: PlayerId,
    pub(super) targets: Vec<TargetSelection>,
}

impl ResolvedAbility {
    #[must_use]
    pub(crate) const fn controller(&self) -> PlayerId {
        self.controller
    }

    #[must_use]
    pub(crate) fn target_player(&self, index: TargetIndex) -> Option<PlayerId> {
        self.targets.get(index.index()).and_then(|selection| {
            selection.targets().iter().find_map(|target| match target {
                Target::Player(player) => Some(*player),
                Target::Card(_) | Target::Permanent(_) | Target::Spell(_) => None,
            })
        })
    }
}

/// Result of assigning a frozen set of object-backed options to two piles.
#[derive(Clone, Debug)]
pub(crate) struct PileSplit {
    pub(super) resolving_controller: PlayerId,
    pub(super) subject: PlayerId,
    pub(super) first: Vec<DecisionOption>,
    pub(super) second: Vec<DecisionOption>,
}

impl PileSplit {
    #[must_use]
    pub(crate) const fn subject(&self) -> PlayerId {
        self.subject
    }
}

/// Result of choosing one of two piles.
#[derive(Clone, Debug)]
pub(crate) struct PileChoice {
    pub(super) resolving_controller: PlayerId,
    pub(super) subject: PlayerId,
    pub(super) chosen: Vec<GameObjectId>,
    pub(super) unchosen: Vec<GameObjectId>,
}

impl PileChoice {
    #[must_use]
    pub(crate) const fn resolving_controller(&self) -> PlayerId {
        self.resolving_controller
    }

    #[must_use]
    pub(crate) const fn subject(&self) -> PlayerId {
        self.subject
    }

    #[must_use]
    pub(crate) fn into_parts(self) -> (Vec<GameObjectId>, Vec<GameObjectId>) {
        (self.chosen, self.unchosen)
    }
}

#[derive(Clone, Copy, Debug)]
pub(crate) struct PilesSeparated {
    key: &'static str,
    resolve: for<'game> fn(&mut CardRuntime<'game>, PileSplit),
}

impl PartialEq for PilesSeparated {
    fn eq(&self, other: &Self) -> bool {
        self.key == other.key
    }
}

impl Eq for PilesSeparated {}

impl PilesSeparated {
    #[must_use]
    pub(crate) const fn new(
        key: &'static str,
        resolve: for<'game> fn(&mut CardRuntime<'game>, PileSplit),
    ) -> Self {
        Self { key, resolve }
    }

    #[must_use]
    pub(crate) const fn key(self) -> &'static str {
        self.key
    }

    pub(super) fn run(self, runtime: &mut CardRuntime<'_>, piles: PileSplit) {
        (self.resolve)(runtime, piles);
    }
}

#[derive(Clone, Copy, Debug)]
pub(crate) struct PileChosen {
    key: &'static str,
    resolve: for<'game> fn(&mut CardRuntime<'game>, PileChoice),
}

impl PartialEq for PileChosen {
    fn eq(&self, other: &Self) -> bool {
        self.key == other.key
    }
}

impl Eq for PileChosen {}

impl PileChosen {
    #[must_use]
    pub(crate) const fn new(
        key: &'static str,
        resolve: for<'game> fn(&mut CardRuntime<'game>, PileChoice),
    ) -> Self {
        Self { key, resolve }
    }

    #[must_use]
    pub(crate) const fn key(self) -> &'static str {
        self.key
    }

    pub(super) fn run(self, runtime: &mut CardRuntime<'_>, choice: PileChoice) {
        (self.resolve)(runtime, choice);
    }
}

/// Narrow capability surface available to card-owned resolution callbacks.
/// Its game reference is private so set modules cannot mutate unrelated state.
pub(crate) struct CardRuntime<'game> {
    pub(super) game: &'game mut Game,
}

impl CardRuntime<'_> {
    #[must_use]
    pub(crate) fn controlled_permanents(&self, player: PlayerId) -> Vec<GameObjectId> {
        self.game
            .battlefield
            .iter()
            .filter(|permanent| permanent.controller == player)
            .map(|permanent| permanent.card.id)
            .collect()
    }

    pub(crate) fn queue_permanent_partition(
        &mut self,
        resolving_controller: PlayerId,
        divider: PlayerId,
        subject: PlayerId,
        permanents: &[GameObjectId],
        on_complete: PilesSeparated,
    ) {
        let options = self.game.permanent_decision_options(permanents);
        self.game.queue_two_pile_partition(
            resolving_controller,
            divider,
            subject,
            "Choose the permanents in pile 1",
            options,
            on_complete,
        );
    }

    pub(crate) fn queue_pile_choice(
        &mut self,
        chooser: PlayerId,
        piles: PileSplit,
        prompt: impl Into<String>,
        option_prefix: &str,
        on_complete: PileChosen,
    ) {
        self.game
            .queue_card_owned_pile_choice(chooser, piles, prompt, option_prefix, on_complete);
    }

    pub(crate) fn sacrifice_permanents_simultaneously(
        &mut self,
        permanents: &[GameObjectId],
        player: PlayerId,
        resolving_controller: PlayerId,
    ) {
        if !self
            .game
            .can_be_forced_to_sacrifice(player, resolving_controller)
        {
            return;
        }
        let controlled = permanents
            .iter()
            .copied()
            .filter(|id| self.game.permanent_controller(*id) == Some(player))
            .collect::<Vec<_>>();
        self.game.move_permanents_to_graveyard(&controlled);
    }
}
