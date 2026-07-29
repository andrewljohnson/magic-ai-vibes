#include "old_school/decision_density_bilinear.hpp"

#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    namespace aq19 =
        old_school::decision_density_bilinear;
    std::vector<std::string_view> arguments;
    arguments.reserve(
        argc > 1
            ? static_cast<std::size_t>(argc - 1)
            : 0);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    const auto command = aq19::parse_command(arguments);
    if (!command) {
        aq19::print_usage(std::cerr);
        return 2;
    }
    try {
        const aq19::RunReport report =
            *command == aq19::Command::Run
                ? aq19::run()
                : aq19::run_offline();
        aq19::print_report(std::cout, report);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "AQ19-DBC6 failed: "
                  << error.what() << '\n';
        return 1;
    }
}
