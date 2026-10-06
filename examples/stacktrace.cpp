
#include <iostream>
#include <sstream>
#include <boost/stacktrace.hpp>
#include <gsl/gsl>

void my_terminate_handler() {
    std::ostringstream oss;
    oss << boost::stacktrace::stacktrace();
    std::cerr << oss.str() << std::endl;
}

void foo() { Ensures(0); }

int main() {
    std::set_terminate(my_terminate_handler);
    foo();
    return 0;
}
