#include "old_school/fq4_dev_coverage_census.hpp"

#include <exception>
#include <iostream>

int main(int argc, char**) {
    if (argc != 1) {
        std::cerr
            << "Usage: old-school-fq4-dev4-coverage-census\n";
        return 2;
    }
    try {
        const auto report =
            old_school::fq4_dev_coverage_census::
                run_fixed();
        std::cout
            << old_school::
                   fq4_dev_coverage_census::
                       format_report(report);
        std::cout.flush();
        return std::cout.good() ? 0 : 2;
    } catch (const std::exception&) {
        std::cerr
            << "result=ERROR"
               " reason=fixed_coverage_census_failed\n";
        return 2;
    }
}
