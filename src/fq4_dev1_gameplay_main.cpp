#include "old_school/fq4_dev1_gameplay.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    return old_school::fq4_dev1_gameplay::run_cli(
        argc, argv, std::cout, std::cerr);
}
