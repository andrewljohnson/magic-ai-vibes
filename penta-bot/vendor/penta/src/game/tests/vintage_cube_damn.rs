//! Damn: one card that is either removal or a Wrath, and regenerates from
//! neither.

use super::*;

fn cast_damn(game: &mut Game, damn: CardInstanceId, overloaded: bool) {
    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| match action {
            Action::CastSpell { card, choices, .. } => {
                *card == damn && choices.costs().alternative().is_some() == overloaded
            }
            _ => false,
        })
        .unwrap_or_else(|| panic!("a cast with overloaded={overloaded} is offered"));
    game.apply(PlayerId::One, cast).expect("it is cast");
    pass_priority_pair(game);
    drain_pending(game);
}

fn staged(overloaded: bool) -> (Game, CardInstanceId) {
    let mut game = ready_game();
    game.battlefield.clear();
    let damn = card(95_500, cards::DAMN, PlayerId::One);
    let damn_id = damn.id;
    game.players[0].hand.push(damn);
    if overloaded {
        game.add_unrestricted_mana(PlayerId::One, ManaColor::White, 2);
        game.add_unrestricted_mana(PlayerId::One, ManaColor::Colorless, 2);
    } else {
        game.add_unrestricted_mana(PlayerId::One, ManaColor::Black, 2);
    }
    (game, damn_id)
}

/// Cast for its printed cost it answers one creature and leaves the rest of
/// the board alone -- including the caster's own.
#[test]
fn damn_kills_one_creature_for_two_black() {
    let (mut game, damn_id) = staged(false);
    let theirs = creature(95_501, cards::SERRA_ANGEL, PlayerId::Two);
    let theirs_id = theirs.card.id;
    game.battlefield.push(theirs);
    let mine = creature(95_502, cards::GRIZZLY_BEARS, PlayerId::One);
    let mine_id = mine.card.id;
    game.battlefield.push(mine);

    cast_damn(&mut game, damn_id, false);

    assert!(
        game.battlefield
            .iter()
            .all(|permanent| permanent.card.id != theirs_id),
        "the target is destroyed",
    );
    assert!(
        game.battlefield
            .iter()
            .any(|permanent| permanent.card.id == mine_id),
        "and nothing else is touched",
    );
}

/// Overloaded it is a Wrath, and a Wrath takes the caster's board too.
#[test]
fn overloaded_damn_destroys_every_creature_including_yours() {
    let (mut game, damn_id) = staged(true);
    for id in 95_510..95_513 {
        game.battlefield
            .push(creature(id, cards::GRIZZLY_BEARS, PlayerId::Two));
    }
    game.battlefield
        .push(creature(95_520, cards::SERRA_ANGEL, PlayerId::One));

    cast_damn(&mut game, damn_id, true);

    assert!(
        game.battlefield.is_empty(),
        "each creature means each, not each one you don't control",
    );
}

/// "Can't be regenerated" on both halves, which is the whole reason to play
/// it over the sorceries it is otherwise a copy of.
#[test]
fn neither_half_can_be_regenerated_through() {
    for overloaded in [false, true] {
        let (mut game, damn_id) = staged(overloaded);
        let mut troll = creature(95_530, cards::SEDGE_TROLL, PlayerId::Two);
        troll.regeneration_shields = 1;
        let troll_id = troll.card.id;
        game.battlefield.push(troll);

        cast_damn(&mut game, damn_id, overloaded);

        assert!(
            game.battlefield
                .iter()
                .all(|permanent| permanent.card.id != troll_id),
            "the shield does not save it, overloaded={overloaded}",
        );
        assert!(
            game.players[1]
                .graveyard
                .iter()
                .any(|card| card.definition == cards::SEDGE_TROLL),
        );
    }
}
