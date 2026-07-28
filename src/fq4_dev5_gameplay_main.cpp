#include "old_school/fq4_dev5_gameplay.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    return old_school::fq4_dev5_gameplay::run_cli(
        argc, argv, std::cout, std::cerr);
}
