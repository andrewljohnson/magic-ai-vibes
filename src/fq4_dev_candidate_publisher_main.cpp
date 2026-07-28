#include "old_school/fq4_dev_candidate_publisher.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    return old_school::fq4_dev_candidate_publisher::
        run_cli(argc, argv, std::cout, std::cerr);
}
