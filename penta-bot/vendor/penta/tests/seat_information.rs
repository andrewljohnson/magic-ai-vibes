//! A seat gets what `Game::events_for*` decides it gets. The raw log is a
//! different thing: it opens with the shuffle seed, and decklists are public,
//! so a seat holding it can rebuild both libraries -- the opponent's hand and
//! every draw either player will make.
//!
//! The browser client is also the engine today, so nothing stops it reading
//! the raw log by hand. That is exactly the habit that stops being safe the
//! moment the engine moves server-side, and a reviewer cannot see it in a
//! diff. So it is a build failure instead.

use std::fs;
use std::path::PathBuf;

/// Everything under here stands in for a seat: it holds a view, not the
/// engine. Scanning the tree rather than naming one file matters -- `WebGame`'s
/// body is spread over several modules, and a guard pointed at `lib.rs` alone
/// would read a facade and check nothing.
const SEAT_TREE: &str = "wasm/src";

/// The two that legitimately hold the engine, and say so in their own docs.
const ENGINE_OWNERS: [&str; 2] = ["session.rs", "hosted.rs"];

fn seat_side_files(root: &std::path::Path) -> Vec<PathBuf> {
    let mut found = Vec::new();
    collect_rust_files(&root.join(SEAT_TREE), &mut found);
    found.retain(|path| {
        let name = path
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("");
        !ENGINE_OWNERS.contains(&name) && !path.components().any(|part| part.as_os_str() == "tests")
    });
    assert!(
        found.len() > 2,
        "expected several seat-side modules under {SEAT_TREE}, found {}",
        found.len()
    );
    found
}

fn collect_rust_files(directory: &std::path::Path, into: &mut Vec<PathBuf>) {
    let Ok(entries) = fs::read_dir(directory) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            collect_rust_files(&path, into);
        } else if path.extension().is_some_and(|extension| extension == "rs") {
            into.push(path);
        }
    }
}

/// Everything the seat-facing client does to the engine goes through the
/// session, so the day a remote one appears there is a list of methods to
/// implement rather than a hunt through 3,000 lines. Test code may still
/// reach the engine to build a position; that is what the `#[cfg(test)]`
/// accessors are for, and this only reads the code above the test module.
#[test]
fn a_seat_holds_a_session_rather_than_the_engine() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let mut offenders = Vec::new();
    for path in seat_side_files(&root) {
        let source = fs::read_to_string(&path).expect("readable");
        let seat_side = source
            .split_once("mod tests {")
            .map_or(source.as_str(), |(before, _)| before);
        let relative = path.strip_prefix(&root).unwrap_or(&path).display();
        offenders.extend(
            seat_side
                .lines()
                .enumerate()
                .filter(|(_, line)| line.contains("self.game"))
                .map(|(number, line)| format!("{relative}:{}: {}", number + 1, line.trim())),
        );
    }
    assert!(
        offenders.is_empty(),
        "these reach past the session to the engine. Add the method to \
         LocalSession instead, so a remote session has to answer for it \
         too:\n  {}",
        offenders.join("\n  ")
    );
}

#[test]
fn a_seat_never_reads_the_raw_event_log() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let mut offenders = Vec::new();
    for path in seat_side_files(&root) {
        let source = fs::read_to_string(&path).expect("a seat file is readable");
        let relative = path.strip_prefix(&root).unwrap_or(&path).display();
        for (number, line) in source.lines().enumerate() {
            if line.contains(".events()") {
                offenders.push(format!("{relative}:{}: {}", number + 1, line.trim()));
            }
        }
    }
    assert!(
        offenders.is_empty(),
        "these read the unprojected log. Use `events_for` for the whole \
         stream, or `event_cursor` and `events_for_since` for what one action \
         caused:\n  {}",
        offenders.join("\n  ")
    );
}
