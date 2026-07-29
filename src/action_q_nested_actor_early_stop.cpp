#include "old_school/action_q_nested_actor_early_stop.hpp"

#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <numeric>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::action_q_nested_actor_early_stop {
namespace {

void append_u64(std::string& output, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<char>(
            (value >> shift) & 0xffU));
    }
}

void append_size(std::string& output, std::size_t value) {
    append_u64(output, static_cast<std::uint64_t>(value));
}

void append_string(
    std::string& output, std::string_view value) {
    append_size(output, value.size());
    output.append(value);
}

void append_double(std::string& output, double value) {
    append_u64(
        output, std::bit_cast<std::uint64_t>(value));
}

template <typename Value>
void append_optional(
    std::string& output,
    const std::optional<Value>& value) {
    append_u64(output, value.has_value() ? 1 : 0);
    if (value.has_value()) {
        append_u64(
            output,
            static_cast<std::uint64_t>(*value));
    }
}

void append_action(
    std::string& output, const PriorityAction& action) {
    append_u64(
        output,
        static_cast<std::uint64_t>(action.kind));
    append_u64(
        output,
        static_cast<std::uint64_t>(action.card));
    append_u64(output, action.target.has_value() ? 1 : 0);
    if (action.target.has_value()) {
        append_size(output, action.target->player);
        append_optional(output, action.target->creature);
    }
    append_optional(output, action.spell_target);
    append_optional(output, action.source_permanent);
    append_u64(
        output,
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(action.x_value)));
}

void append_manifest(
    std::string& output, const g1::ManifestRoot& manifest) {
    append_string(output, "manifest");
    const g1::RootCoordinate& coordinate =
        manifest.coordinate;
    append_size(output, coordinate.schedule_index);
    append_size(output, coordinate.pairing_index);
    append_u64(output, coordinate.game_seed);
    append_size(output, coordinate.starting_player);
    for (const DeckId deck : coordinate.seat_decks) {
        append_u64(
            output, static_cast<std::uint64_t>(deck));
    }
    append_size(output, coordinate.actor);
    append_size(output, coordinate.trace_ordinal);
    append_size(output, coordinate.nontrivial_ordinal);
    append_size(
        output,
        coordinate.actor_game_nontrivial_roots);
    append_size(output, coordinate.retained_position);
    append_u64(
        output,
        static_cast<std::uint64_t>(coordinate.split));
    append_u64(output, coordinate.search_seed);

    append_string(output, "actions");
    append_size(output, manifest.actions.size());
    for (const PriorityAction& action :
         manifest.actions) {
        append_action(output, action);
    }
    append_string(output, "descriptors");
    append_size(
        output, manifest.action_descriptors.size());
    for (const std::string& descriptor :
         manifest.action_descriptors) {
        append_string(output, descriptor);
    }
    append_string(output, "options");
    append_size(output, manifest.options.size());
    for (const auto& option : manifest.options) {
        append_size(output, option.size());
        for (const double feature : option) {
            append_double(output, feature);
        }
    }
}

void append_values(
    std::string& output, std::string_view name,
    std::span<const double> values) {
    append_string(output, name);
    append_size(output, values.size());
    for (const double value : values) {
        append_double(output, value);
    }
}

void append_accounting(
    std::string& output,
    const g1::RootAccounting& accounting) {
    append_string(output, "accounting");
    append_size(output, accounting.base_sampled_worlds);
    append_size(
        output, accounting.base_rollout_evaluations);
    append_size(
        output, accounting.base_terminal_evaluations);
    append_size(
        output,
        accounting.base_bootstrapped_evaluations);
    append_size(
        output, accounting.teacher_sampled_worlds);
    append_size(
        output, accounting.teacher_rollout_evaluations);
    append_size(
        output,
        accounting.teacher_terminal_evaluations);
    append_size(
        output,
        accounting.teacher_bootstrapped_evaluations);
    append_size(
        output,
        accounting.teacher_inner_rollout_evaluations);
    append_size(
        output,
        accounting.teacher_inner_search_invocations);
    append_size(
        output,
        accounting.teacher_inner_search_max_depth);
}

void append_example(
    std::string& output,
    const g1::RootExample& example) {
    append_manifest(output, example.manifest);
    append_values(output, "base", example.base_scores);
    append_values(
        output, "teacher", example.teacher_scores);
    append_values(
        output, "target",
        example.target_probabilities);
    append_double(output, example.weight);
    append_accounting(output, example.accounting);
}

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool pooled_metric_bits(
    const g1::Metrics& metrics,
    std::size_t roots, std::size_t options,
    double agreement, double regret) {
    return metrics.roots == roots &&
           metrics.options == options &&
           same_bits(
               metrics.equal_deck_top_one_expected_agreement,
               agreement) &&
           same_bits(
               metrics.equal_deck_mean_regret, regret);
}

std::size_t option_count(
    std::span<const g1::RootExample> examples) {
    return std::accumulate(
        examples.begin(), examples.end(), std::size_t{0},
        [](std::size_t total,
           const g1::RootExample& example) {
            return total +
                   example.manifest.actions.size();
        });
}

bool is_prefix_epoch(std::size_t epochs) {
    return std::find(
               kPrefixEpochs.begin(),
               kPrefixEpochs.end(), epochs) !=
           kPrefixEpochs.end();
}

bool fit_identity_bound(
    const g1::FitReport& fit,
    std::size_t epochs) {
    if (!fit.candidate ||
        fit.optimizer != optimizer_for_epochs(epochs) ||
        fit.parent_fingerprint_before !=
            g1::kRequiredParentFingerprint ||
        fit.parent_fingerprint_after !=
            g1::kRequiredParentFingerprint ||
        fit.candidate_fingerprint !=
            learned_model_fingerprint(fit.candidate) ||
        !fit.parent_immutable ||
        !fit.repeated_fit_bit_identical ||
        !fit.only_priority_component_changed) {
        return false;
    }
    return true;
}

bool prefix_identity_bound(
    const PrefixReport& prefix) {
    return is_prefix_epoch(prefix.epochs) &&
           fit_identity_bound(prefix.fit, prefix.epochs) &&
           prefix.offline.parent_fingerprint ==
               g1::kRequiredParentFingerprint &&
           prefix.offline.candidate_fingerprint ==
               prefix.fit.candidate_fingerprint &&
           prefix.offline.parent_fit ==
               prefix.fit.parent_fit &&
           prefix.offline.candidate_fit ==
               prefix.fit.candidate_fit &&
           prefix.offline.parent_check ==
               prefix.fit.parent_check &&
           prefix.offline.candidate_check ==
               prefix.fit.candidate_check &&
           prefix.ancestral_eligible ==
               prefix.offline.ancestral.gate_passed();
}

void require_parent(
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            g1::kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ4-G2 requires exact frozen C16");
    }
}

void print_metric_row(
    std::ostream& output, std::size_t epochs,
    std::string_view split, std::string_view deck,
    const g1::Metrics& parent,
    const g1::Metrics& candidate,
    std::optional<std::size_t> deck_index) {
    double parent_agreement =
        parent.equal_deck_top_one_expected_agreement;
    double candidate_agreement =
        candidate.equal_deck_top_one_expected_agreement;
    double parent_regret =
        parent.equal_deck_mean_regret;
    double candidate_regret =
        candidate.equal_deck_mean_regret;
    if (deck_index.has_value()) {
        parent_agreement =
            parent.decks[*deck_index]
                .top_one_expected_agreement;
        candidate_agreement =
            candidate.decks[*deck_index]
                .top_one_expected_agreement;
        parent_regret =
            parent.decks[*deck_index].mean_regret;
        candidate_regret =
            candidate.decks[*deck_index].mean_regret;
    }
    output
        << std::setprecision(17)
        << "prefix_metric epochs=" << epochs
        << " split=" << split
        << " deck=" << deck
        << " parent_agreement=" << parent_agreement
        << " candidate_agreement="
        << candidate_agreement
        << " parent_regret=" << parent_regret
        << " candidate_regret=" << candidate_regret
        << " regret_delta="
        << candidate_regret - parent_regret << '\n';
}

} // namespace

bool ControlReport::gate_passed() const {
    const bool observed_fingerprint_exact =
        fit.candidate &&
        fit.candidate_fingerprint ==
            kExpectedControlFingerprint &&
        learned_model_fingerprint(fit.candidate) ==
            kExpectedControlFingerprint;
    const bool observed_metrics_exact =
        control_matches_frozen_result(fit);
    return fingerprint_exact &&
           fingerprint_exact ==
               observed_fingerprint_exact &&
           aggregate_metrics_bit_exact &&
           aggregate_metrics_bit_exact ==
               observed_metrics_exact &&
           corpus_counts_exact &&
           fit_identity_bound(fit, kControlEpochs);
}

bool PrefixReport::eligible() const {
    const bool ancestral_gate =
        offline.ancestral.gate_passed();
    return is_prefix_epoch(epochs) &&
           fit.optimizer ==
               optimizer_for_epochs(epochs) &&
           fit.parent_immutable &&
           fit.repeated_fit_bit_identical &&
           fit.only_priority_component_changed &&
           offline.parent_immutable ==
               fit.parent_immutable &&
           offline.repeated_fit_bit_identical ==
               fit.repeated_fit_bit_identical &&
           offline.only_priority_component_changed ==
               fit.only_priority_component_changed &&
           std::isfinite(
               offline.candidate_check
                   .equal_deck_mean_regret) &&
           offline.gate_passed() &&
           ancestral_eligible &&
           ancestral_eligible == ancestral_gate;
}

const PrefixReport* OfflineRunReport::selected() const {
    if (!selected_index.has_value() ||
        *selected_index >= prefixes.size()) {
        return nullptr;
    }
    return &prefixes[*selected_index];
}

bool OfflineRunReport::selection_ready() const {
    try {
        g1::validate_corpus(corpus);
        g1::require_frozen_census(corpus.census);
        if (corpus_reconstructions != 1 ||
            corpus_digest !=
                canonical_corpus_digest(corpus) ||
            !control.gate_passed() ||
            corpus.fit.size() != kExpectedFitRoots ||
            option_count(corpus.fit) !=
                kExpectedFitOptions ||
            corpus.check.size() !=
                kExpectedDevRoots ||
            option_count(corpus.check) !=
                kExpectedDevOptions ||
            !prefix_family_is_exact(prefixes)) {
            return false;
        }
        for (std::size_t index = 0;
             index < prefixes.size(); ++index) {
            if (!prefix_identity_bound(prefixes[index])) {
                return false;
            }
        }
        const auto expected =
            select_prefix(prefixes);
        return expected.has_value() &&
               selected_index == expected &&
               selected() != nullptr &&
               selected()->eligible();
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments) {
    if (arguments.size() == 1 &&
        arguments.front() == "--run") {
        return Command::Run;
    }
    return std::nullopt;
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-action-q-nested-actor-early-stop "
           "--run\n";
}

bool is_declared_epoch(std::size_t epochs) {
    return epochs == kControlEpochs ||
           is_prefix_epoch(epochs);
}

LearnedValuePriorityHeadUpdateConfig
optimizer_for_epochs(std::size_t epochs) {
    if (!is_declared_epoch(epochs)) {
        throw std::invalid_argument(
            "AQ4-G2 epoch count is outside the frozen ladder");
    }
    LearnedValuePriorityHeadUpdateConfig optimizer =
        g1::optimizer_config();
    optimizer.epochs = epochs;
    return optimizer;
}

std::string canonical_corpus_digest(
    const g1::Corpus& corpus) {
    std::string payload;
    append_string(
        payload,
        "old-school-action-q-aq4-g2-owner-safe-corpus-v1");
    append_string(payload, "census");
    append_u64(payload, corpus.census.root_seed);
    append_string(
        payload, corpus.census.parent_fingerprint);
    append_size(payload, corpus.census.games);
    append_string(
        payload, corpus.census.manifest_hash);
    append_size(payload, corpus.census.decks.size());
    for (const g1::DeckCensus& deck :
         corpus.census.decks) {
        append_size(payload, deck.actor_games);
        append_size(payload, deck.nontrivial_roots);
        for (std::size_t split = 0; split < 2; ++split) {
            append_size(
                payload, deck.retained_roots[split]);
            append_size(
                payload, deck.retained_options[split]);
        }
    }
    append_size(payload, corpus.census.roots.size());
    for (const g1::ManifestRoot& manifest :
         corpus.census.roots) {
        append_manifest(payload, manifest);
    }
    const auto append_split =
        [&](std::string_view name,
            const std::vector<g1::RootExample>& examples) {
            append_string(payload, name);
            append_size(payload, examples.size());
            for (const g1::RootExample& example : examples) {
                append_example(payload, example);
            }
        };
    append_split("fit", corpus.fit);
    append_split("dev", corpus.check);
    return artifact_integrity::sha256_string(payload);
}

bool control_matches_frozen_result(
    const g1::FitReport& control) {
    return control.candidate_fingerprint ==
               kExpectedControlFingerprint &&
           control.optimizer ==
               optimizer_for_epochs(kControlEpochs) &&
           control.fit_examples == kExpectedFitRoots &&
           control.fit_options == kExpectedFitOptions &&
           pooled_metric_bits(
               control.parent_fit,
               kExpectedFitRoots,
               kExpectedFitOptions,
               kExpectedParentFitAgreement,
               kExpectedParentFitRegret) &&
           pooled_metric_bits(
               control.candidate_fit,
               kExpectedFitRoots,
               kExpectedFitOptions,
               kExpectedControlFitAgreement,
               kExpectedControlFitRegret) &&
           pooled_metric_bits(
               control.parent_check,
               kExpectedDevRoots,
               kExpectedDevOptions,
               kExpectedParentDevAgreement,
               kExpectedParentDevRegret) &&
           pooled_metric_bits(
               control.candidate_check,
               kExpectedDevRoots,
               kExpectedDevOptions,
               kExpectedControlDevAgreement,
               kExpectedControlDevRegret);
}

bool prefix_family_is_exact(
    std::span<const PrefixReport> prefixes) {
    if (prefixes.size() != kPrefixEpochs.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < prefixes.size(); ++index) {
        if (prefixes[index].epochs !=
                kPrefixEpochs[index] ||
            prefixes[index].fit.optimizer !=
                optimizer_for_epochs(
                    kPrefixEpochs[index])) {
            return false;
        }
    }
    return true;
}

std::optional<std::size_t> select_prefix(
    std::span<const PrefixReport> prefixes) {
    std::optional<std::size_t> selected;
    for (std::size_t index = 0;
         index < prefixes.size(); ++index) {
        if (!prefixes[index].eligible()) {
            continue;
        }
        if (!selected.has_value()) {
            selected = index;
            continue;
        }
        const PrefixReport& incumbent =
            prefixes[*selected];
        const double challenger_regret =
            prefixes[index]
                .offline.candidate_check
                .equal_deck_mean_regret;
        const double incumbent_regret =
            incumbent.offline.candidate_check
                .equal_deck_mean_regret;
        if (challenger_regret < incumbent_regret ||
            (challenger_regret == incumbent_regret &&
             prefixes[index].epochs <
                 incumbent.epochs)) {
            selected = index;
        }
    }
    return selected;
}

OfflineRunReport run_offline(
    std::shared_ptr<const LearnedModel> parent) {
    require_parent(parent);
    OfflineRunReport report;
    const g1::Census census =
        g1::collect_census(parent);
    g1::require_frozen_census(census);
    report.corpus =
        g1::collect_corpus(parent, census);
    report.corpus_reconstructions = 1;
    report.corpus_digest =
        canonical_corpus_digest(report.corpus);

    report.control.fit =
        g1::fit_with_optimizer(
            report.corpus, parent,
            optimizer_for_epochs(kControlEpochs));
    report.control.fingerprint_exact =
        report.control.fit.candidate &&
        report.control.fit.candidate_fingerprint ==
            kExpectedControlFingerprint &&
        learned_model_fingerprint(
            report.control.fit.candidate) ==
            kExpectedControlFingerprint;
    report.control.aggregate_metrics_bit_exact =
        control_matches_frozen_result(
            report.control.fit);
    report.control.corpus_counts_exact =
        report.corpus.fit.size() ==
            kExpectedFitRoots &&
        option_count(report.corpus.fit) ==
            kExpectedFitOptions &&
        report.corpus.check.size() ==
            kExpectedDevRoots &&
        option_count(report.corpus.check) ==
            kExpectedDevOptions;
    if (!report.control.gate_passed()) {
        return report;
    }

    report.prefixes.reserve(kPrefixEpochs.size());
    for (const std::size_t epochs : kPrefixEpochs) {
        PrefixReport prefix;
        prefix.epochs = epochs;
        prefix.fit =
            g1::fit_with_optimizer(
                report.corpus, parent,
                optimizer_for_epochs(epochs));
        prefix.offline =
            g1::evaluate_offline(
                report.corpus, prefix.fit,
                parent, prefix.fit.candidate);
        prefix.ancestral_eligible =
            prefix.offline.ancestral.gate_passed();
        report.prefixes.push_back(std::move(prefix));
    }
    report.selected_index =
        select_prefix(report.prefixes);
    return report;
}

BotBenchmarkSummary run_selector(
    std::shared_ptr<const LearnedModel> parent,
    const OfflineRunReport& report) {
    require_parent(parent);
    if (!report.selection_ready()) {
        throw std::invalid_argument(
            "AQ4-G2 selector requires a bound eligible prefix");
    }
    const PrefixReport& selected = *report.selected();
    if (!selected.offline.ancestral.gate_passed() ||
        selected.fit.candidate_fingerprint !=
            learned_model_fingerprint(
                selected.fit.candidate)) {
        throw std::invalid_argument(
            "AQ4-G2 selected prefix identity drifted");
    }

    GameConfig game;
    game.max_turns = 500;
    game.learned_training_seed = 424242;
    game.learned_search_depth = 1;
    const BotBenchmarkSummary summary =
        run_bot_benchmark(
            g1::kSelectorRepetitions,
            kSelectorSeed,
            g1::selector_bot_config(
                selected.fit.candidate,
                g1::kCandidateResidualWeight),
            g1::selector_bot_config(parent, 0.0),
            game, false);
    g1::validate_selector_summary(
        summary, parent, selected.fit.candidate,
        kSelectorSeed);
    return summary;
}

void print_offline(
    std::ostream& output,
    const OfflineRunReport& report) {
    output
        << "schema=old-school-action-q-aq4-g2-early-stop-v1\n"
        << "mode=run corpus_digest="
        << report.corpus_digest
        << " corpus_reconstructions="
        << report.corpus_reconstructions
        << " fit_roots=" << report.corpus.fit.size()
        << " fit_options=" << option_count(report.corpus.fit)
        << " dev_roots=" << report.corpus.check.size()
        << " dev_options="
        << option_count(report.corpus.check) << '\n'
        << "control epochs=" << kControlEpochs
        << " fingerprint="
        << report.control.fit.candidate_fingerprint
        << " fingerprint_exact="
        << report.control.fingerprint_exact
        << " metric_bits_exact="
        << report.control.aggregate_metrics_bit_exact
        << " corpus_counts_exact="
        << report.control.corpus_counts_exact
        << " result="
        << (report.control.gate_passed()
                ? "PASS"
                : "FAIL")
        << '\n'
        << std::setprecision(17)
        << "control_metrics"
        << " parent_fit_agreement="
        << report.control.fit.parent_fit
               .equal_deck_top_one_expected_agreement
        << " parent_fit_regret="
        << report.control.fit.parent_fit
               .equal_deck_mean_regret
        << " control_fit_agreement="
        << report.control.fit.candidate_fit
               .equal_deck_top_one_expected_agreement
        << " control_fit_regret="
        << report.control.fit.candidate_fit
               .equal_deck_mean_regret
        << " parent_dev_agreement="
        << report.control.fit.parent_check
               .equal_deck_top_one_expected_agreement
        << " parent_dev_regret="
        << report.control.fit.parent_check
               .equal_deck_mean_regret
        << " control_dev_agreement="
        << report.control.fit.candidate_check
               .equal_deck_top_one_expected_agreement
        << " control_dev_regret="
        << report.control.fit.candidate_check
               .equal_deck_mean_regret
        << '\n';
    if (!report.control.gate_passed()) {
        output
            << "result=REJECT stage=CONTROL"
            << " selector_seed_opened=0"
            << " artifact_published=0\n";
        return;
    }

    for (const PrefixReport& prefix : report.prefixes) {
        output
            << "prefix epochs=" << prefix.epochs
            << " fingerprint="
            << prefix.fit.candidate_fingerprint
            << " g1_offline="
            << prefix.offline.gate_passed()
            << " descriptor="
            << prefix.offline.descriptor_order_identity
            << " redundant_counter="
            << prefix.offline.redundant_counter_pass
            << " braingeyser_x0="
            << prefix.offline.braingeyser_x_zero_excluded
            << " sick_growth="
            << prefix.offline.sick_bear_growth_pass
            << " force_spike="
            << prefix.offline.live_force_spike
            << " ancestral="
            << prefix.ancestral_eligible
            << " ancestral_self="
            << prefix.offline.ancestral.self_score
            << " ancestral_opponent="
            << prefix.offline.ancestral.opponent_score
            << " ancestral_self_above="
            << prefix.offline.ancestral
                   .self_strictly_above_opponent
            << " ancestral_opponent_absent="
            << prefix.offline.ancestral
                   .opponent_absent_from_support
            << " ancestral_hidden_identity="
            << prefix.offline.ancestral
                   .hidden_repartition_bit_identical
            << " eligible=" << prefix.eligible()
            << '\n';
        print_metric_row(
            output, prefix.epochs, "FIT", "ALL",
            prefix.fit.parent_fit,
            prefix.fit.candidate_fit,
            std::nullopt);
        print_metric_row(
            output, prefix.epochs, "DEV", "ALL",
            prefix.fit.parent_check,
            prefix.fit.candidate_check,
            std::nullopt);
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const std::string_view name =
                deck_name(static_cast<DeckId>(deck));
            print_metric_row(
                output, prefix.epochs, "FIT", name,
                prefix.fit.parent_fit,
                prefix.fit.candidate_fit, deck);
            print_metric_row(
                output, prefix.epochs, "DEV", name,
                prefix.fit.parent_check,
                prefix.fit.candidate_check, deck);
            output
                << "prefix_dev_guard epochs="
                << prefix.epochs
                << " deck=" << name
                << " passed="
                << prefix.offline
                       .check_deck_regret_guard[deck]
                << '\n';
        }
    }
    const PrefixReport* selected = report.selected();
    output
        << "selection ready=" << report.selection_ready()
        << " selected_epochs="
        << (selected == nullptr ? 0 : selected->epochs)
        << " selected_dev_regret="
        << (selected == nullptr
                ? std::numeric_limits<double>::quiet_NaN()
                : selected->offline.candidate_check
                      .equal_deck_mean_regret)
        << '\n';
    if (!report.selection_ready()) {
        output
            << "result=REJECT stage=MODEL_SELECTION"
            << " selector_seed_opened=0"
            << " artifact_published=0\n";
    }
}

void print_selector(
    std::ostream& output,
    const BotBenchmarkSummary& summary) {
    g1::print_selector(
        output, summary,
        g1::classify_selector(summary));
}

} // namespace old_school::action_q_nested_actor_early_stop
