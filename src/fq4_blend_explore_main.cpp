#include "old_school/fq4_blend_explore.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    return old_school::fq4_blend_explore::run_cli(
        argc, argv, std::cout, std::cerr);
}
