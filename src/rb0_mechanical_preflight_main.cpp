#include "old_school/rb0_mechanical_preflight.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace mechanical =
    old_school::rb0_mechanical_preflight;

int main(int argc, char* argv[]) {
    if (argc != 1) {
        bool named_quarantined_seed = false;
        bool named_reserved_seed = false;
        const std::string quarantined =
            std::to_string(
                old_school::replay_weight_audit::
                    kQuarantinedAuditSeed);
        const std::string reserved =
            std::to_string(
                old_school::replay_weight_audit::kAuditSeed);
        for (int index = 1; index < argc; ++index) {
            named_quarantined_seed =
                named_quarantined_seed ||
                std::string_view(argv[index]).find(
                    quarantined) != std::string_view::npos;
            named_reserved_seed =
                named_reserved_seed ||
                std::string_view(argv[index]).find(
                    reserved) != std::string_view::npos;
        }
        if (named_quarantined_seed) {
            std::cerr
                << "RB0-E1 rejects quarantined RB0-0 seed "
                << quarantined << '\n';
        } else if (named_reserved_seed) {
            std::cerr
                << "RB0-E1 rejects reserved RB0-0 seed "
                << reserved << '\n';
        } else {
            std::cerr
                << "RB0-E1 is argument-free and accepts no "
                   "command-line options\n";
        }
        return 2;
    }

    try {
        const mechanical::Report report =
            mechanical::run(std::cout);
        mechanical::write_report(report, std::cout);
        return mechanical::exit_code(
            mechanical::mechanically_clean(report));
    } catch (const std::exception& error) {
        std::cerr
            << "RB0-E1 mechanical preflight infrastructure "
               "failure: "
            << error.what() << '\n';
        return 2;
    }
}
