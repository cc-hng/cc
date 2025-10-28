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
        kSig.emit("game_over");
    }
}

void on_pos(Point3D pos) {
    LOGI("[unknown] pos: {}", pos);
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
    // signal.connect("vel", 1, 1, on_vel);
    signal.connect("game_over", on_game_over2);
    signal.connect("game_over", on_game_over1);
    signal.connect("pos", on_pos);
    signal.connect("vel", on_vel);

    signal.connect("/on_val", on_val2);
    signal.connect("/on_val", on_val2);

    // auto& ctx = cc::AsioPool::instance().get_io_context();
    // net::co_spawn(ctx, async_background_task(), net::detached);

    // player by shared_ptr
    {
        auto player = std::make_shared<Player>("shared_ptr");
        signal.connect("vel", &Player::on_vel, player);
        signal.connect("game_over", &Player::on_game_over, player);
        // signal.connect("pos", &Player::on_pos, player);
    }

    // auto topicinfo = signal.list();
    // fmt::print("topic: {}\n", topicinfo);

    // player on stack
    // lifetime error
    // if (1) {
    //     Player player("stack");
    //     signal.register_handler("vel", &Player::on_vel, &player);
    //     signal.register_handler("game_over", &Player::on_game_over, &player);
    //     signal.emit("vel", -20, 20);
    // }


    signal.emit("vel", 12, 12);
    signal.emit("vel", 10, 10);
    // signal.emit("pos", Point3D{1, 2, 3});
    // Point3D pt = {3, 4, 5};
    // signal.emit("pos", pt);
    signal.emit("game_over");

    var_arr_t arr;
    arr.emplace_back(100);
    arr.emplace_back(100);
    signal.emit_any("vel", var_t(arr));

    cc::AsioPool::instance().set_interval(1000, [&](auto) { signal.emit("/on_val", var_t(1)); });
    cc::AsioPool::instance().set_interval(100, [&](auto) { signal.emit("vel", 12, 12); });
    // cc::AsioPool::instance().set_timeout(1000, [] { signal.emit("vel", 12, 12); });

    cc::AsioPool::instance().set_timeout(3000, [&] {
        // fatal: type dismatch
        signal.emit("vel", 'a', true);
    });
    cc::AsioPool::instance().run(1);

    return 0;
}
