#include <string>
#include <string_view>
#include <boost/url.hpp>
#include <cc/lit/client.h>

int main() {
    std::string s =
        "https://user:pass@example.com:443/path/to/"
        "my%2dfile.txt?id=42&name=John%20Doe+Jingleheimer%2DSchmidt#page%20anchor";

    boost::system::result<boost::urls::url_view> r = boost::urls::parse_uri(s);
    return 0;
}
