#include "old_school/oc1_action_regression.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    return old_school::oc1_action_regression::run_cli(
        argc, argv, std::cout, std::cerr);
}
