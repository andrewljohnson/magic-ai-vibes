#include "old_school/output_calibration_runner.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    return old_school::output_calibration::
        run_output_calibration_cli(
            argc, argv, std::cout, std::cerr);
}
