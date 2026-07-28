#include "old_school/fq4_dev_background_diagnostic.hpp"

#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    const bool parent_control =
        argc == 2 &&
        std::string_view(argv[1]) ==
            "--parent-control";
    if (argc != 1 && !parent_control) {
        std::cerr
            << "Usage: old-school-fq4-dev2-background-diagnostic "
               "[--parent-control]\n";
        return 2;
    }
    try {
        if (parent_control) {
            const auto report =
                old_school::
                    fq4_dev_background_diagnostic::
                        run_parent_control();
            std::cout
                << old_school::
                       fq4_dev_background_diagnostic::
                           format_parent_control_report(
                               report);
            std::cout.flush();
            return std::cout.good() ? 0 : 2;
        }
        const auto report =
            old_school::fq4_dev_background_diagnostic::
                run_fixed();
        std::cout
            << old_school::
                   fq4_dev_background_diagnostic::
                       format_report(report);
        std::cout.flush();
        return std::cout.good() ? 0 : 2;
    } catch (const std::exception&) {
        std::cerr
            << "result=ERROR"
               " reason=fixed_background_diagnostic_failed\n";
        return 2;
    }
}
