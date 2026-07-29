#include "old_school/decision_density_sparse_cross.hpp"

#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    namespace aq20 =
        old_school::decision_density_sparse_cross;
    std::vector<std::string_view> arguments;
    arguments.reserve(
        argc > 1
            ? static_cast<std::size_t>(argc - 1)
            : 0);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    if (!aq20::parse_command(arguments)) {
        aq20::print_usage(std::cerr);
        return 2;
    }
    try {
        const aq20::RunReport report =
            aq20::run_offline();
        aq20::print_report(std::cout, report);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "AQ20-DBC6 sparse-cross failed: "
                  << error.what() << '\n';
        return 1;
    }
}
