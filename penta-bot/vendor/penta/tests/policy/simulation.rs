use super::*;

#[test]
#[ignore = "slow simulation sweep"]
fn handcrafted_policy_decisively_beats_random_across_builtin_decks_and_seats() {
    let catalog = poc::catalog().unwrap();
    let decks = [
        poc::goblins(),
        poc::sligh(),
        poc::artifacts(),
        poc::robots(),
        poc::the_deck(),
    ];
    let mut wins = 0;
    let mut decided_games = 0;

    for deck in decks {
        for seed in 0..10 {
            for handcrafted_seat in [PlayerId::One, PlayerId::Two] {
                let mut game =
                    Game::new(catalog.clone(), [deck.clone(), deck.clone()], seed).unwrap();
                let mut handcrafted = HandcraftedPolicy::new(catalog.clone());
                let mut random = RandomPolicy::new(seed ^ 0xa11c_e5ed);
                let result = match handcrafted_seat {
                    PlayerId::One => {
                        play_game(&mut game, &mut handcrafted, &mut random, ACTION_LIMIT)
                    }
                    PlayerId::Two => {
                        play_game(&mut game, &mut random, &mut handcrafted, ACTION_LIMIT)
                    }
                }
                .unwrap();

                if let GameResult::Winner { winner, .. } = result {
                    decided_games += 1;
                    wins += usize::from(winner == handcrafted_seat);
                }
            }
        }
    }

    assert_eq!(decided_games, 100);
    assert!(
        wins >= 90,
        "handcrafted policy won only {wins} of {decided_games} games"
    );
}
