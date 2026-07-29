#pragma once

#include "old_school/action_q_nested_actor_distill.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::action_q_nested_actor_early_stop {

namespace g1 = action_q_nested_actor_distill;

inline constexpr std::uint64_t kSelectorSeed =
    202607282131ULL;
inline constexpr std::size_t kControlEpochs = 64;
inline constexpr std::array<std::size_t, 4> kPrefixEpochs{
    4, 8, 16, 32,
};
inline constexpr std::string_view
    kExpectedControlFingerprint =
        "e0217302d83a4949950af84ab754e38be6ebbd6c2adac6a419"
        "3f05f70b7a1376";
inline constexpr std::size_t kExpectedFitRoots = 320;
inline constexpr std::size_t kExpectedFitOptions = 1023;
inline constexpr std::size_t kExpectedDevRoots = 319;
inline constexpr std::size_t kExpectedDevOptions = 1018;
inline constexpr double kExpectedParentFitAgreement =
    0.66874999999999996;
inline constexpr double kExpectedParentFitRegret =
    0.018050745526697963;
inline constexpr double kExpectedControlFitAgreement =
    0.82187500000000002;
inline constexpr double kExpectedControlFitRegret =
    0.0062735556307336555;
inline constexpr double kExpectedParentDevAgreement =
    0.727281746031746;
inline constexpr double kExpectedParentDevRegret =
    0.015804978455738215;
inline constexpr double kExpectedControlDevAgreement =
    0.69875992063492065;
inline constexpr double kExpectedControlDevRegret =
    0.014688536185484463;

enum class Command {
    Run,
};

struct ControlReport {
    g1::FitReport fit;
    bool fingerprint_exact = false;
    bool aggregate_metrics_bit_exact = false;
    bool corpus_counts_exact = false;

    bool gate_passed() const;
};

struct PrefixReport {
    std::size_t epochs = 0;
    g1::FitReport fit;
    g1::OfflineReport offline;
    bool ancestral_eligible = false;

    bool eligible() const;
};

struct OfflineRunReport {
    g1::Corpus corpus;
    std::string corpus_digest;
    std::size_t corpus_reconstructions = 0;
    ControlReport control;
    std::vector<PrefixReport> prefixes;
    std::optional<std::size_t> selected_index;

    bool selection_ready() const;
    const PrefixReport* selected() const;
};

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

bool is_declared_epoch(std::size_t epochs);
LearnedValuePriorityHeadUpdateConfig
optimizer_for_epochs(std::size_t epochs);
std::string canonical_corpus_digest(
    const g1::Corpus& corpus);
bool control_matches_frozen_result(
    const g1::FitReport& control);
bool prefix_family_is_exact(
    std::span<const PrefixReport> prefixes);
std::optional<std::size_t> select_prefix(
    std::span<const PrefixReport> prefixes);

OfflineRunReport run_offline(
    std::shared_ptr<const LearnedModel> parent);
BotBenchmarkSummary run_selector(
    std::shared_ptr<const LearnedModel> parent,
    const OfflineRunReport& report);

void print_offline(
    std::ostream& output,
    const OfflineRunReport& report);
void print_selector(
    std::ostream& output,
    const BotBenchmarkSummary& summary);

} // namespace old_school::action_q_nested_actor_early_stop
