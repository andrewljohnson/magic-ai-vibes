use super::*;

#[test]
fn handcrafted_never_aims_removal_at_its_own_permanents() {
    let catalog = poc::catalog().unwrap();
    let own_angel = permanent(1, poc::cards::SERRA_ANGEL, PlayerId::One, Some(4), Some(4));
    let own_mox = permanent(2, poc::cards::MOX_PEARL, PlayerId::One, None, None);
    let swords = CardInstanceId(10);
    let disenchant = CardInstanceId(11);
    let mut observation = policy_observation(
        vec![own_angel, own_mox],
        vec![
            Action::PassPriority,
            Action::CastSpell {
                card: swords,
                choices: CastChoices::default()
                    .with_x(0)
                    .with_targets(vec![TargetSelection::new(
                        TargetSlotId(0),
                        vec![Target::Permanent(CardInstanceId(1))],
                    )]),
                sacrifices: Vec::new(),
            },
            Action::CastSpell {
                card: disenchant,
                choices: CastChoices::default()
                    .with_x(0)
                    .with_targets(vec![TargetSelection::new(
                        TargetSlotId(0),
                        vec![Target::Permanent(CardInstanceId(2))],
                    )]),
                sacrifices: Vec::new(),
            },
        ],
    );
    observation.hand = vec![
        (swords, poc::cards::SWORDS_TO_PLOWSHARES),
        (disenchant, poc::cards::DISENCHANT),
    ];
    let mut policy = HandcraftedPolicy::new(catalog);

    // Removal carries a large base score, so a merely-unattractive friendly
    // target used to stay far above passing.
    assert_eq!(
        policy.choose_action(&observation),
        Some(Action::PassPriority),
        "the only targets on offer are its own board, so it should hold the removal",
    );
}

#[test]
fn handcrafted_still_spends_removal_on_the_opponent() {
    let catalog = poc::catalog().unwrap();
    let own_angel = permanent(1, poc::cards::SERRA_ANGEL, PlayerId::One, Some(4), Some(4));
    let their_angel = permanent(3, poc::cards::SERRA_ANGEL, PlayerId::Two, Some(4), Some(4));
    let swords = CardInstanceId(10);
    let cast_at_theirs = Action::CastSpell {
        card: swords,
        choices: CastChoices::default()
            .with_x(0)
            .with_targets(vec![TargetSelection::new(
                TargetSlotId(0),
                vec![Target::Permanent(CardInstanceId(3))],
            )]),
        sacrifices: Vec::new(),
    };
    let mut observation = policy_observation(
        vec![own_angel, their_angel],
        vec![
            Action::PassPriority,
            Action::CastSpell {
                card: swords,
                choices: CastChoices::default()
                    .with_x(0)
                    .with_targets(vec![TargetSelection::new(
                        TargetSlotId(0),
                        vec![Target::Permanent(CardInstanceId(1))],
                    )]),
                sacrifices: Vec::new(),
            },
            cast_at_theirs.clone(),
        ],
    );
    observation.hand = vec![(swords, poc::cards::SWORDS_TO_PLOWSHARES)];
    let mut policy = HandcraftedPolicy::new(catalog);

    assert_eq!(policy.choose_action(&observation), Some(cast_at_theirs));
}

#[test]
fn handcrafted_scavenging_ooze_exiles_an_opponents_graveyard_card() {
    let catalog = card::catalog().unwrap();
    let ooze = CardInstanceId(1);
    let food = CardInstanceId(2);
    let eat = Action::ActivateAbility {
        source: ooze,
        ability: printed_ability(cards::SCAVENGING_OOZE, 0),
        targets: activated_targets(Target::Card(food)),
        cost_objects: Vec::new(),
        x: 0,
        modes: Vec::new(),
    };
    let mut observation = policy_observation(
        vec![permanent(
            ooze.0,
            cards::SCAVENGING_OOZE,
            PlayerId::One,
            Some(2),
            Some(2),
        )],
        vec![Action::PassPriority, eat.clone()],
    );
    observation.mana_pools[PlayerId::One.index()].green = 1;
    observation.graveyards[PlayerId::Two.index()].push((food, cards::SAVANNAH_LIONS));
    let mut policy = HandcraftedPolicy::new(catalog);

    assert_eq!(policy.choose_action(&observation), Some(eat));
}

#[test]
fn handcrafted_animates_a_factory_once_rather_than_every_priority() {
    let catalog = poc::catalog().unwrap();
    // The animation is the Factory's second clause; the first taps for mana.
    // Scoring reads the real ability now, so a stand-in origin would find the
    // mana ability instead.
    let animate = Action::ActivateAbility {
        source: CardInstanceId(1),
        ability: AbilityOrigin::Printed {
            definition: poc::cards::MISHRA_S_FACTORY,
            part: CardPartId::PRIMARY,
            ability: AbilityId(1),
        },
        targets: Vec::new(),
        cost_objects: Vec::new(),
        x: 0,
        modes: Vec::new(),
    };

    let dormant = permanent(1, poc::cards::MISHRA_S_FACTORY, PlayerId::One, None, None);
    let mut observation =
        policy_observation(vec![dormant], vec![Action::PassPriority, animate.clone()]);
    observation.step = Step::BeginningOfCombat;
    let mut policy = HandcraftedPolicy::new(catalog.clone());
    assert_eq!(
        policy.choose_action(&observation),
        Some(animate.clone()),
        "a dormant Factory is still worth animating",
    );

    // Same board, except the Factory is already a 2/2.
    let awake = permanent(
        1,
        poc::cards::MISHRA_S_FACTORY,
        PlayerId::One,
        Some(2),
        Some(2),
    );
    let mut observation = policy_observation(vec![awake], vec![Action::PassPriority, animate]);
    observation.step = Step::BeginningOfCombat;
    let mut policy = HandcraftedPolicy::new(catalog);
    assert_eq!(
        policy.choose_action(&observation),
        Some(Action::PassPriority),
        "animating a Factory that is already a creature only burns mana",
    );
}
