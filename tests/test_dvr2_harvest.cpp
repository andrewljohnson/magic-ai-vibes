#include "old_school/dvr2_harvest.hpp"
#include "old_school/dvr1_replay.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace dvr2 = old_school::dvr2_harvest;

namespace {

class TestRunner {
  public:
    template <typename Function>
    void run(std::string_view name, Function function) {
        try {
            function();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, " << failed_
                  << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(Function function,
                     std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        std::string pattern =
            (std::filesystem::temp_directory_path() /
             "old-school-dvr2-XXXXXX")
                .string();
        std::vector<char> buffer(
            pattern.begin(), pattern.end());
        buffer.push_back('\0');
        char* created = ::mkdtemp(buffer.data());
        if (created == nullptr) {
            throw std::system_error(
                errno, std::generic_category(),
                "cannot create DVR2 test directory");
        }
        path_ = created;
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void test_exact_source_schedule() {
    const auto schedule = dvr2::source_schedule();
    const auto repeated = dvr2::source_schedule();
    expect(
        schedule == repeated &&
            schedule.size() ==
                dvr2::kExpectedPhysicalGames,
        "DVR2 schedule is not exactly 80 games");
    std::array<std::size_t, 2> seed_counts{};
    std::set<std::uint64_t> game_seeds;
    std::array<
        std::array<
            std::array<
                std::array<std::array<std::size_t, 2>, 2>,
                old_school::kDeckCount>,
            old_school::kDeckCount>,
        dvr2::kSourceSeedBases.size()>
        cells{};
    for (const auto& game : schedule) {
        expect(
            game.source.seed_base_index <
                dvr2::kSourceSeedBases.size() &&
                game.source.seed_base ==
                    dvr2::kSourceSeedBases[
                        game.source.seed_base_index],
            "DVR2 schedule seed base changed");
        ++seed_counts[game.source.seed_base_index];
        game_seeds.insert(game.source.game_seed);
        for (std::size_t owner = 0; owner < 2; ++owner) {
            const std::size_t owner_deck =
                static_cast<std::size_t>(
                    game.seat_decks[owner]);
            const std::size_t opponent_deck =
                static_cast<std::size_t>(
                    game.seat_decks[1 - owner]);
            const std::size_t play =
                owner == game.source.starting_player ? 0 : 1;
            ++cells[game.source.seed_base_index]
                   [owner_deck][opponent_deck]
                   [owner][play];
        }
    }
    expect(
        seed_counts[0] == 40 && seed_counts[1] == 40 &&
            game_seeds.size() ==
                dvr2::kExpectedPhysicalGames,
        "DVR2 seed-base counts or physical game seeds changed");
    for (std::size_t seed_index = 0;
         seed_index < dvr2::kSourceSeedBases.size();
         ++seed_index) {
        for (std::size_t owner = 0;
             owner < old_school::kDeckCount; ++owner) {
            for (std::size_t opponent = 0;
                 opponent < old_school::kDeckCount;
                 ++opponent) {
                for (std::size_t seat = 0;
                     seat < 2; ++seat) {
                    for (std::size_t play = 0;
                         play < 2; ++play) {
                        expect(
                            cells[seed_index][owner]
                                 [opponent][seat][play] ==
                                (owner == opponent
                                     ? 0U
                                     : 1U),
                            "DVR2 per-base ordered matchup/"
                            "seat/play balance changed");
                    }
                }
            }
        }
    }
    const std::string observed_hash =
        dvr2::source_schedule_sha256();
    if (observed_hash !=
        dvr2::kExpectedSourceScheduleSha256) {
        throw std::runtime_error(
            "DVR2 source schedule hash changed: " +
            observed_hash);
    }
}

std::vector<dvr2::SelectionCandidate>
selection_fixture() {
    std::vector<dvr2::SelectionCandidate> result;
    const auto schedule = dvr2::source_schedule();
    const auto add =
        [&](const dvr2::ScheduledGame& game,
            std::size_t owner, bool own_top,
            std::size_t ordinal, std::size_t actions,
            std::string fingerprint) {
            dvr2::SourceLocator source = game.source;
            source.owner_seat = owner;
            source.owner_on_play =
                owner == source.starting_player;
            source.trace_ordinal = ordinal;
            result.push_back({
                .source_index = result.size(),
                .source = source,
                .owner_deck = game.seat_decks[owner],
                .opponent_deck =
                    game.seat_decks[1 - owner],
                .top_controlled_by_owner = own_top,
                .information_action_fingerprint =
                    std::move(fingerprint),
                .action_count = actions,
            });
        };

    for (std::size_t game_index = 0;
         game_index < schedule.size(); ++game_index) {
        for (std::size_t owner = 0; owner < 2; ++owner) {
            const std::string base =
                "g" + std::to_string(game_index) +
                ".p" + std::to_string(owner);
            add(schedule[game_index], owner, false, 0, 2,
                base + ".opponent");
            add(schedule[game_index], owner, true, 1, 2,
                base + ".own");
        }
    }

    // Frozen precedence witnesses on one already-full owner-game:
    // action cap is classified before dedup/owner cap, then a duplicate is
    // classified before owner cap, then a fresh third root hits owner cap.
    const auto& game = schedule.front();
    add(game, 0, false, 2, 33, "cap");
    add(
        game, 0, false, 3, 2,
        result.front().information_action_fingerprint);
    add(game, 0, false, 4, 2, "third-owner-root");
    return result;
}

void test_selection_quotas_and_cross_sums() {
    const auto candidates = selection_fixture();
    const auto report = dvr2::select_roots(candidates);
    expect(
        report.selected_indices.size() ==
            dvr2::kMaximumPrimaryRoots,
        "DVR2 did not select exactly 64 quota roots");
    expect(
        report.quotas_met && report.cross_sums_valid,
        "DVR2 quota or manifest cross-sum failed");

    std::array<std::size_t, old_school::kDeckCount>
        blue_opponent{};
    for (const std::size_t index :
         report.selected_indices) {
        const auto& candidate = candidates[index];
        if (candidate.owner_deck ==
                old_school::DeckId::Blue &&
            !candidate.top_controlled_by_owner) {
            ++blue_opponent[static_cast<std::size_t>(
                candidate.opponent_deck)];
        }
    }
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            blue_opponent[deck] ==
                (deck ==
                         static_cast<std::size_t>(
                             old_school::DeckId::Blue)
                     ? 0U
                     : 8U),
            "Blue opponent-top subquota changed");
    }

    const auto retained_for =
        [&](old_school::DeckId owner,
            bool own_top,
            std::optional<old_school::DeckId> opponent) {
            std::vector<const dvr2::SelectionCandidate*>
                retained;
            for (const std::size_t index :
                 report.selected_indices) {
                const auto& candidate =
                    candidates[index];
                if (candidate.owner_deck == owner &&
                    candidate.top_controlled_by_owner ==
                        own_top &&
                    (!opponent.has_value() ||
                     candidate.opponent_deck ==
                         *opponent)) {
                    retained.push_back(&candidate);
                }
            }
            return retained;
        };
    const auto expect_balanced_marginals =
        [](const std::vector<
               const dvr2::SelectionCandidate*>& retained,
           std::size_t expected_each,
           std::string_view message) {
            std::array<std::size_t, 2> seeds{};
            std::array<std::size_t, 2> seats{};
            std::array<std::size_t, 2> play_draw{};
            for (const auto* candidate : retained) {
                ++seeds[candidate->source.seed_base_index];
                ++seats[candidate->source.owner_seat];
                ++play_draw[
                    candidate->source.owner_on_play
                        ? 0
                        : 1];
            }
            expect(
                seeds[0] == expected_each &&
                    seeds[1] == expected_each &&
                    seats[0] == expected_each &&
                    seats[1] == expected_each &&
                    play_draw[0] == expected_each &&
                    play_draw[1] == expected_each,
                message);
        };
    for (std::size_t owner_index = 0;
         owner_index < old_school::kDeckCount;
         ++owner_index) {
        const auto owner =
            static_cast<old_school::DeckId>(
                owner_index);
        if (owner == old_school::DeckId::Blue) {
            for (std::size_t opponent_index = 0;
                 opponent_index <
                     old_school::kDeckCount;
                 ++opponent_index) {
                const auto opponent =
                    static_cast<old_school::DeckId>(
                        opponent_index);
                if (opponent == owner) {
                    continue;
                }
                const auto opponent_top =
                    retained_for(
                        owner, false, opponent);
                const auto own_top =
                    retained_for(
                        owner, true, opponent);
                expect(
                    opponent_top.size() == 8 &&
                        own_top.size() == 2,
                    "DVR2 Blue per-opponent quota changed");
                expect_balanced_marginals(
                    opponent_top, 4,
                    "DVR2 Blue opponent-top quota lost "
                    "seed/seat/play balance");
                expect_balanced_marginals(
                    own_top, 1,
                    "DVR2 Blue own-top quota lost "
                    "seed/seat/play balance");
            }
            continue;
        }
        for (const bool own_top : {false, true}) {
            const auto retained =
                retained_for(
                    owner, own_top, std::nullopt);
            const std::size_t expected_size =
                own_top ? 2 : 4;
            expect(
                retained.size() == expected_size,
                "DVR2 non-Blue owner quota changed");
            std::set<old_school::DeckId> opponents;
            for (const auto* candidate : retained) {
                opponents.insert(
                    candidate->opponent_deck);
            }
            expect(
                opponents.size() == expected_size,
                "DVR2 non-Blue round robin did not "
                "cycle opponent first");
            expect_balanced_marginals(
                retained, expected_size / 2,
                "DVR2 non-Blue quota lost "
                "seed/seat/play balance");
        }
    }

    auto permuted = candidates;
    std::reverse(permuted.begin(), permuted.end());
    const auto permuted_report =
        dvr2::select_roots(permuted);
    const auto selected_fingerprints =
        [](const auto& source,
           const dvr2::SelectionReport& selection) {
            std::vector<std::string> fingerprints;
            for (const std::size_t index :
                 selection.selected_indices) {
                fingerprints.push_back(
                    source[index]
                        .information_action_fingerprint);
            }
            return fingerprints;
        };
    expect(
        report.coverage == permuted_report.coverage &&
            selected_fingerprints(candidates, report) ==
                selected_fingerprints(
                    permuted, permuted_report),
        "DVR2 quota round robin changed under input "
        "permutation");

    std::size_t cap = 0;
    std::size_t duplicate = 0;
    std::size_t per_owner = 0;
    std::size_t quota = 0;
    for (const auto& stratum : report.coverage) {
        for (const auto& cell : stratum) {
            expect(
                cell.cross_sum_valid(),
                "DVR2 cell does not cross-sum");
            cap += cell.action_cap_skipped;
            duplicate += cell.duplicate_skipped;
            per_owner += cell.per_owner_game_skipped;
            quota += cell.quota_skipped;
        }
    }
    expect(
        cap == 1 && duplicate == 1 && per_owner == 1 &&
            quota != 0,
        "DVR2 selector precedence witnesses changed");
}

void test_selection_rejects_impossible_matchup() {
    auto candidates = selection_fixture();
    candidates.front().opponent_deck =
        candidates.front().owner_deck;
    expect_rejected(
        [&] {
            static_cast<void>(
                dvr2::select_roots(candidates));
        },
        "DVR2 accepted an impossible same-deck source matchup");
}

old_school::probes::BsrRootScore score_fixture() {
    old_school::probes::BsrRootScore score;
    score.information_action_fingerprint =
        "score-fingerprint";
    score.action_count = 2;
    score.actual_action_index = 0;
    score.actual_action_descriptor = "pass";
    score.reference_model_fingerprint =
        std::string(dvr2::kExpectedModelFingerprint);
    score.reference_seed_base =
        old_school::probes::kBsrReferenceSeed;
    score.scout_worlds =
        old_school::probes::kBsrScoutWorlds;
    score.confirmation_worlds =
        old_school::probes::kBsrConfirmationWorlds;
    score.horizon_turns =
        old_school::probes::kBsrReferenceHorizon;
    score.rollouts_per_world = 1;
    score.evaluation_threads =
        old_school::probes::kBsrReferenceEvaluationThreads;
    score.scout_seed = 1;
    score.confirmation_seed = 2;
    score.rollout_evaluations = 512;
    score.terminal_evaluations = 100;
    score.bootstrapped_evaluations = 412;
    score.action_means = {
        {
            .descriptor = "pass",
            .scout_mean = 0.5,
            .confirmation_mean = 0.5,
        },
        {
            .descriptor = "other",
            .scout_mean = 0.5,
            .confirmation_mean = 0.5,
        },
    };
    score.scout_confirmation_best_set_stable = true;
    score.actual_outside_best_sets = false;
    score.descriptor_order_invariant = true;
    score.hidden_repartition_eligible = true;
    score.hidden_repartition_bit_identical = true;
    score.accounting_passed = true;
    return score;
}

void test_reference_classification_and_budget() {
    auto score = score_fixture();
    expect(
        dvr2::classify_reference(score) ==
            dvr2::ReferenceClassification::StableAgreement,
        "DVR2 stable agreement classification changed");
    score.actual_outside_best_sets = true;
    expect(
        dvr2::classify_reference(score) ==
            dvr2::ReferenceClassification::
                StableDisagreement,
        "DVR2 stable disagreement classification changed");
    score.scout_confirmation_best_set_stable = false;
    expect(
        dvr2::classify_reference(score) ==
            dvr2::ReferenceClassification::UnstableBestSet,
        "DVR2 unstable classification changed");
    score.scout_confirmation_best_set_stable = true;
    score.evaluation_threads = 1;
    expect(
        dvr2::classify_reference(score) ==
            dvr2::ReferenceClassification::InvalidInvariance,
        "DVR2 primary classification accepted one thread");

    expect(
        dvr2::reference_score_cost(2) == 512 &&
            dvr2::reference_score_cost(32) == 8192,
        "DVR2 exact score cost changed");
    expect(
        dvr2::can_reserve_worst_case_reference_path(
            0, 32, false, 24576),
        "DVR2 rejected an exactly fitting three-score path");
    expect(
        !dvr2::can_reserve_worst_case_reference_path(
            1, 32, false, 24576),
        "DVR2 accepted a one-evaluation cap breach");
    expect(
        dvr2::worst_case_reference_score_calls(false) == 3 &&
            dvr2::worst_case_reference_score_calls(true) == 2 &&
            dvr2::can_reserve_worst_case_reference_path(
                0, 32, true, 16384) &&
            !dvr2::can_reserve_worst_case_reference_path(
                0, 32, false, 16384),
        "DVR2 did not release the third-score reservation "
        "after retaining an owner-deck control");
    expect_rejected(
        [] {
            static_cast<void>(
                dvr2::reference_score_cost(1));
        },
        "DVR2 accepted an under-floor score cost");
}

void test_reference_budget_actual_call_transitions() {
    const auto score = score_fixture();
    dvr2::ReferenceBudgetLedger budget{
        .evaluation_cap = 1536,
    };
    expect(
        budget.can_reserve(2, 3),
        "DVR2 ledger rejected an exact three-call reservation");
    budget.charge(score);
    expect(
        budget.score_calls == 1 &&
            budget.evaluations == 512 &&
            budget.terminal_evaluations == 100 &&
            budget.bootstrapped_evaluations == 412 &&
            budget.accounting_valid(),
        "DVR2 primary call transition was misaccounted");
    budget.charge(score);
    expect(
        budget.score_calls == 2 &&
            budget.evaluations == 1024 &&
            budget.terminal_evaluations == 200 &&
            budget.bootstrapped_evaluations == 824 &&
            !budget.can_reserve(2, 2) &&
            budget.can_reserve(2, 1),
        "DVR2 replay/control call transition was misaccounted");
    budget.charge(score);
    expect(
        budget.score_calls == 3 &&
            budget.evaluations == 1536 &&
            budget.accounting_valid() &&
            !budget.can_reserve(2, 1),
        "DVR2 exact-cap transition was misaccounted");
    expect_rejected(
        [&] { budget.charge(score); },
        "DVR2 ledger charged a fourth call beyond its cap");

    auto malformed = score;
    --malformed.rollout_evaluations;
    dvr2::ReferenceBudgetLedger fresh;
    expect_rejected(
        [&] { fresh.charge(malformed); },
        "DVR2 ledger accepted a non-actual score cost");
}

std::shared_ptr<const old_school::LearnedModel>
tiny_reference_model() {
    static const auto model =
        old_school::train_learned_value_champion(
            1, 0xD7A2C16ULL);
    return model;
}

old_school::probes::DecisionProbe dvr1_probe_fixture() {
    auto probe =
        old_school::probes::
            make_force_spike_policy_controls_v1()
                .front();
    for (auto& candidate : probe.candidates) {
        const auto* action =
            std::get_if<old_school::PriorityAction>(
                &candidate.action);
        if (action == nullptr) {
            throw std::runtime_error(
                "DVR2 replay fixture is not a Priority root");
        }
        candidate.descriptor =
            old_school::probes::
                stable_priority_action_descriptor(*action);
    }
    return probe;
}

old_school::probes::BsrRootScore
dvr1_disagreement_evidence(
    const old_school::probes::DecisionProbe& probe,
    std::string_view production_descriptor) {
    const old_school::probes::BsrReferenceConfig config{
        .seed = old_school::probes::kBsrReferenceSeed,
        .scout_worlds =
            old_school::probes::kBsrScoutWorlds,
        .confirmation_worlds =
            old_school::probes::kBsrConfirmationWorlds,
        .horizon_turns =
            old_school::probes::kBsrReferenceHorizon,
        .rollouts_per_world = 1,
        .evaluation_threads =
            old_school::probes::
                kBsrReferenceEvaluationThreads,
    };
    auto score =
        old_school::probes::score_bsr_priority_probe(
            probe, production_descriptor,
            tiny_reference_model(), config);
    std::vector<std::string> descriptors;
    for (const auto& candidate : probe.candidates) {
        descriptors.push_back(candidate.descriptor);
    }
    std::sort(descriptors.begin(), descriptors.end());
    const auto best = std::find_if(
        descriptors.begin(), descriptors.end(),
        [&](const std::string& descriptor) {
            return descriptor != production_descriptor;
        });
    if (best == descriptors.end()) {
        throw std::runtime_error(
            "DVR2 replay fixture has no alternative action");
    }
    const auto actual = std::lower_bound(
        descriptors.begin(), descriptors.end(),
        production_descriptor);
    if (actual == descriptors.end() ||
        *actual != production_descriptor) {
        throw std::runtime_error(
            "DVR2 replay fixture lost production action");
    }
    score.action_count = descriptors.size();
    score.actual_action_index =
        static_cast<std::size_t>(
            std::distance(descriptors.begin(), actual));
    score.actual_action_descriptor =
        std::string(production_descriptor);
    score.scout_best_actions = {*best};
    score.confirmation_best_actions = {*best};
    score.action_means.clear();
    for (const std::string& descriptor : descriptors) {
        const bool selected = descriptor == *best;
        score.action_means.push_back({
            .descriptor = descriptor,
            .scout_mean = selected ? 0.8 : 0.2,
            .confirmation_mean =
                selected ? 0.8 : 0.2,
        });
    }
    score.scout_actual_mean = 0.2;
    score.scout_best_mean = 0.8;
    score.confirmation_actual_mean = 0.2;
    score.confirmation_best_mean = 0.8;
    score.confirmation_regret = 0.6;
    score.paired_standard_error = 0.05;
    score.paired_lower_95 = 0.502;
    score.scout_confirmation_best_set_stable = true;
    score.actual_outside_best_sets = true;
    return score;
}

old_school::probes::BsrRootKeyContext
dvr1_provenance_fixture(
    const old_school::probes::DecisionProbe& probe) {
    return {
        .game_seed = 0xD7A2E71DULL,
        .block = 0,
        .schedule_index = 7,
        .tracked_seat = probe.root_player,
        .tracked_starts =
            probe.state.starting_player ==
            probe.root_player,
        .trace_ordinal = 13,
    };
}

old_school::probes::BsrRootScore agreement_evidence(
    old_school::probes::BsrRootScore score,
    std::string_view production_descriptor) {
    for (auto& mean : score.action_means) {
        const bool selected =
            mean.descriptor == production_descriptor;
        mean.scout_mean = selected ? 0.8 : 0.2;
        mean.confirmation_mean =
            selected ? 0.8 : 0.2;
    }
    score.scout_best_actions = {
        std::string(production_descriptor)};
    score.confirmation_best_actions = {
        std::string(production_descriptor)};
    score.scout_actual_mean = 0.8;
    score.scout_best_mean = 0.8;
    score.confirmation_actual_mean = 0.8;
    score.confirmation_best_mean = 0.8;
    score.confirmation_regret = 0.0;
    score.paired_standard_error = 0.0;
    score.paired_lower_95 = 0.0;
    score.actual_outside_best_sets = false;
    return score;
}

void repartition_opponent_hidden(
    old_school::probes::DecisionProbe& probe) {
    auto& opponent =
        probe.state.players[1 - probe.root_player];
    const std::size_t hand_size =
        opponent.hand.size();
    std::vector<old_school::CardId> hidden =
        opponent.hand;
    hidden.insert(
        hidden.end(), opponent.library.begin(),
        opponent.library.end());
    if (hidden.size() > 1) {
        std::rotate(
            hidden.begin(), hidden.begin() + 1,
            hidden.end());
        std::reverse(hidden.begin(), hidden.end());
    }
    const auto hand_end =
        hidden.begin() +
        static_cast<std::ptrdiff_t>(hand_size);
    opponent.hand.assign(
        hidden.begin(), hand_end);
    opponent.library.assign(
        hand_end, hidden.end());
}

void test_dvr1_replay_recapture_and_hidden_identity() {
    const auto probe = dvr1_probe_fixture();
    const std::string production =
        old_school::probes::
            stable_priority_action_descriptor(
                old_school::PriorityAction::pass());
    const auto evidence =
        dvr1_disagreement_evidence(
            probe, production);
    const std::string model_fingerprint =
        old_school::learned_model_fingerprint(
            tiny_reference_model());
    const auto provenance =
        dvr1_provenance_fixture(probe);
    const auto original =
        old_school::probes::
            capture_dvr1_owner_visible_divergence(
                probe, production, model_fingerprint,
                provenance, evidence);
    expect(
        original.captured(),
        "DVR2 replay fixture was not captured");
    const auto decoded =
        old_school::probes::
            deserialize_dvr1_owner_visible_record(
                original.serialized_record);
    const auto replay =
        old_school::probes::
            rehydrate_dvr1_decision_probe(decoded);
    const auto recaptured =
        old_school::probes::
            capture_dvr1_owner_visible_divergence(
                replay, production, model_fingerprint,
                provenance, evidence);

    auto hidden = probe;
    repartition_opponent_hidden(hidden);
    const auto hidden_capture =
        old_school::probes::
            capture_dvr1_owner_visible_divergence(
                hidden, production, model_fingerprint,
                provenance, evidence);
    expect(
        recaptured.captured() &&
            recaptured.serialized_record ==
                original.serialized_record &&
            recaptured.record_fingerprint ==
                original.record_fingerprint &&
            hidden_capture.captured() &&
            hidden_capture.serialized_record ==
                original.serialized_record &&
            hidden_capture.record_fingerprint ==
                original.record_fingerprint,
        "DVR2 DVR1 replay/recapture or hidden repartition "
        "changed canonical bytes/fingerprint");
}

void test_semantic_control_and_canonical_agreement() {
    const auto probe = dvr1_probe_fixture();
    const std::string production =
        old_school::probes::
            stable_priority_action_descriptor(
                old_school::PriorityAction::pass());
    const auto disagreement =
        dvr1_disagreement_evidence(
            probe, production);
    auto one_thread = disagreement;
    one_thread.evaluation_threads = 1;
    expect(
        dvr2::semantic_control_score_equal(
            disagreement, one_thread),
        "DVR2 rejected a one-thread semantic control "
        "differing only in thread metadata");
    ++one_thread.confirmation_seed;
    expect(
        !dvr2::semantic_control_score_equal(
            disagreement, one_thread),
        "DVR2 normalized more than control thread metadata");

    const auto agreement =
        agreement_evidence(
            disagreement, production);
    const auto capture =
        old_school::probes::
            capture_dvr1_owner_visible_divergence(
                probe, production,
                old_school::learned_model_fingerprint(
                    tiny_reference_model()),
                dvr1_provenance_fixture(probe),
                agreement);
    expect(
        capture.disposition ==
                old_school::probes::
                    Dvr1CaptureDisposition::
                        ReferenceAgreement &&
            !capture.record.has_value() &&
            capture.serialized_record.empty() &&
            capture.record_fingerprint.empty(),
        "DVR2 canonical four-thread agreement did not "
        "produce the DVR1 ReferenceAgreement control");
}

dvr2::HarvestReport evidence_accounting_fixture() {
    dvr2::HarvestReport report;
    report.model_fingerprint =
        std::string(dvr2::kExpectedModelFingerprint);
    auto disagreement = score_fixture();
    disagreement.actual_outside_best_sets = true;
    disagreement.confirmation_regret = 0.05;
    disagreement.paired_lower_95 = 0.001;
    disagreement.information_action_fingerprint =
        "disagreement-fingerprint";
    auto agreement = score_fixture();
    agreement.information_action_fingerprint =
        "control-fingerprint";

    dvr2::RootEvidence disagreement_root{
        .source = {
            .seed_base = dvr2::kSourceSeedBases[0],
            .seed_base_index = 0,
            .schedule_index = 0,
            .pairing_index = 0,
            .game_seed = 11,
            .owner_seat = 0,
            .owner_on_play = true,
            .starting_player = 0,
            .trace_ordinal = 1,
        },
        .owner_deck = old_school::DeckId::Blue,
        .opponent_deck = old_school::DeckId::Red,
        .stratum = dvr2::Stratum::BlueOpponentTop,
        .information_action_fingerprint =
            "disagreement-fingerprint",
        .production_action_descriptor = "pass",
        .classification =
            dvr2::ReferenceClassification::
                StableDisagreement,
        .score = disagreement,
        .dvr1_record = "record",
        .dvr1_record_fingerprint = "fingerprint",
        .decoded_replay_exact = true,
    };
    dvr2::RootEvidence control_root{
        .source = {
            .seed_base = dvr2::kSourceSeedBases[0],
            .seed_base_index = 0,
            .schedule_index = 1,
            .pairing_index = 0,
            .game_seed = 12,
            .owner_seat = 1,
            .owner_on_play = false,
            .starting_player = 0,
            .trace_ordinal = 2,
        },
        .owner_deck = old_school::DeckId::Red,
        .opponent_deck = old_school::DeckId::Blue,
        .stratum = dvr2::Stratum::NonBlueOwnTop,
        .information_action_fingerprint =
            "control-fingerprint",
        .production_action_descriptor = "pass",
        .classification =
            dvr2::ReferenceClassification::
                StableAgreement,
        .score = agreement,
        .reversed_single_thread_repeat_exact = true,
        .regenerated_canonical_repeat_exact = true,
        .repeat_reference_agreement = true,
    };
    report.roots = {
        disagreement_root,
        control_root,
    };
    report.selection.selected_indices = {0, 1};
    report.selected_roots = 2;
    report.stable_disagreements = 1;
    report.stable_agreements = 1;
    report.blue_opponent_top_disagreements = 1;
    report.blue_opponent_top_high_cost = 1;
    report.reference_score_calls = 5;
    report.reference_evaluations = 2560;
    report.reference_terminal_evaluations = 500;
    report.reference_bootstrapped_evaluations = 2060;
    report.controls[static_cast<std::size_t>(
        old_school::DeckId::Red)] = {
        .retained = true,
        .budget_skipped = false,
        .source = control_root.source,
        .information_action_fingerprint =
            control_root
                .information_action_fingerprint,
        .action_count = 2,
    };
    return report;
}

void test_independent_evidence_accounting() {
    const auto report = evidence_accounting_fixture();
    expect(
        dvr2::reference_accounting_matches_evidence(
            report),
        "DVR2 rejected independently consistent evidence "
        "accounting");

    auto uncharged = report;
    --uncharged.reference_score_calls;
    expect(
        !dvr2::reference_accounting_matches_evidence(
            uncharged),
        "DVR2 accepted an uncharged replay/control call");
    auto wrong_terminal = report;
    --wrong_terminal.reference_terminal_evaluations;
    expect(
        !dvr2::reference_accounting_matches_evidence(
            wrong_terminal),
        "DVR2 accepted mutated terminal accounting");
    auto unmatched_control = report;
    ++unmatched_control
          .controls[static_cast<std::size_t>(
              old_school::DeckId::Red)]
          .source.trace_ordinal;
    expect(
        !dvr2::reference_accounting_matches_evidence(
            unmatched_control),
        "DVR2 accepted an unmatched retained control");
    auto missing_repeat = report;
    missing_repeat.roots[1]
        .regenerated_canonical_repeat_exact = false;
    expect(
        !dvr2::reference_accounting_matches_evidence(
            missing_repeat),
        "DVR2 accepted a control with a missing repeat");
    auto wrong_rs1 = report;
    wrong_rs1.blue_opponent_top_high_cost = 0;
    expect(
        !dvr2::reference_accounting_matches_evidence(
            wrong_rs1),
        "DVR2 accepted a drifted RS1 counter");
    auto missing_root_index = report;
    missing_root_index.selection.selected_indices.pop_back();
    expect(
        !dvr2::reference_accounting_matches_evidence(
            missing_root_index),
        "DVR2 accepted a root/selection count mismatch");
    auto wrong_identity = report;
    wrong_identity.roots[0]
        .score.information_action_fingerprint +=
        ".wrong";
    expect(
        !dvr2::reference_accounting_matches_evidence(
            wrong_identity),
        "DVR2 accepted a score/root fingerprint mismatch");
    auto wrong_model = report;
    wrong_model.roots[0]
        .score.reference_model_fingerprint += ".wrong";
    expect(
        !dvr2::reference_accounting_matches_evidence(
            wrong_model),
        "DVR2 accepted a score/model mismatch");
    auto wrong_descriptor = report;
    wrong_descriptor.roots[0]
        .score.actual_action_descriptor = "other";
    expect(
        !dvr2::reference_accounting_matches_evidence(
            wrong_descriptor),
        "DVR2 accepted a score/root action mismatch");
    auto wrong_seed = report;
    ++wrong_seed.roots[0]
          .score.reference_seed_base;
    expect(
        !dvr2::reference_accounting_matches_evidence(
            wrong_seed),
        "DVR2 accepted a nonfrozen reference seed");
    auto missing_mean = report;
    missing_mean.roots[0].score.action_means.pop_back();
    expect(
        !dvr2::reference_accounting_matches_evidence(
            missing_mean),
        "DVR2 accepted an incomplete action-mean table");
}

dvr2::HarvestReport valid_report_fixture() {
    dvr2::HarvestReport report;
    report.model_artifact_path =
        "build/model-cache/"
        "old-school-value-challenger-v3-c16-t800-s424242.bin";
    report.model_artifact_sha256 =
        std::string(64, 'a');
    report.model_fingerprint =
        std::string(dvr2::kExpectedModelFingerprint);
    report.exact_source_schedule = true;
    report.source_balance_passed = true;
    report.source_policy_exact = true;
    report.model_identity_passed = true;
    report.manifest_cross_sums_passed = true;
    report.reference_accounting_passed = true;
    report.invariance_passed = true;
    report.replay_passed = true;
    report.watchdog_passed = true;
    report.valid = true;
    return report;
}

void test_bundle_determinism_and_strict_checksum() {
    const auto report = valid_report_fixture();
    const std::string first =
        dvr2::serialize_payload(report);
    const std::string second =
        dvr2::serialize_payload(report);
    expect(
        first == second &&
            first.find("/Users/") == std::string::npos,
        "DVR2 payload is nondeterministic or host-specific");
    const std::string bundle =
        dvr2::make_checksummed_bundle(first);
    expect(
        dvr2::verify_checksummed_bundle(bundle),
        "DVR2 rejected its own bundle");

    std::string corrupted = bundle;
    corrupted[corrupted.size() - 2] ^= 1;
    expect(
        !dvr2::verify_checksummed_bundle(corrupted),
        "DVR2 accepted checksum corruption");
    expect(
        !dvr2::verify_checksummed_bundle(bundle + "trailing"),
        "DVR2 accepted trailing bytes");
    expect(
        !dvr2::verify_checksummed_bundle(
            "schema\t+25:old-school-dvr2-bundle-v1\n"),
        "DVR2 accepted a noncanonical length");
    expect(
        !dvr2::verify_checksummed_bundle(
            "schema\t1e2:old-school-dvr2-bundle-v1\n"),
        "DVR2 accepted a nondigit length");
    expect(
        !dvr2::verify_checksummed_bundle(
            bundle.substr(0, bundle.size() - 1)),
        "DVR2 accepted a truncated frame");

    auto absolute = report;
    absolute.model_artifact_path =
        "/machine-specific/model.bin";
    expect_rejected(
        [&] {
            static_cast<void>(
                dvr2::serialize_payload(absolute));
        },
        "DVR2 serialized an absolute model artifact path");
}

void test_atomic_no_replace_publication() {
    TemporaryDirectory temporary;
    const auto target = temporary.path() / "evidence.dvr2";
    const std::string payload =
        dvr2::serialize_payload(valid_report_fixture());
    const std::string bundle =
        dvr2::make_checksummed_bundle(payload);
    dvr2::write_bundle_atomic_no_replace(target, bundle);
    std::ifstream input(target, std::ios::binary);
    const std::string observed{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    expect(
        observed == bundle,
        "DVR2 publication bytes changed");
    expect_rejected(
        [&] {
            dvr2::write_bundle_atomic_no_replace(
                target, bundle);
        },
        "DVR2 overwrote an existing evidence path");
    std::ifstream after(target, std::ios::binary);
    const std::string unchanged{
        std::istreambuf_iterator<char>(after),
        std::istreambuf_iterator<char>()};
    expect(
        unchanged == bundle,
        "DVR2 no-replace failure mutated the target");

    const auto symlink_target =
        temporary.path() / "symlink-target";
    {
        std::ofstream marker(
            symlink_target, std::ios::binary);
        marker << "unchanged";
    }
    const auto symlink_path =
        temporary.path() / "symlink-evidence.dvr2";
    std::filesystem::create_symlink(
        symlink_target, symlink_path);
    expect_rejected(
        [&] {
            dvr2::write_bundle_atomic_no_replace(
                symlink_path, bundle);
        },
        "DVR2 followed an existing evidence symlink");
    std::ifstream marker_input(
        symlink_target, std::ios::binary);
    const std::string marker_observed{
        std::istreambuf_iterator<char>(marker_input),
        std::istreambuf_iterator<char>()};
    expect(
        marker_observed == "unchanged",
        "DVR2 symlink rejection mutated its target");

    const auto concurrent =
        temporary.path() / "concurrent.dvr2";
    std::atomic<std::size_t> successes{0};
    std::atomic<std::size_t> rejections{0};
    const auto publish = [&] {
        try {
            dvr2::write_bundle_atomic_no_replace(
                concurrent, bundle);
            ++successes;
        } catch (const std::exception&) {
            ++rejections;
        }
    };
    std::thread first(publish);
    std::thread second(publish);
    first.join();
    second.join();
    std::ifstream concurrent_input(
        concurrent, std::ios::binary);
    const std::string concurrent_bundle{
        std::istreambuf_iterator<char>(concurrent_input),
        std::istreambuf_iterator<char>()};
    expect(
        successes == 1 && rejections == 1 &&
            dvr2::verify_checksummed_bundle(
                concurrent_bundle),
        "DVR2 concurrent publication did not select "
        "exactly one valid writer");

    const auto injected =
        temporary.path() / "fault.dvr2";
    expect_rejected(
        [&] {
            dvr2::testing::
                write_bundle_atomic_no_replace(
                    injected, bundle,
                    dvr2::testing::PublicationFault::
                        AfterLinkBeforeDirectorySync);
        },
        "DVR2 post-link fault did not fail");
    expect(
        !std::filesystem::exists(
            std::filesystem::symlink_status(injected)),
        "DVR2 post-link infrastructure failure left "
        "published evidence");

    const auto expired =
        temporary.path() / "expired.dvr2";
    expect_rejected(
        [&] {
            dvr2::testing::
                write_bundle_atomic_no_replace(
                    expired, bundle,
                    dvr2::testing::PublicationFault::
                        WatchdogExpiredBeforeLink);
        },
        "DVR2 pre-link watchdog fault did not fail");
    expect(
        !std::filesystem::exists(
            std::filesystem::symlink_status(expired)),
        "DVR2 pre-link watchdog failure published evidence");
}

void test_invalid_and_watchdog_reports_do_not_publish() {
    TemporaryDirectory temporary;
    const auto attempt =
        [&](const dvr2::HarvestReport& report,
            const std::filesystem::path& target) {
            const std::string payload =
                dvr2::serialize_payload(report);
            dvr2::write_bundle_atomic_no_replace(
                target,
                dvr2::make_checksummed_bundle(payload));
        };

    auto invalid = valid_report_fixture();
    invalid.valid = false;
    const auto invalid_path =
        temporary.path() / "invalid.dvr2";
    expect_rejected(
        [&] { attempt(invalid, invalid_path); },
        "DVR2 published an invalid report");
    expect(
        !std::filesystem::exists(
            std::filesystem::symlink_status(
                invalid_path)),
        "DVR2 invalid report left an evidence path");

    auto watchdog = valid_report_fixture();
    watchdog.watchdog_passed = false;
    const auto watchdog_path =
        temporary.path() / "watchdog.dvr2";
    expect_rejected(
        [&] { attempt(watchdog, watchdog_path); },
        "DVR2 published a watchdog-expired report");
    expect(
        !std::filesystem::exists(
            std::filesystem::symlink_status(
                watchdog_path)),
        "DVR2 watchdog failure left an evidence path");
}

void test_exit_code_contract() {
    dvr2::RunResult result;
    expect(
        result.exit_code() == 2,
        "DVR2 invalid result did not exit 2");
    result.report.valid = true;
    expect(
        result.exit_code() == 2,
        "DVR2 unpublished result did not exit 2");
    result.published = true;
    expect(
        result.exit_code() == 1,
        "DVR2 valid negative did not exit 1");
    result.report.rs1_licensed = true;
    expect(
        result.exit_code() == 0,
        "DVR2 licensed result did not exit 0");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "exact frozen source schedule",
        test_exact_source_schedule);
    runner.run(
        "selection quotas and cross-sums",
        test_selection_quotas_and_cross_sums);
    runner.run(
        "impossible source matchup rejection",
        test_selection_rejects_impossible_matchup);
    runner.run(
        "reference classification and budget",
        test_reference_classification_and_budget);
    runner.run(
        "reference actual-call budget transitions",
        test_reference_budget_actual_call_transitions);
    runner.run(
        "DVR1 replay recapture and hidden identity",
        test_dvr1_replay_recapture_and_hidden_identity);
    runner.run(
        "semantic control and canonical DVR1 agreement",
        test_semantic_control_and_canonical_agreement);
    runner.run(
        "independent retained-evidence accounting",
        test_independent_evidence_accounting);
    runner.run(
        "bundle determinism and strict checksum",
        test_bundle_determinism_and_strict_checksum);
    runner.run(
        "atomic no-replace publication",
        test_atomic_no_replace_publication);
    runner.run(
        "invalid and watchdog no-publication",
        test_invalid_and_watchdog_reports_do_not_publish);
    runner.run(
        "exit-code contract",
        test_exit_code_contract);
    return runner.finish();
}
