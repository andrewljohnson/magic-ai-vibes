#include "old_school/fq4_dev_schedule.hpp"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    namespace schedule = old_school::fq4_dev_schedule;
    if (argc != 1) {
        const std::string program =
            argc > 0 && argv[0] != nullptr
                ? argv[0]
                : "old-school-fq4-priority-dev-schedule";
        std::cerr << "Usage: " << program << '\n';
        return 2;
    }
    try {
        for (const schedule::Split split :
             {schedule::Split::Fit,
              schedule::Split::Check}) {
            const auto games =
                schedule::source_schedule(split);
            const std::string bytes =
                schedule::serialize_source_schedule(
                    games);
            const auto balance =
                schedule::audit_schedule_balance(
                    games);
            const std::string sha256 =
                schedule::
                    source_schedule_sha256(split);
            if (!balance.exact ||
                bytes.size() !=
                    schedule::
                        expected_schedule_bytes(split) ||
                sha256 !=
                    schedule::
                        expected_schedule_sha256(split)) {
                throw std::runtime_error(
                    "frozen schedule contract failed");
            }
            std::cout
                << "split="
                << schedule::split_name(split)
                << " seed_base="
                << schedule::seed_base(split)
                << " games=" << games.size()
                << " perspectives="
                << balance.owner_perspectives
                << " bytes=" << bytes.size()
                << " sha256="
                << sha256
                << " exact=1\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "FQ4 development schedule failure: "
            << error.what() << '\n';
        return 2;
    }
}
