#include <iostream>
#include <cc/asio.hpp>
#include <cc/lit.hpp>
#include <cc/lit/client.h>
#include <gsl/gsl>

int main() {
    auto defer = gsl::finally([] { cc::lit::fetch_pool_cleanup(); });
    auto& ap   = cc::AsioPool::instance();

    ap.co_spawn([]() -> net::task<void> {
        auto resp = co_await cc::lit::http_get("http://127.0.0.1:8088");
        std::cout << resp.body() << std::endl;

        std::cout << "------------------- 分割线 -------------------" << std::endl;
        resp = co_await cc::lit::http_get("http://127.0.0.1:8088/api/a");
        std::cout << resp.body() << std::endl;

        std::cout << "------------------- 分割线 -------------------" << std::endl;
        resp = co_await cc::lit::http_get("http://127.0.0.1:8088/api/param?a=1&b=2");
        std::cout << resp.body() << std::endl;

        std::cout << "------------------- 分割线 -------------------" << std::endl;
        resp = co_await cc::lit::http_post("http://127.0.0.1:8088/api/c", "{}");
        std::cout << resp.body() << std::endl;

        std::cout << "------------------- 分割线 -------------------" << std::endl;
        std::vector<lit::multipart_formdata_t> form_data = {
            {"field1", "",            "text/plain", "This is the content of field1"     },
            {"file1",  "example.txt", "text/plain", "This is the content of example.txt"},
            {"field2", "",            "text/plain", "This is the content of field2"     }
        };
        resp = co_await lit::http_upload("http://127.0.0.1:8088/api/upload", form_data);
        std::cout << resp.body() << std::endl;
    });

    ap.run(1);
    return 0;
}
