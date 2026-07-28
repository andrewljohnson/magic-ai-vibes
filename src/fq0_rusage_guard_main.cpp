#include "old_school/fq0_rusage_guard.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    return old_school::fq0_rusage_guard::run_cli(
        argc, argv, std::cout, std::cerr);
}
