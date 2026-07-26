#include "old_school/game.hpp"
#include "old_school/probe_runner.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kTrainingGames = 800;
constexpr std::uint64_t kTrainingSeed = 424242;
constexpr std::size_t kGenerations = 16;
constexpr std::string_view kExpectedFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

void print_keys(const std::vector<std::string>& keys) {
    for (std::size_t index = 0; index < keys.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        std::cout << keys[index];
    }
}

} // namespace

int main() {
    try {
        const std::string path =
            old_school::learned_value_challenger_cache_path(
                kTrainingGames, kTrainingSeed, kGenerations);
        const auto model =
            old_school::load_learned_value_challenger_artifact(
                path, kTrainingGames, kTrainingSeed, kGenerations)
                .model();
        const std::string fingerprint =
            old_school::learned_model_fingerprint(model);
        if (fingerprint != kExpectedFingerprint) {
            throw std::runtime_error(
                "frozen C16 fingerprint mismatch: expected " +
                std::string(kExpectedFingerprint) + ", got " +
                fingerprint);
        }

        const auto report =
            old_school::probe_runner::score_attack_regression_v1(
                {
                    .name = "Frozen C16",
                    .model = model,
                },
                {
                    .name = "Frozen C16 repeat",
                    .model = model,
                });

        std::cout << std::fixed << std::setprecision(10);
        std::cout << "Post-C17 Attack Regression v1\n";
        std::cout << "artifact: " << path << '\n';
        std::cout << "fingerprint: " << fingerprint << '\n';
        std::cout << "fixture: " << report.stable_id << '\n';
        std::cout << "reference best: ";
        print_keys(report.reference_label.reference_best_set);
        std::cout << '\n';
        for (const auto& candidate :
             report.reference_label.candidates) {
            std::cout << "reference Q[" << candidate.key
                      << "]: " << candidate.q << '\n';
        }
        for (const auto& pair : report.reference_label.pairs) {
            std::cout << "reference delta Q[" << pair.first
                      << " - " << pair.second << "]: "
                      << pair.delta_q << ", paired SE "
                      << pair.paired_standard_error << '\n';
        }
        for (const auto& score :
             report.parent.deployment.scores) {
            std::cout << "production score[" << score.key
                      << "]: " << score.score << '\n';
        }
        std::cout << "production selected: ";
        print_keys(report.parent.deployment.selected_keys);
        std::cout << '\n';
        std::cout << "production selection is reference-best: "
                  << (report.parent.selects_reference_best
                          ? "yes"
                          : "no")
                  << '\n';
        std::cout << "production reference regret: "
                  << report.parent.regret << '\n';
        std::cout << "hidden repartition: "
                  << (report.hidden_repartition.passed
                          ? "PASS"
                          : "FAIL")
                  << '\n';
        std::cout << "reference evaluations: "
                  << report.reference_accounting
                         .rollout_evaluations
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Attack regression failed: "
                  << error.what() << '\n';
        return 2;
    }
}
