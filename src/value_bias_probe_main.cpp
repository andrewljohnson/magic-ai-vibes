// Value-bias probe: quantifies whether an SPZ value net systematically
// rewards the RESIDUE of spending — bigger own graveyard, fewer cards in
// hand — instead of the resources themselves. Suspected symptoms: passing
// into a cleanup discard scoring above a free Mox deploy, self-Disenchant
// of a Black Vise, animating Factories without attacking.
//
// Method: generate a corpus of realistic mid-game decision states from
// champion-config self-play (rollout, 4 worlds, gamma 0.98) across the
// 8-deck metagame, then apply minimal counterfactual zone transforms to
// each state and measure the net's value delta from the active player's
// perspective. Transforms only feed the feature extractor — they never
// resolve rules — so the conservation-violating graveyard pads are
// legitimate feature-space measurements, not reachable game states (the
// extractor clamps derived hidden-pool counts at zero, so padding is
// safe).
//
// Transforms (each independent; skipped where inapplicable):
//   discard      hand -> own graveyard      expected <= 0
//   undiscard    own graveyard -> hand      expected >= 0
//   mox-deploy   Mox from hand -> play      expected >= 0
//   own-gy-pad   conjure land into own gy   expected ~ 0
//   opp-gy-pad   conjure land into opp gy   expected ~ 0 (or slightly +)

#include "old_school/game.hpp"
#include "old_school/selfplay_zero.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace old_school;
namespace spz = old_school::selfplay_zero;

namespace {

constexpr std::array<CardId, 5> kMoxes = {
    CardId::MoxSapphire, CardId::MoxPearl, CardId::MoxRuby,
    CardId::MoxEmerald, CardId::MoxJet};
constexpr std::array<CardId, 5> kBasics = {
    CardId::Forest, CardId::Mountain, CardId::Island, CardId::Plains,
    CardId::Swamp};

constexpr double kNoiseBand = 0.005;
constexpr std::size_t kTransformCount = 5;
constexpr std::array<const char*, kTransformCount> kTransformNames = {
    "discard", "undiscard", "mox-deploy", "own-gy-pad", "opp-gy-pad"};
constexpr std::array<const char*, kTransformCount> kExpectations = {
    "<= 0", ">= 0", ">= 0", "~ 0", "~ 0/+"};
// Which tail is the bias signature: +1 counts delta > +band (graveyard /
// spent-resource credit), -1 counts delta < -band (resource-in-hand or
// free-deploy penalty).
constexpr std::array<int, kTransformCount> kBiasSign = {+1, -1, -1, +1,
                                                        +1};

struct Sample {
    GameState state;
    std::array<std::size_t, 2> deck_index = {0, 0};
    std::size_t turn = 0;
    std::size_t player = 0;  // active player; the evaluated perspective
};

struct DeltaRecord {
    double delta = 0.0;
    std::size_t deck = 0;  // spz_decks() index of the evaluated player
    std::size_t turn = 0;
};

std::uint64_t mix_seed(std::uint64_t seed, std::uint64_t salt) {
    std::uint64_t z = seed + 0x9E3779B97F4A7C15ULL * (salt + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void run_jobs(std::size_t job_count, std::size_t threads,
              const std::function<void(std::size_t)>& job) {
    const std::size_t workers =
        std::max<std::size_t>(1, std::min(threads, job_count));
    if (workers <= 1) {
        for (std::size_t index = 0; index < job_count; ++index) {
            job(index);
        }
        return;
    }
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> pool;
    pool.reserve(workers);
    for (std::size_t worker = 0; worker < workers; ++worker) {
        pool.emplace_back([&]() {
            while (true) {
                const std::size_t index =
                    next.fetch_add(1, std::memory_order_relaxed);
                if (index >= job_count) {
                    return;
                }
                job(index);
            }
        });
    }
    for (std::thread& worker : pool) {
        worker.join();
    }
}

// The controller's value path (value_for): terminal dead-checks, then
// perspective-safe features through the schema dispatcher, then the net.
double state_value(const spz::SpzNet& net, const GameState& state,
                   std::size_t perspective,
                   const std::array<std::vector<CardId>, 2>& decks) {
    const bool self_dead = state.players[perspective].life <= 0 ||
                           state.failed_draw[perspective];
    const bool opponent_dead =
        state.players[1 - perspective].life <= 0 ||
        state.failed_draw[1 - perspective];
    if (self_dead || opponent_dead) {
        if (self_dead && opponent_dead) {
            return 0.5;
        }
        return self_dead ? 0.0 : 1.0;
    }
    return net.value(spz::spz_features_for(
        net.input_count(), observe_game_state(state, perspective), decks,
        TurnPhase::FirstMain));
}

// A land the deck actually runs, for the graveyard-pad probes. Prefers a
// basic (maximally inert signal); decks with zero basics (robots) fall
// back to any land in the decklist so every deck stays measurable.
std::optional<CardId> pad_land(const std::vector<CardId>& deck) {
    for (const CardId basic : kBasics) {
        if (std::find(deck.begin(), deck.end(), basic) != deck.end()) {
            return basic;
        }
    }
    for (const CardId card : deck) {
        if (card_definition(card).type == CardType::Land) {
            return card;
        }
    }
    return std::nullopt;
}

template <typename T>
const T& pick(const std::vector<T>& options, std::mt19937_64& rng) {
    std::uniform_int_distribution<std::size_t> index(0,
                                                     options.size() - 1);
    return options[index(rng)];
}

std::optional<GameState> apply_transform(
    std::size_t transform, const Sample& sample,
    const std::array<std::vector<CardId>, 2>& decks,
    std::mt19937_64& rng) {
    GameState state = sample.state;
    PlayerState& me = state.players[sample.player];
    PlayerState& opponent = state.players[1 - sample.player];
    switch (transform) {
        case 0: {  // discard: random hand card -> own graveyard
            if (me.hand.empty()) {
                return std::nullopt;
            }
            std::uniform_int_distribution<std::size_t> index(
                0, me.hand.size() - 1);
            const std::size_t at = index(rng);
            me.graveyard.push_back(me.hand[at]);
            me.hand.erase(me.hand.begin() +
                          static_cast<std::ptrdiff_t>(at));
            return state;
        }
        case 1: {  // undiscard: random own graveyard card -> hand
            if (me.graveyard.empty()) {
                return std::nullopt;
            }
            std::uniform_int_distribution<std::size_t> index(
                0, me.graveyard.size() - 1);
            const std::size_t at = index(rng);
            me.hand.push_back(me.graveyard[at]);
            me.graveyard.erase(me.graveyard.begin() +
                               static_cast<std::ptrdiff_t>(at));
            return state;
        }
        case 2: {  // mox-deploy: Mox from hand -> battlefield, untapped
            std::vector<std::size_t> mox_positions;
            for (std::size_t at = 0; at < me.hand.size(); ++at) {
                if (std::find(kMoxes.begin(), kMoxes.end(),
                              me.hand[at]) != kMoxes.end()) {
                    mox_positions.push_back(at);
                }
            }
            if (mox_positions.empty()) {
                return std::nullopt;
            }
            const std::size_t at = pick(mox_positions, rng);
            me.artifacts.push_back(ArtifactPermanent{
                .id = state.next_permanent_id++,
                .card = me.hand[at],
                .tapped = false,
            });
            me.hand.erase(me.hand.begin() +
                          static_cast<std::ptrdiff_t>(at));
            return state;
        }
        case 3: {  // own-gy-pad: conjure a run land into own graveyard
            const auto land = pad_land(decks[sample.player]);
            if (!land) {
                return std::nullopt;
            }
            me.graveyard.push_back(*land);
            return state;
        }
        case 4: {  // opp-gy-pad: same on the opponent's graveyard
            const auto land = pad_land(decks[1 - sample.player]);
            if (!land) {
                return std::nullopt;
            }
            opponent.graveyard.push_back(*land);
            return state;
        }
        default:
            return std::nullopt;
    }
}

double percentile(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) {
        return 0.0;
    }
    const double position = q * static_cast<double>(sorted.size() - 1);
    const std::size_t low = static_cast<std::size_t>(position);
    const std::size_t high = std::min(low + 1, sorted.size() - 1);
    const double fraction = position - static_cast<double>(low);
    return sorted[low] * (1.0 - fraction) + sorted[high] * fraction;
}

struct Aggregate {
    std::size_t n = 0;
    double mean = 0.0;
    double share_above = 0.0;  // delta > +kNoiseBand
    double share_below = 0.0;  // delta < -kNoiseBand
    double p25 = 0.0;
    double p50 = 0.0;
    double p75 = 0.0;
};

Aggregate aggregate_deltas(const std::vector<double>& deltas) {
    Aggregate out;
    out.n = deltas.size();
    if (deltas.empty()) {
        return out;
    }
    double sum = 0.0;
    std::size_t above = 0;
    std::size_t below = 0;
    for (const double delta : deltas) {
        sum += delta;
        above += delta > kNoiseBand ? 1 : 0;
        below += delta < -kNoiseBand ? 1 : 0;
    }
    const double count = static_cast<double>(deltas.size());
    out.mean = sum / count;
    out.share_above = static_cast<double>(above) / count;
    out.share_below = static_cast<double>(below) / count;
    std::vector<double> sorted = deltas;
    std::sort(sorted.begin(), sorted.end());
    out.p25 = percentile(sorted, 0.25);
    out.p50 = percentile(sorted, 0.50);
    out.p75 = percentile(sorted, 0.75);
    return out;
}

std::size_t turn_bucket(std::size_t turn) {
    if (turn <= 6) {
        return 0;
    }
    return turn <= 10 ? 1 : 2;
}

constexpr std::array<const char*, 3> kTurnBucketNames = {"3-6", "7-10",
                                                         "11-15"};

void write_aggregate_fields(std::ostream& out, const Aggregate& stats) {
    out << "\"n\":" << stats.n << ",\"mean\":" << stats.mean
        << ",\"share_above_band\":" << stats.share_above
        << ",\"share_below_band\":" << stats.share_below
        << ",\"p25\":" << stats.p25 << ",\"p50\":" << stats.p50
        << ",\"p75\":" << stats.p75;
}

void write_aggregate_json(std::ostream& out, const Aggregate& stats) {
    out << "{";
    write_aggregate_fields(out, stats);
    out << "}";
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t games = 200;
    std::size_t samples_per_game = 10;
    std::uint64_t seed = 20260807;
    std::size_t threads = std::thread::hardware_concurrency();
    std::string output_path = "build/telemetry/value-bias.json";
    std::string pilot_model = "data/spz-champion.txt";
    std::string pilot_advantage = "data/spz-advantage.txt";
    std::vector<std::string> models;
    for (int arg = 1; arg < argc; ++arg) {
        const std::string flag = argv[arg];
        const auto value = [&]() -> std::string {
            if (arg + 1 >= argc) {
                std::cerr << "missing value for " << flag << "\n";
                std::exit(1);
            }
            return argv[++arg];
        };
        if (flag == "--games") {
            games = std::stoul(value());
        } else if (flag == "--samples-per-game") {
            samples_per_game = std::stoul(value());
        } else if (flag == "--seed") {
            seed = std::stoull(value());
        } else if (flag == "--threads") {
            threads = std::stoul(value());
        } else if (flag == "--out") {
            output_path = value();
        } else if (flag == "--pilot-model") {
            pilot_model = value();
        } else if (flag == "--model") {
            models.push_back(value());
        } else {
            std::cerr << "unknown flag " << flag << "\n";
            return 1;
        }
    }
    if (models.empty()) {
        models = {"data/spz-champion.txt", "data/spz-real6.txt.96bak"};
    }

    // ------------------------------------------------------------------
    // Corpus: champion-config self-play (the benchmark policy: rollout,
    // 4 worlds, gamma 0.98) over the 8-deck metagame; ~10 decision
    // states per game sampled from turn-start snapshots (the state at
    // the active player's first priority decision of the turn), turns
    // 3-15.
    const auto pilot_net = std::make_shared<const spz::SpzNet>(
        spz::load_spz_net(pilot_model));
    std::shared_ptr<const spz::SpzAdvantageNet> pilot_advantage_net;
    {
        std::ifstream probe(pilot_advantage);
        if (probe) {
            pilot_advantage_net =
                std::make_shared<const spz::SpzAdvantageNet>(
                    spz::load_spz_advantage_net(pilot_advantage));
        }
    }

    const auto& decks = spz::spz_decks();
    std::vector<Sample> samples;
    std::mutex samples_mutex;
    std::atomic<std::size_t> games_done{0};
    std::cout << "generating corpus: " << games
              << " self-play games (pilot " << pilot_model << ", "
              << pilot_net->input_count() << " inputs, threads "
              << threads << ")\n";
    run_jobs(games, threads, [&](std::size_t game) {
        const std::size_t deck_zero = game % spz::kSpzDeckCount;
        const std::size_t deck_one =
            (game / spz::kSpzDeckCount) % spz::kSpzDeckCount;
        const std::uint64_t game_seed = mix_seed(seed, game);
        const std::array<std::vector<CardId>, 2> game_decks = {
            decks[deck_zero], decks[deck_one]};
        GameConfig config;
        config.max_turns = 200;
        config.starting_player = game % 2;
        for (std::size_t seat = 0; seat < 2; ++seat) {
            spz::SpzPolicyConfig policy;
            policy.worlds = 4;
            policy.block_prediction_worlds = 4;
            policy.rollout = true;
            policy.gamma_per_turn = 0.98;
            policy.seed = game_seed + seat;
            config.human_controllers[seat] = spz::make_spz_controller(
                pilot_net, game_decks, seat, policy, nullptr, nullptr,
                pilot_advantage_net);
        }
        Game match(game_decks[0], game_decks[1], game_seed,
                   std::move(config));
        std::vector<GameState> trace;
        match.run_with_trace(trace);
        // Turn-start snapshots only: empty stack, the active player is
        // about to take their FirstMain priority decision.
        std::vector<const GameState*> eligible;
        for (const GameState& state : trace) {
            if (state.stack.empty() && state.turn_number >= 3 &&
                state.turn_number <= 15) {
                eligible.push_back(&state);
            }
        }
        std::mt19937_64 rng(mix_seed(game_seed, 0x5A11));
        std::shuffle(eligible.begin(), eligible.end(), rng);
        const std::size_t take =
            std::min(samples_per_game, eligible.size());
        {
            std::lock_guard<std::mutex> lock(samples_mutex);
            for (std::size_t index = 0; index < take; ++index) {
                const GameState& state = *eligible[index];
                samples.push_back(Sample{
                    .state = state,
                    .deck_index = {deck_zero, deck_one},
                    .turn = state.turn_number,
                    .player = state.active_player,
                });
            }
        }
        const std::size_t done = games_done.fetch_add(1) + 1;
        if (done % 10 == 0 || done == games) {
            std::lock_guard<std::mutex> lock(samples_mutex);
            std::cout << "  " << done << "/" << games << " games, "
                      << samples.size() << " states\n"
                      << std::flush;
        }
    });
    std::cout << "corpus: " << samples.size() << " decision states\n";

    // ------------------------------------------------------------------
    // Probe each model on the shared corpus.
    std::ostringstream json;
    json << "{\n  \"seed\": " << seed << ",\n  \"games\": " << games
         << ",\n  \"pilot_model\": \"" << pilot_model
         << "\",\n  \"corpus_states\": " << samples.size()
         << ",\n  \"noise_band\": " << kNoiseBand
         << ",\n  \"models\": [";
    bool first_model = true;

    for (const std::string& model_path : models) {
        std::optional<spz::SpzNet> net;
        std::string skip_note;
        try {
            net.emplace(spz::load_spz_net(model_path));
        } catch (const std::exception& error) {
            skip_note = std::string("load failed: ") + error.what();
        }
        if (net && !samples.empty()) {
            // Schema probe: the dispatcher throws for input counts that
            // no current extractor produces (card pool moved on).
            try {
                const Sample& probe = samples.front();
                const std::array<std::vector<CardId>, 2> probe_decks = {
                    decks[probe.deck_index[0]],
                    decks[probe.deck_index[1]]};
                spz::spz_features_for(
                    net->input_count(),
                    observe_game_state(probe.state, probe.player),
                    probe_decks, TurnPhase::FirstMain);
            } catch (const std::exception& error) {
                skip_note = std::string("schema mismatch: ") +
                            error.what();
                net.reset();
            }
        }
        if (!first_model) {
            json << ",";
        }
        first_model = false;
        if (!net) {
            std::cout << "\nmodel " << model_path << ": SKIPPED ("
                      << skip_note << ")\n";
            json << "\n    {\"path\": \"" << model_path
                 << "\", \"loaded\": false, \"note\": \"" << skip_note
                 << "\"}";
            continue;
        }

        std::array<std::vector<DeltaRecord>, kTransformCount> records;
        std::mutex records_mutex;
        std::vector<double> baselines(samples.size(), 0.0);
        run_jobs(samples.size(), threads, [&](std::size_t index) {
            const Sample& sample = samples[index];
            const std::array<std::vector<CardId>, 2> game_decks = {
                decks[sample.deck_index[0]],
                decks[sample.deck_index[1]]};
            const double v0 = state_value(*net, sample.state,
                                          sample.player, game_decks);
            baselines[index] = v0;
            const std::size_t deck_of_player =
                sample.deck_index[sample.player];
            for (std::size_t transform = 0;
                 transform < kTransformCount; ++transform) {
                std::mt19937_64 rng(
                    mix_seed(seed, index * kTransformCount + transform));
                const auto transformed = apply_transform(
                    transform, sample, game_decks, rng);
                if (!transformed) {
                    continue;
                }
                const double delta =
                    state_value(*net, *transformed, sample.player,
                                game_decks) -
                    v0;
                std::lock_guard<std::mutex> lock(records_mutex);
                records[transform].push_back(DeltaRecord{
                    .delta = delta,
                    .deck = deck_of_player,
                    .turn = sample.turn,
                });
            }
        });

        double baseline_mean = 0.0;
        for (const double v0 : baselines) {
            baseline_mean += v0;
        }
        if (!baselines.empty()) {
            baseline_mean /= static_cast<double>(baselines.size());
        }

        std::cout << "\n=== model " << model_path << " ("
                  << net->input_count() << " inputs, baseline v0 mean "
                  << std::fixed << std::setprecision(3) << baseline_mean
                  << ") ===\n";
        std::cout << "  transform      expect        n     mean      "
                     "p25      p50      p75   >+.005   <-.005\n";
        json << "\n    {\"path\": \"" << model_path
             << "\", \"loaded\": true, \"inputs\": "
             << net->input_count()
             << ", \"baseline_mean\": " << baseline_mean
             << ", \"transforms\": [";
        for (std::size_t transform = 0; transform < kTransformCount;
             ++transform) {
            std::vector<double> deltas;
            deltas.reserve(records[transform].size());
            for (const DeltaRecord& record : records[transform]) {
                deltas.push_back(record.delta);
            }
            const Aggregate stats = aggregate_deltas(deltas);
            std::cout << "  " << std::left << std::setw(13)
                      << kTransformNames[transform] << std::setw(8)
                      << kExpectations[transform] << std::right
                      << std::setw(7) << stats.n << std::showpos
                      << std::fixed << std::setprecision(4)
                      << std::setw(9) << stats.mean << std::setw(9)
                      << stats.p25 << std::setw(9) << stats.p50
                      << std::setw(9) << stats.p75 << std::noshowpos
                      << std::setprecision(1) << std::setw(8)
                      << 100.0 * stats.share_above << "%"
                      << std::setw(8) << 100.0 * stats.share_below
                      << "%\n"
                      << std::setprecision(4);
            json << (transform ? "," : "") << "\n      {\"name\": \""
                 << kTransformNames[transform]
                 << "\", \"expectation\": \""
                 << kExpectations[transform] << "\", \"stats\": ";
            write_aggregate_json(json, stats);

            // Bias share = the wrong-direction tail for this transform.
            const auto bias_share = [&](const Aggregate& part) {
                return kBiasSign[transform] > 0 ? part.share_above
                                                : part.share_below;
            };

            json << ", \"by_deck\": [";
            std::cout << "      by deck:";
            for (std::size_t deck = 0; deck < spz::kSpzDeckCount;
                 ++deck) {
                std::vector<double> part;
                for (const DeltaRecord& record : records[transform]) {
                    if (record.deck == deck) {
                        part.push_back(record.delta);
                    }
                }
                const Aggregate stats_part = aggregate_deltas(part);
                json << (deck ? "," : "") << "{\"deck\": \""
                     << spz::spz_deck_name(deck) << "\", ";
                write_aggregate_fields(json, stats_part);
                json << ",\"bias_share\":" << bias_share(stats_part)
                     << "}";
                std::cout << "  " << spz::spz_deck_name(deck) << " "
                          << std::showpos << stats_part.mean
                          << std::noshowpos << "/"
                          << std::setprecision(0)
                          << 100.0 * bias_share(stats_part) << "%"
                          << std::setprecision(4);
            }
            json << "]";
            std::cout << "\n      by turn:";
            json << ", \"by_turn\": [";
            for (std::size_t bucket = 0; bucket < 3; ++bucket) {
                std::vector<double> part;
                for (const DeltaRecord& record : records[transform]) {
                    if (turn_bucket(record.turn) == bucket) {
                        part.push_back(record.delta);
                    }
                }
                const Aggregate stats_part = aggregate_deltas(part);
                json << (bucket ? "," : "") << "{\"turns\": \""
                     << kTurnBucketNames[bucket] << "\", ";
                write_aggregate_fields(json, stats_part);
                json << ",\"bias_share\":" << bias_share(stats_part)
                     << "}";
                std::cout << "  " << kTurnBucketNames[bucket] << " "
                          << std::showpos << stats_part.mean
                          << std::noshowpos << "/"
                          << std::setprecision(0)
                          << 100.0 * bias_share(stats_part) << "%"
                          << std::setprecision(4);
            }
            json << "]}";
            std::cout << "\n";
        }
        json << "\n    ]}";
    }
    json << "\n  ]\n}\n";

    std::filesystem::create_directories(
        std::filesystem::path(output_path).parent_path());
    std::ofstream out(output_path);
    if (!out) {
        std::cerr << "cannot write " << output_path << "\n";
        return 1;
    }
    out << json.str();
    std::cout << "\nwrote " << output_path << "\n";
    return 0;
}
