/// Parses the current snapshot while retaining protocol-28 fields whose
/// engine representation has moved elsewhere.
fn parse_compatible_game_snapshot(checkpoint_value: &Value) -> Result<GameSnapshot, String> {
    let version = u32_field(checkpoint_value, "version")
        .map_err(|error| format!("invalid game snapshot: {error}"))?;
    if version != crate::protocol::CHECKPOINT_VERSION {
        return Err(format!(
            "checkpoint version {version} does not match {}",
            crate::protocol::CHECKPOINT_VERSION
        ));
    }
    let fingerprint = str_field(checkpoint_value, "simulationFingerprint")
        .map_err(|error| format!("invalid game snapshot: {error}"))?;
    // SPZ VENDOR PATCH (LOCAL ONLY -- never propose upstream): the checkpoint
    // arm of the same foreign-fingerprint acceptance applied in
    // protocol/bot_game.rs. A server observation carries the server's
    // fingerprint in BOTH places, so relaxing only the outer gate still
    // rejects. Same opt-in, same risk: see the note in bot_game.rs.
    if fingerprint != crate::protocol::SIMULATION_FINGERPRINT {
        let accepted = std::env::var("SPZ_ACCEPT_FINGERPRINT").unwrap_or_default();
        if accepted.is_empty() || accepted != fingerprint {
            return Err(format!(
                "checkpoint simulation fingerprint {fingerprint:?} does not match {}",
                crate::protocol::SIMULATION_FINGERPRINT
            ));
        }
    }
    let checkpoint: GameSnapshot = serde_json::from_value(checkpoint_value.clone())
        .map_err(|error| format!("invalid game snapshot: {error}"))?;
    if checkpoint.channel_active != [false; 2] {
        return Err(
            "checkpoint legacy channelActive state must be represented by ongoingEffects".into(),
        );
    }
    if checkpoint.has_deferred_state {
        return Err(
            "checkpoint contains executable rules state without stable catalog semantics".into(),
        );
    }
    Ok(checkpoint)
}
