//! The cards the Premodern BW Control list needed.

use super::*;

/// Defense Grid taxes the instant held up, not the sorcery cast on time.
#[test]
fn defense_grid_taxes_only_the_nonactive_player() {
    let mut game = ready_game();
    game.battlefield
        .push(creature(10_000, cards::DEFENSE_GRID, PlayerId::One));

    // The active player's own spell is untaxed.
    let bolt = card(10_001, cards::LIGHTNING_BOLT, PlayerId::One);
    let bolt_id = bolt.id;
    game.players[PlayerId::One.index()].hand.push(bolt);
    game.players[PlayerId::One.index()].mana_pool.red = 1;
    game.priority = PlayerId::One;
    assert!(
        game.legal_actions(PlayerId::One)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if *card == bolt_id)),
        "on its controller's turn a Bolt still costs one",
    );

    // The other seat pays three more for the same spell.
    let theirs = card(10_002, cards::LIGHTNING_BOLT, PlayerId::Two);
    let theirs_id = theirs.id;
    game.players[PlayerId::Two.index()].hand.push(theirs);
    game.players[PlayerId::Two.index()].mana_pool.red = 1;
    game.priority = PlayerId::Two;
    assert!(
        !game
            .legal_actions(PlayerId::Two)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if *card == theirs_id)),
        "off-turn it costs three more, which one red cannot pay",
    );

    game.players[PlayerId::Two.index()].mana_pool.colorless = 3;
    assert!(
        game.legal_actions(PlayerId::Two)
            .iter()
            .any(|action| matches!(action, Action::CastSpell { card, .. } if *card == theirs_id)),
        "with the tax paid it is castable again",
    );
}

/// Skeletal Scrying spends the graveyard that fed it.
#[test]
fn skeletal_scrying_exiles_as_many_as_it_draws() {
    let mut game = ready_game();
    for index in 0..4 {
        let card = card(10_010 + index, cards::SWAMP, PlayerId::One);
        game.players[PlayerId::One.index()].graveyard.push(card);
    }
    let scrying = card(10_000, cards::SKELETAL_SCRYING, PlayerId::One);
    let scrying_id = scrying.id;
    game.players[PlayerId::One.index()].hand.push(scrying);
    let pool = &mut game.players[PlayerId::One.index()].mana_pool;
    pool.black = 1;
    pool.colorless = 2;
    game.priority = PlayerId::One;
    let hand_before = game.players[PlayerId::One.index()].hand.len();

    let cast = game
        .legal_actions(PlayerId::One)
        .into_iter()
        .find(|action| {
            matches!(action, Action::CastSpell { card, choices, .. }
                if *card == scrying_id && choices.x() == 2)
        })
        .expect("two is affordable and the graveyard has four");
    game.apply(PlayerId::One, cast).unwrap();
    drain_pending(&mut game);

    assert_eq!(
        game.players[PlayerId::One.index()].hand.len(),
        hand_before - 1 + 2,
        "two drawn, one Scrying spent",
    );
    assert_eq!(
        game.players[PlayerId::One.index()].life,
        18,
        "and two life paid",
    );
    assert_eq!(
        game.players[PlayerId::One.index()].graveyard.len(),
        3,
        "two exiled from four, and the Scrying itself arrived",
    );
}

/// Gerrard's Verdict pays for the lands it took, which cannot be known until
/// the opponent has chosen what to lose.
#[test]
fn gerrards_verdict_gains_three_for_each_land_discarded() {
    let played = |lands: usize| {
        let mut game = ready_game();
        game.players[PlayerId::Two.index()].hand.clear();
        for index in 0..lands {
            let land = card(
                10_010 + u32::try_from(index).expect("fits"),
                cards::SWAMP,
                PlayerId::Two,
            );
            game.players[PlayerId::Two.index()].hand.push(land);
        }
        for index in lands..2 {
            let spell = card(
                10_020 + u32::try_from(index).expect("fits"),
                cards::LIGHTNING_BOLT,
                PlayerId::Two,
            );
            game.players[PlayerId::Two.index()].hand.push(spell);
        }

        let verdict = card(10_000, cards::GERRARDS_VERDICT, PlayerId::One);
        let verdict_id = verdict.id;
        game.players[PlayerId::One.index()].hand.push(verdict);
        let pool = &mut game.players[PlayerId::One.index()].mana_pool;
        pool.white = 1;
        pool.black = 1;
        game.priority = PlayerId::One;
        game.apply(
            PlayerId::One,
            cast_action(
                verdict_id,
                vec![Target::Player(PlayerId::Two)],
                Vec::new(),
                0,
            ),
        )
        .expect("the Verdict is cast");
        drain_pending(&mut game);
        game.players[PlayerId::One.index()].life
    };

    assert_eq!(played(2), 26, "two lands discarded is six life");
    assert_eq!(played(0), 20, "no land discarded is none");
}

/// Cabal Therapy takes every copy of the name it guesses, and nothing else.
#[test]
fn cabal_therapy_takes_every_copy_of_the_named_card() {
    let mut game = ready_game();
    game.players[PlayerId::Two.index()].hand.clear();
    for index in 0..2 {
        let bolt = card(
            10_010 + u32::try_from(index).expect("fits"),
            cards::LIGHTNING_BOLT,
            PlayerId::Two,
        );
        game.players[PlayerId::Two.index()].hand.push(bolt);
    }
    game.players[PlayerId::Two.index()]
        .hand
        .push(card(10_020, cards::COUNTERSPELL, PlayerId::Two));

    let therapy = card(10_000, cards::CABAL_THERAPY, PlayerId::One);
    let therapy_id = therapy.id;
    game.players[PlayerId::One.index()].hand.push(therapy);
    game.players[PlayerId::One.index()].mana_pool.black = 1;
    game.priority = PlayerId::One;
    game.apply(
        PlayerId::One,
        cast_action(
            therapy_id,
            vec![Target::Player(PlayerId::Two)],
            Vec::new(),
            0,
        ),
    )
    .expect("the Therapy is cast");
    pass_until_decision(&mut game);

    let decision = game
        .observe(PlayerId::One)
        .decision
        .expect("the Therapy asks for a name");
    let bolt = decision
        .options
        .iter()
        .find(|option| option.label == "Lightning Bolt")
        .expect("a spell is nameable")
        .id;
    game.apply(
        PlayerId::One,
        Action::ChooseDecision {
            decision: decision.id,
            options: vec![bolt],
        },
    )
    .expect("naming is legal");
    drain_pending(&mut game);

    let hand = &game.players[PlayerId::Two.index()].hand;
    assert_eq!(hand.len(), 1, "both Bolts went");
    assert_eq!(
        hand[0].definition,
        cards::COUNTERSPELL,
        "and the card that was not named stayed",
    );
}

/// Haunting Echoes takes the copies still in the library, and leaves the
/// basic lands where they are.
#[test]
fn haunting_echoes_takes_every_copy_of_what_it_exiled() {
    let mut game = ready_game();
    // One Psychatog and one basic in the yard; two more Psychatogs and a
    // Swamp waiting in the library.
    game.players[PlayerId::Two.index()].graveyard.push(card(
        10_010,
        cards::PSYCHATOG,
        PlayerId::Two,
    ));
    game.players[PlayerId::Two.index()]
        .graveyard
        .push(card(10_011, cards::SWAMP, PlayerId::Two));
    game.players[PlayerId::Two.index()].library.clear();
    game.players[PlayerId::Two.index()]
        .library
        .push(card(10_012, cards::PSYCHATOG, PlayerId::Two));
    game.players[PlayerId::Two.index()]
        .library
        .push(card(10_013, cards::PSYCHATOG, PlayerId::Two));
    game.players[PlayerId::Two.index()]
        .library
        .push(card(10_014, cards::SWAMP, PlayerId::Two));

    let echoes = card(10_000, cards::HAUNTING_ECHOES, PlayerId::One);
    let echoes_id = echoes.id;
    game.players[PlayerId::One.index()].hand.push(echoes);
    let pool = &mut game.players[PlayerId::One.index()].mana_pool;
    pool.black = 2;
    pool.colorless = 3;
    game.priority = PlayerId::One;

    game.apply(
        PlayerId::One,
        cast_action(
            echoes_id,
            vec![Target::Player(PlayerId::Two)],
            Vec::new(),
            0,
        ),
    )
    .expect("five mana casts it at the opponent");
    drain_pending(&mut game);

    let opponent = &game.players[PlayerId::Two.index()];
    assert_eq!(
        opponent.graveyard.len(),
        1,
        "the basic land stays; the Psychatog goes",
    );
    assert_eq!(
        opponent.library.len(),
        1,
        "both library Psychatogs follow it, and the Swamp does not",
    );
    assert_eq!(
        opponent.exile.len(),
        3,
        "one from the graveyard and its two copies",
    );
}
