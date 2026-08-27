use serde_json::Value;

use super::action_json::protocol_actions;
use super::catalog_json::catalog_json_for_format;
use super::decks::{deck_by_name_for_format, parse_format_slug};
use super::json_common::{seat_by_name, seat_name, zone_cards_json};
use super::observation_json::observation_json_for_format;
use super::{ACTION_LIMIT, BotGame, Opponent, OpponentPolicy};
use crate::ids::CardDefinitionId;
use crate::policy::Policy;
use crate::{Action, Format, Game, GameResult, HandcraftedPolicy, PlayerId, RandomPolicy, poc};

/// Returns whether a supplied open-world value contains the complete value
/// rebuilt by this engine. Objects may add members; arrays and scalar values
/// retain their exact meaning.
fn contains_rebuilt_value(supplied: &Value, rebuilt: &Value) -> bool {
    contains_rebuilt_value_inner(supplied, rebuilt, false)
}

/// SPZ VENDOR PATCH (LOCAL ONLY): the same containment check, told whether
/// the supplied observation came from a format-8 engine.
///
/// Format 9 added fields THROUGHOUT the observation, not only at the top
/// level -- each battlefield permanent gained a `counters` list that a
/// format-8 observation never carried anywhere. Those keys cannot be
/// verified against a source that does not express them, so under `from_v8`
/// a key the supplied object simply does not have is skipped.
///
/// This narrows the check, so be exact about what survives: every field the
/// format-8 wire form DID carry is still compared byte for byte, arrays
/// still have to be the same length, and a key present in both is still
/// compared. What is given up is only the ability to verify state that the
/// older format never transmitted.
fn contains_rebuilt_value_inner(supplied: &Value, rebuilt: &Value, from_v8: bool) -> bool {
    match (supplied, rebuilt) {
        (Value::Object(supplied), Value::Object(rebuilt)) => {
            rebuilt.iter().all(|(field, value)| {
                match supplied.get(field) {
                    Some(supplied) => {
                        contains_rebuilt_value_inner(supplied, value, from_v8)
                    }
                    None => from_v8,
                }
            })
        }
        (Value::Array(supplied), Value::Array(rebuilt)) => {
            supplied.len() == rebuilt.len()
                && supplied.iter().zip(rebuilt).all(|(supplied, rebuilt)| {
                    contains_rebuilt_value_inner(supplied, rebuilt, from_v8)
                })
        }
        _ => supplied == rebuilt,
    }
}

impl BotGame {
    /// Constructs a local determinization from a redacted observation and a
    /// separate hidden-zone hypothesis. The hypothesis includes both players'
    /// libraries and outside-game cards, even when those latter lists are empty.
    /// `rollout_seed` initializes only the reconstructed game's future
    /// randomness; observations never carry the host game's seed or RNG state.
    ///
    /// # Errors
    ///
    /// Returns a message for a version mismatch, malformed or inconsistent
    /// hidden zones, checkpoint state without stable catalog semantics, or a
    /// reconstruction that changes the public observation.
    pub fn from_observation_json(
        observation_json: &str,
        hidden_json: &str,
        rollout_seed: u64,
    ) -> Result<Self, String> {
        let mut observation: Value = serde_json::from_str(observation_json)
            .map_err(|error| format!("bad observation JSON: {error}"))?;
        // SPZ VENDOR PATCH (LOCAL ONLY): the public server still emits
        // checkpoint format 8. Upconvert before every gate below, so the
        // parse AND the field-by-field rebuild comparison both see one
        // format. See upconvert_checkpoint_v8.
        let from_v8 = upconvert_checkpoint_v8(&mut observation)?;
        let observation = observation;
        let hidden: Value = serde_json::from_str(hidden_json)
            .map_err(|error| format!("bad hidden-state JSON: {error}"))?;
        if observation["protocolVersion"].as_u64() != Some(u64::from(super::PROTOCOL_VERSION)) {
            return Err(format!(
                "observation protocol version does not match {}",
                super::PROTOCOL_VERSION
            ));
        }
        // SPZ VENDOR PATCH (LOCAL ONLY -- never propose upstream): accept a
        // foreign simulation fingerprint when the operator names it.
        //
        // The fingerprint hashes every simulation source file, so ANY patch of
        // ours changes it -- our additive accessors included. It also pins the
        // exact upstream revision, and the public server runs a revision that
        // is neither our pin nor upstream HEAD. Without this, reconstruction
        // rejects every server observation, search never runs, and the hosted
        // bot silently degrades to a raw 1-ply pick.
        //
        // This is a REAL RISK, not a formality: if the server's rules actually
        // differ from ours, reconstruction yields a subtly wrong world and
        // search reasons about the wrong game. It is opt-in per process and
        // must name the exact fingerprint being trusted.
        if observation["simulationFingerprint"].as_str() != Some(super::SIMULATION_FINGERPRINT) {
            let accepted = std::env::var("SPZ_ACCEPT_FINGERPRINT").unwrap_or_default();
            let seen = observation["simulationFingerprint"].as_str().unwrap_or("");
            if accepted.is_empty() || accepted != seen {
                return Err(format!(
                    "observation simulation fingerprint does not match {}",
                    super::SIMULATION_FINGERPRINT
                ));
            }
        }
        let format = parse_format_slug(
            observation["format"]
                .as_str()
                .ok_or("observation format must be a string")?,
        )?;
        let viewer = seat_by_name(
            observation["seat"]
                .as_str()
                .ok_or("observation seat must be a string")?,
        )
        .ok_or("observation seat must be p1 or p2")?;
        let catalog = poc::catalog().map_err(|error| error.to_string())?;
        let game = Game::from_observation_checkpoint(
            catalog.clone(),
            format,
            &observation,
            &hidden,
            rollout_seed,
        )?;
        let rebuilt = Self {
            game,
            catalog,
            format,
            opponent_seat: viewer.opponent(),
            opponent: OpponentPolicy::External,
        };
        let rebuilt_observation: Value = serde_json::from_str(&rebuilt.observe_json(viewer))
            .map_err(|error| format!("rebuilt observation was invalid: {error}"))?;
        if !contains_rebuilt_value_inner(
            &observation["legalActions"],
            &rebuilt_observation["legalActions"],
            from_v8,
        ) {
            return Err("checkpoint rebuilt a different legal-action list".into());
        }
        let rebuilt_fields = rebuilt_observation
            .as_object()
            .ok_or("rebuilt observation must be an object")?;
        for (field, rebuilt_value) in rebuilt_fields {
            if matches!(field.as_str(), "engineVersion" | "protocolCapabilities") {
                continue;
            }
            // SPZ VENDOR PATCH (LOCAL ONLY -- never propose upstream): third
            // and last arm of the foreign-fingerprint acceptance. The rebuilt
            // observation stamps OUR fingerprint while the supplied one
            // carries the server's, so this field-by-field comparison would
            // reject even after the two explicit gates pass. Skipped only
            // when the operator opted in to that exact fingerprint.
            // SPZ VENDOR PATCH (LOCAL ONLY): format 9 ADDED open-counter
            // fields to the observation (playerCounters, cardCounters, ...).
            // A format-8 observation has none of them, so the rebuilt
            // observation carries fields the supplied one cannot match.
            //
            // Allow that ONLY when the rebuild also found nothing there. If
            // our engine derived counters the format-8 source never had,
            // the two boards really do differ and this must still reject.
            if from_v8
                && observation.get(field).is_none()
                && is_empty_addition(rebuilt_value)
            {
                continue;
            }
            if field.as_str() == "simulationFingerprint" {
                let accepted = std::env::var("SPZ_ACCEPT_FINGERPRINT").unwrap_or_default();
                if !accepted.is_empty()
                    && observation["simulationFingerprint"].as_str() == Some(accepted.as_str())
                {
                    continue;
                }
            }
            // SPZ VENDOR PATCH (LOCAL ONLY): the nested checkpoint embeds the
            // fingerprint too, so under the opt-in we normalise THAT STRING
            // ONLY and still compare every other byte of real game state.
            let supplied_owned = observation.get(field).map(|supplied| {
                let accepted = std::env::var("SPZ_ACCEPT_FINGERPRINT").unwrap_or_default();
                if field.as_str() == "checkpoint" && !accepted.is_empty() {
                    let mut copy = supplied.clone();
                    if let Some(object) = copy.as_object_mut() {
                        if object.get("simulationFingerprint").and_then(Value::as_str)
                            == Some(accepted.as_str())
                        {
                            object.insert(
                                "simulationFingerprint".to_owned(),
                                Value::String(super::SIMULATION_FINGERPRINT.to_owned()),
                            );
                        }
                    }
                    copy
                } else {
                    supplied.clone()
                }
            });
            if !supplied_owned
                .as_ref()
                .is_some_and(|supplied| {
                    contains_rebuilt_value_inner(supplied, rebuilt_value, from_v8)
                })
            {
                return Err(format!(
                    "checkpoint rebuilt a different public observation field: {field}"
                ));
            }
        }
        Ok(rebuilt)
    }

    /// Starts a game. `p1_deck`/`p2_deck` name built-in decks; `opponent`
    /// plays `opponent_seat` unless it is [`Opponent::External`].
    ///
    /// # Errors
    ///
    /// Returns a message when a deck name is unknown, the game cannot be
    /// built, or the scripted opponent cannot reach the first decision.
    pub fn new(
        p1_deck: &str,
        p2_deck: &str,
        opponent: Opponent,
        opponent_seat: PlayerId,
        seed: u64,
    ) -> Result<Self, String> {
        Self::new_with_format(
            Format::OldSchool9394,
            p1_deck,
            p2_deck,
            opponent,
            opponent_seat,
            seed,
        )
    }

    /// Starts a game using decks and rules from `format`.
    ///
    /// # Errors
    ///
    /// Returns a message when a deck does not belong to `format`, the game
    /// cannot be built, or the scripted opponent cannot reach a decision.
    pub fn new_with_format(
        format: Format,
        p1_deck: &str,
        p2_deck: &str,
        opponent: Opponent,
        opponent_seat: PlayerId,
        seed: u64,
    ) -> Result<Self, String> {
        let catalog = poc::catalog().map_err(|error| error.to_string())?;
        let deck_one = deck_by_name_for_format(format, p1_deck)
            .ok_or_else(|| format!("unknown deck for {}: {p1_deck}", format.slug()))?;
        let deck_two = deck_by_name_for_format(format, p2_deck)
            .ok_or_else(|| format!("unknown deck for {}: {p2_deck}", format.slug()))?;
        let game = Game::new_with_format(format, catalog.clone(), [deck_one, deck_two], seed)
            .map_err(|error| error.to_string())?;
        let opponent = match opponent {
            Opponent::External => OpponentPolicy::External,
            Opponent::Random => OpponentPolicy::Random(RandomPolicy::new(seed ^ 0x00b0_7b07)),
            Opponent::Handcrafted => {
                OpponentPolicy::Handcrafted(HandcraftedPolicy::new(catalog.clone()))
            }
        };
        let mut bot_game = Self {
            game,
            catalog,
            format,
            opponent_seat,
            opponent,
        };
        bot_game.advance()?;
        Ok(bot_game)
    }

    /// Starts a game from a JSON config, the single entry point the FFI and
    /// Python bindings share:
    ///
    /// ```json
    /// {"format": "old-school-93-94", "p1Deck": "Sligh", "p2Deck": "The Deck",
    ///  "opponent": "handcrafted", "opponentSeat": "p2", "seed": 42}
    /// ```
    ///
    /// `format` defaults to `"old-school-93-94"`; `opponent` is `"random"`,
    /// `"handcrafted"`, or `"external"`; `opponentSeat` defaults to `"p2"`.
    ///
    /// # Errors
    ///
    /// Returns a message for malformed JSON, unknown deck or opponent names,
    /// or a game that cannot start.
    pub fn from_config_json(config: &str) -> Result<Self, String> {
        let value: Value =
            serde_json::from_str(config).map_err(|error| format!("bad config JSON: {error}"))?;
        let field = |name: &str| -> Result<&str, String> {
            value[name]
                .as_str()
                .ok_or_else(|| format!("config field {name} must be a string"))
        };
        let opponent = match field("opponent").unwrap_or("handcrafted") {
            "external" => Opponent::External,
            "random" => Opponent::Random,
            "handcrafted" => Opponent::Handcrafted,
            other => return Err(format!("unknown opponent: {other}")),
        };
        let opponent_seat = seat_by_name(field("opponentSeat").unwrap_or("p2"))
            .ok_or_else(|| "opponentSeat must be \"p1\" or \"p2\"".to_string())?;
        let format = match value.get("format") {
            None => Format::OldSchool9394,
            Some(value) => parse_format_slug(
                value
                    .as_str()
                    .ok_or_else(|| "config field format must be a string".to_string())?,
            )?,
        };
        let seed = value["seed"].as_u64().unwrap_or(0);
        Self::new_with_format(
            format,
            field("p1Deck")?,
            field("p2Deck")?,
            opponent,
            opponent_seat,
            seed,
        )
    }

    /// The format whose rules and deck registry this game uses.
    #[must_use]
    pub const fn format(&self) -> Format {
        self.format
    }

    /// The seat that must act next, or `None` when the game is over.
    #[must_use]
    pub fn decision_seat(&self) -> Option<PlayerId> {
        self.game.decision_player()
    }

    /// [`Self::decision_seat`] as a protocol seat name.
    #[must_use]
    pub fn decision_seat_name(&self) -> Option<&'static str> {
        self.decision_seat().map(seat_name)
    }

    /// A seat's hand as `{objectId, definition}` JSON, unredacted.
    ///
    /// This is the simulation surface, not the protocol surface. It reports
    /// what is really there so a search bot can rearrange hidden state for a
    /// rollout; [`Self::observe_json`] remains the redacted view a client
    /// should be shown.
    #[must_use]
    pub fn hand_json(&self, seat: PlayerId) -> String {
        zone_cards_json(&self.game.hand(seat)).to_string()
    }

    /// A seat's library, top card first. See [`Self::hand_json`].
    #[must_use]
    pub fn library_json(&self, seat: PlayerId) -> String {
        zone_cards_json(&self.game.library(seat)).to_string()
    }

    /// Replaces a seat's hand with exactly these card definitions.
    ///
    /// # Errors
    ///
    /// Returns the zone error as a string when a definition is not in the
    /// catalog this game was built with.
    pub fn set_hand(&mut self, seat: PlayerId, cards: &[CardDefinitionId]) -> Result<(), String> {
        self.game
            .set_hand(seat, cards)
            .map_err(|error| error.to_string())
    }

    /// Replaces a seat's library, top card first. See [`Self::set_hand`].
    ///
    /// # Errors
    ///
    /// Returns the zone error as a string under the same conditions as
    /// [`Self::set_hand`].
    pub fn set_library(
        &mut self,
        seat: PlayerId,
        cards: &[CardDefinitionId],
    ) -> Result<(), String> {
        self.game
            .set_library(seat, cards)
            .map_err(|error| error.to_string())
    }

    /// SPZ VENDOR PATCH (proposed upstream): hand the reconstructed
    /// core game to a native search bot. A hosted bot builds worlds
    /// with `from_observation_json` and then wants typed observations
    /// and native action application for its playouts -- everything the
    /// core `Game` already offers -- without round-tripping JSON on
    /// every ply.
    #[must_use]
    pub fn into_core_game(self) -> crate::Game {
        self.game
    }

    /// SPZ VENDOR PATCH (proposed upstream, same rationale): borrow the
    /// core game for typed observation without consuming the wrapper,
    /// so a native bot can featurize every ply JSON-free while the
    /// wrapper keeps driving any built-in opponent.
    #[must_use]
    pub fn core_game(&self) -> &crate::Game {
        &self.game
    }

    /// The observation for one seat as canonical protocol JSON.
    #[must_use]
    pub fn observe_json(&self, seat: PlayerId) -> String {
        let observation = self.game.observe(seat);
        let actions = protocol_actions(&observation);
        observation_json_for_format(
            &self.catalog,
            self.format,
            &observation,
            self.game.in_pregame(),
            &actions,
        )
        .to_string()
    }

    /// The number of legal actions for the seat that must act, so FFI
    /// callers can pick an index without parsing JSON.
    #[must_use]
    pub fn legal_action_count(&self) -> usize {
        self.decision_seat()
            .map_or(0, |seat| protocol_actions(&self.game.observe(seat)).len())
    }

    /// The finished game's result, if any, as protocol JSON.
    #[must_use]
    pub fn result(&self) -> Option<GameResult> {
        self.game.observe(PlayerId::One).result
    }

    /// Plays the given index from the acting seat's `legalActions`, then lets
    /// a scripted opponent play until the driven seat has a real choice.
    ///
    /// # Errors
    ///
    /// Returns a message when the game is over, the index is out of range, or
    /// the engine rejects the action.
    pub fn act(&mut self, action_index: usize) -> Result<(), String> {
        let seat = self.decision_seat().ok_or("the game is over")?;
        let actions = protocol_actions(&self.game.observe(seat));
        let action = actions.get(action_index).cloned().ok_or_else(|| {
            format!(
                "action index {action_index} out of range ({} legal actions)",
                actions.len()
            )
        })?;
        self.game
            .apply(seat, action)
            .map_err(|error| error.to_string())?;
        self.advance()
    }

    /// Answers the pending decision with an explicit set of option ids, for
    /// multi-pick decisions where the default expansion is not what you want.
    /// The observation's `decision` object lists the options and bounds.
    ///
    /// # Errors
    ///
    /// Returns a message when no decision is pending or the engine rejects
    /// the selection.
    pub fn choose_decision(&mut self, option_ids: &[u32]) -> Result<(), String> {
        let seat = self.decision_seat().ok_or("the game is over")?;
        let observation = self.game.observe(seat);
        let decision = observation
            .decision
            .as_ref()
            .ok_or("no decision is pending")?;
        self.game
            .apply(
                seat,
                Action::ChooseDecision {
                    decision: decision.id,
                    options: option_ids.to_vec(),
                },
            )
            .map_err(|error| error.to_string())?;
        self.advance()
    }

    /// Runs the scripted opponent until the driven seat must make a real
    /// choice or the game ends.
    fn advance(&mut self) -> Result<(), String> {
        for _ in 0..ACTION_LIMIT {
            let Some(player) = self.game.decision_player() else {
                return Ok(());
            };
            if player != self.opponent_seat {
                return Ok(());
            }
            let observation = self.game.observe(player);
            let policy: &mut dyn Policy = match &mut self.opponent {
                OpponentPolicy::External => return Ok(()),
                OpponentPolicy::Random(policy) => policy,
                OpponentPolicy::Handcrafted(policy) => policy,
            };
            let action = policy
                .choose_action(&observation)
                .ok_or("the scripted opponent returned no action")?;
            self.game
                .apply(player, action)
                .map_err(|error| error.to_string())?;
        }
        Err("the game exceeded its action limit".to_string())
    }

    /// The catalog as protocol JSON.
    #[must_use]
    pub fn catalog_json(&self) -> String {
        catalog_json_for_format(&self.catalog, self.format).to_string()
    }
}

/// SPZ VENDOR PATCH (LOCAL ONLY -- never propose upstream): accept a
/// format-8 checkpoint by upconverting it to format 9.
///
/// The public server runs an engine OLDER than our pin and still emits
/// checkpoint format 8, so every server observation was rejected with
/// "checkpoint version 8 does not match 9" and the hosted bot silently
/// played a 1-ply fallback.
///
/// Format 9 replaced each permanent's POSITIONAL `counters` array with
/// sparse `{name, count}` entries. Converting needs format 8's index
/// order, which is NOT the same as format 9's registry order: they agree
/// through index 26 and then diverge, because 9 inserted the keyword
/// counters at 27. Mapping by position into the v9 table would therefore
/// put the wrong counters on permanents from index 27 on -- silently.
/// This table is format 8's own `CounterKind::index()`/`name()` pairing,
/// read out of the pre-bump revision, and it is checked empirically: a
/// hosted game against Icatian Javelineers ("enters with a javelin
/// counter") carries a 1 at index 1, which this table names "javelin".
const V8_COUNTER_NAMES: [&str; 34] = ["+1/+1", "javelin", "muster", "charge", "loyalty", "spore", "-1/-1", "+1/+2", "credit", "tide", "-0/-2", "time", "doom", "carrion", "pupa", "sleep", "vitality", "corpse", "wind", "storage", "mining", "fuse", "fade", "depletion", "wish", "level", "finality", "flying", "lifelink", "age", "chorus", "silver", "stun", "rev"];

fn upconvert_checkpoint_v8(observation: &mut Value) -> Result<bool, String> {
    if observation.get("checkpoint").and_then(|c| c.get("version"))
        .and_then(Value::as_u64) != Some(8)
    {
        return Ok(false);
    }
    let checkpoint = observation
        .get_mut("checkpoint")
        .expect("just read the checkpoint's version");
    convert_positional_counters(checkpoint)?;
    if let Some(map) = checkpoint.as_object_mut() {
        map.insert(
            "version".to_owned(),
            Value::from(u64::from(crate::protocol::CHECKPOINT_VERSION)),
        );
    }
    // Format 9 also added the OPEN `playerCounters` collection to the
    // observation, keeping `poison`/`energy` as compatibility projections.
    // A format-8 observation has no such field, so the rebuilt observation
    // carries one the supplied observation cannot match and the
    // field-by-field check rejects the world.
    //
    // Synthesise the empty collection -- but ONLY when there is genuinely
    // nothing to represent. If this game has poison or energy on the table
    // we do not know how format 9 would project it, and guessing would put
    // a silently wrong board in front of the search. Fail instead.
    if observation.get("playerCounters").is_none() {
        for field in ["poison", "energy"] {
            if let Some(Value::Array(values)) = observation.get(field) {
                if values.iter().any(|v| v.as_u64().unwrap_or(0) != 0) {
                    return Err(format!(
                        "format-8 observation carries {field} counters; this \
                         engine cannot project them into playerCounters"
                    ));
                }
            }
        }
        if let Some(map) = observation.as_object_mut() {
            map.insert(
                "playerCounters".to_owned(),
                Value::Array(vec![Value::Array(Vec::new()),
                                  Value::Array(Vec::new())]),
            );
        }
    }
    Ok(true)
}

fn convert_positional_counters(value: &mut Value) -> Result<(), String> {
    match value {
        Value::Object(map) => {
            let positional = matches!(map.get("counters"),
                Some(Value::Array(items))
                    if items.iter().all(serde_json::Value::is_number));
            if positional {
                let Some(Value::Array(items)) = map.get("counters") else {
                    unreachable!("just matched a numeric counters array")
                };
                if items.len() > V8_COUNTER_NAMES.len() {
                    return Err(format!(
                        "format-8 checkpoint carries {} counter slots, more \
                         than the {} this engine can name",
                        items.len(),
                        V8_COUNTER_NAMES.len()
                    ));
                }
                let mut sparse = Vec::new();
                for (index, item) in items.iter().enumerate() {
                    let count = item.as_u64().unwrap_or(0);
                    if count == 0 {
                        continue;
                    }
                    let mut entry = serde_json::Map::new();
                    entry.insert("name".to_owned(),
                                 Value::from(V8_COUNTER_NAMES[index]));
                    entry.insert("count".to_owned(), Value::from(count));
                    sparse.push(Value::Object(entry));
                }
                map.insert("counters".to_owned(), Value::Array(sparse));
            }
            for (_, child) in map.iter_mut() {
                convert_positional_counters(child)?;
            }
        }
        Value::Array(items) => {
            for item in items {
                convert_positional_counters(item)?;
            }
        }
        _ => {}
    }
    Ok(())
}

/// SPZ VENDOR PATCH (LOCAL ONLY): "the rebuild found nothing here", for
/// fields format 9 added that format 8 never carried.
fn is_empty_addition(value: &Value) -> bool {
    match value {
        Value::Null => true,
        Value::Array(items) => items.iter().all(is_empty_addition),
        Value::Object(map) => map.is_empty(),
        Value::Number(number) => number.as_u64() == Some(0),
        _ => false,
    }
}
