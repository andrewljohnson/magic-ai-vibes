#include "old_school/fq0_bellman_audit.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    return old_school::fq0_bellman_audit::run_cli(
        argc, argv, std::cout, std::cerr);
}
