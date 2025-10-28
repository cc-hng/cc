
#include <boost/core/demangle.hpp>
#include <cc/util.h>
#include <stdio.h>

// template <typename T>
// std::string type_name() {
// return 
//     boost::core::demangle(typeid(Signature).name());
// }


int main() {
    printf("%s\n", cc::type_name<int, float>().c_str());
    return 0;
}
