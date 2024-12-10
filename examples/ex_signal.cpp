#include "log.h"
#include <cc/asio/pool.h>
#include <cc/signal.h>
#include <cc/value.h>
#include <fmt/ranges.h>

static cc::ConcurrentSignal kSig;

using Point3D = std::vector<int>;

struct Player {
    Player(std::string_view name) : name_(name) {}
    ~Player() {}

    void on_pos(Point3D pos) { LOGI("[player {}] pos {}", name_, pos); }

    void on_vel(int x, int y) {
        x_ = x;
        y_ = y;
        LOGI("[player {}] speed [{}, {}]", name_, x, y);
    }
    void on_game_over() { LOGI("[player {}] game over", name_); }

    std::string name_;
    int x_, y_;
};

void on_game_over1() {
    LOGI("[module 1] game over");
}
void on_game_over2() {
    LOGI("[module 2] game over");
}
void on_vel(int x, int y) {
    LOGI("[unknown] speed x: {}, y: {}", x, y);
    if (x < 0) {
        kSig.pub("game_over");
    }
}

void on_pos(Point3D pos) {
    LOGI("[unknown] pos: {}", pos);
}

asio::awaitable<void> async_background_task() {
    auto [_, stream] = kSig.stream<int, int>("vel");
    for (;;) {
        auto items = co_await (*stream)();
        if (!items.size()) {
            break;
        }

        for (auto [x, y] : items) {
            LOGI("[background] speed x: {}, y: {}", x, y);
        }
    }
}

void on_val1(var_t v) {
    LOGI("on_val1: {}", v);
}

void on_val2(const var_t& v) {
    LOGI("on_val2: {}", v);
}

int main() {
    init_logger();

    auto& signal = kSig;
    signal.sub("vel", 1, 1, on_vel);
    signal.sub("game_over", on_game_over1);
    signal.sub("game_over", on_game_over2);
    signal.sub("pos", on_pos);

    signal.sub("/on_val", on_val1);
    signal.sub("/on_val", on_val2);

    // auto& ctx = cc::AsioPool::instance().get_io_context();
    // asio::co_spawn(ctx, async_background_task(), asio::detached);

    // player by shared_ptr
    {
        auto player = std::make_shared<Player>("shared_ptr");
        signal.sub("vel", 0.5, &Player::on_vel, player);
        signal.sub("game_over", &Player::on_game_over, player);
        signal.sub("pos", &Player::on_pos, player);
    }

    // player on stack
    // lifetime error
    // if (1) {
    //     Player player("stack");
    //     signal.register_handler("vel", &Player::on_vel, &player);
    //     signal.register_handler("game_over", &Player::on_game_over, &player);
    //     signal.emit("vel", -20, 20);
    // }

    signal.pub("vel", 12, 12);
    signal.pub("vel", 10, 10);
    signal.pub("pos", Point3D{1, 2, 3});
    Point3D pt = {3, 4, 5};
    signal.pub("pos", pt);
    signal.pub("game_over");

    cc::AsioPool::instance().set_interval(1000, [](auto) { signal.pub("/on_val", var_t(1)); });
    cc::AsioPool::instance().set_interval(10, [](auto) { signal.pub("vel", 12, 12); });
    // cc::AsioPool::instance().set_timeout(1000, [] { signal.pub("vel", 12, 12); });

    cc::AsioPool::instance().set_timeout(3000, [] {
        // fatal: type dismatch
        signal.pub("vel", 'a', true);
    });
    cc::AsioPool::instance().run(1);

    return 0;
}
