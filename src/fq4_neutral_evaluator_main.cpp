#include "old_school/fq4_neutral_evaluator_runner.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    return old_school::fq4_neutral_evaluator_runner::
        run_cli(argc, argv, std::cout, std::cerr);
}
