#include "old_school/dvr2_harvest.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    if (argc != 3 ||
        std::string_view(argv[1]) != "--output" ||
        std::string_view(argv[2]).empty()) {
        std::cerr
            << "usage: old-school-dvr2-harvest --output PATH\n"
            << "DVR2 accepts only its new evidence output path; "
               "all model, source, search, budget, thread, and "
               "watchdog settings are fixed.\n";
        return 2;
    }

    try {
        const auto result =
            old_school::dvr2_harvest::run(
                std::filesystem::path(argv[2]),
                std::cout);
        return result.exit_code();
    } catch (const std::exception& error) {
        std::cerr
            << "DVR2 infrastructure failure: "
            << error.what() << '\n';
        return 2;
    }
}
