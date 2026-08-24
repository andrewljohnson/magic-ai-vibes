use super::*;

#[test]
fn random_policy_is_seeded_and_avoids_conceding() {
    let catalog = poc::catalog().unwrap();
    let game = Game::new(catalog, [poc::goblins(), poc::goblins()], 17).unwrap();
    let observation = game.observe(PlayerId::One);
    let mut first = RandomPolicy::new(99);
    let mut second = RandomPolicy::new(99);

    for _ in 0..20 {
        let first_action = first.choose_action(&observation);
        let second_action = second.choose_action(&observation);
        assert_eq!(first_action, second_action);
        assert!(!matches!(first_action, Some(Action::Concede)));
    }
}
