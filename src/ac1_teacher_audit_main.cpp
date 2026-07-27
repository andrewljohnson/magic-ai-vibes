#include "old_school/ac1_teacher_audit.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    return old_school::ac1_teacher_audit::run_cli(
        argc, argv, std::cout, std::cerr);
}
