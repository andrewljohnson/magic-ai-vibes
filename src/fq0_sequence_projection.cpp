#include "old_school/fq0_sequence_projection.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_information_set.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace old_school::fq0_sequence_projection {
namespace {

inline constexpr std::string_view kInformationSetDomain =
    "old-school-fq0-graveyard-quotient-information-set-v1";
inline constexpr std::string_view kLeafConsequenceDomain =
    "old-school-fq0-graveyard-quotient-leaf-consequence-v1";
inline constexpr std::string_view kPriorityConsequenceDomain =
    "old-school-fq0-graveyard-quotient-priority-consequence-v1";

GameState graveyard_quotient_state(const GameState& state) {
    GameState quotient = state;
    for (PlayerState& player : quotient.players) {
        std::sort(
            player.graveyard.begin(),
            player.graveyard.end());
    }
    return quotient;
}

std::string domain_separated_digest(
    std::string_view domain,
    std::string_view legacy_sha256) {
    std::string payload;
    payload.reserve(
        domain.size() + 1U + legacy_sha256.size());
    payload.append(domain);
    payload.push_back('\0');
    payload.append(legacy_sha256);
    return artifact_integrity::sha256_string(payload);
}

} // namespace

std::string graveyard_quotient_information_set_sha256(
    const GameState& state,
    const LearnedDecisionContext& context,
    std::span<const PriorityAction> ordered_actions) {
    const GameState quotient =
        graveyard_quotient_state(state);
    const auto key =
        fq0_information_set::make_information_set_key(
            quotient, context, ordered_actions);
    return domain_separated_digest(
        kInformationSetDomain,
        fq0_information_set::information_set_sha256(key));
}

std::string graveyard_quotient_leaf_consequence_sha256(
    const GameState& state, std::size_t observer,
    const LearnedDecisionContext& context,
    const std::optional<GameResult>& terminal_result) {
    const GameState quotient =
        graveyard_quotient_state(state);
    return domain_separated_digest(
        kLeafConsequenceDomain,
        fq0_information_set::
            redacted_leaf_consequence_sha256(
                quotient, observer, context,
                terminal_result));
}

std::string graveyard_quotient_priority_consequence_sha256(
    const GameState& state, std::size_t observer,
    const LearnedDecisionContext& context,
    const PriorityAction& action) {
    const GameState quotient =
        graveyard_quotient_state(state);
    return domain_separated_digest(
        kPriorityConsequenceDomain,
        fq0_information_set::
            canonical_priority_consequence_sha256(
                quotient, observer, context, action));
}

} // namespace old_school::fq0_sequence_projection
