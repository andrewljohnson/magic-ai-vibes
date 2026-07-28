#include "old_school/fq4_neutral_publisher.hpp"

#include <iostream>
#include <string_view>

#ifndef OLD_SCHOOL_FQ4_DEV5_PRODUCER_COMMIT
#error "OLD_SCHOOL_FQ4_DEV5_PRODUCER_COMMIT must be defined"
#endif

int main(int argc, char* argv[]) {
    return old_school::fq4_neutral_publisher::run_cli(
        argc, argv, std::cout, std::cerr,
        std::string_view(
            OLD_SCHOOL_FQ4_DEV5_PRODUCER_COMMIT));
}
