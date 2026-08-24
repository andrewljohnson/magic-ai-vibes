//! A bot copied from this repository has to be one the server will accept.
//!
//! The wire epoch is negotiated exactly: the registry refuses a bot whose
//! declared `protocolVersion` is not the server's, with no fallback beyond
//! the pre-negotiation legacy value. So a published example that names last
//! epoch's number is not merely stale documentation -- it is a bot that gets
//! a 409 at registration and never plays a game.
//!
//! This has happened twice. `examples/python/hosted_bot.py` sat at protocol
//! 22 against a protocol-28 server, which is how issue 95's reporter ended up
//! debugging a bot the server would not have taken; and the very next epoch
//! bump left both the example and the guide's snippet at 28 against 29, live,
//! within a day. Nothing failed, because nothing was watching.
//!
//! These tests watch. Bumping `PROTOCOL_VERSION` now fails here until the
//! published examples are bumped with it.

use std::fs;
use std::path::{Path, PathBuf};

/// This file lives at `tests/`, so the repository root is one level up.
fn repository_root() -> PathBuf {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    assert!(
        root.join("Cargo.toml").is_file(),
        "expected a repository root at {}",
        root.display(),
    );
    root
}

fn read(relative: &str) -> String {
    let path = repository_root().join(relative);
    fs::read_to_string(&path)
        .unwrap_or_else(|error| panic!("cannot read {}: {error}", path.display()))
}

/// Every `"protocolVersion": <n>` a file declares, in order.
fn declared_versions(source: &str) -> Vec<u32> {
    source
        .match_indices("\"protocolVersion\"")
        .filter_map(|(at, _)| {
            let rest = &source[at..];
            let colon = rest.find(':')?;
            let digits: String = rest[colon + 1..]
                .trim_start()
                .chars()
                .take_while(char::is_ascii_digit)
                .collect();
            digits.parse().ok()
        })
        .collect()
}

/// Files that publish a bot someone is meant to copy and run as-is.
const PUBLISHED: [&str; 2] = ["examples/python/hosted_bot.py", "docs/bots.md"];

#[test]
fn published_examples_declare_the_current_protocol_version() {
    let expected = penta::protocol::PROTOCOL_VERSION;
    for relative in PUBLISHED {
        let source = read(relative);
        let declared = declared_versions(&source);
        assert!(
            !declared.is_empty(),
            "{relative} publishes a bot but declares no protocolVersion; \
             a bot that declares nothing is treated as the legacy epoch and refused",
        );
        for version in declared {
            assert_eq!(
                version, expected,
                "{relative} declares protocolVersion {version}, but this engine \
                 speaks {expected}. A bot copied from here would be refused at \
                 registration. Bump the example along with the epoch.",
            );
        }
    }
}

/// The prose that introduces each snippet names the epoch too, and a reader
/// who trusts the sentence over the code is entitled to the same answer.
#[test]
fn published_prose_names_the_current_protocol_version() {
    let expected = penta::protocol::PROTOCOL_VERSION;
    let phrase = format!("protocol-{expected} indexed-action vocabulary");
    for relative in PUBLISHED {
        let source = read(relative);
        assert!(
            source.contains(&phrase),
            "{relative} should describe its bot as consuming the \
             {phrase:?}, so the comment and the declaration cannot drift apart",
        );
    }
}
