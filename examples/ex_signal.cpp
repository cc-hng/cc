#include <cc/asio/pool.h>
#include <cc/signal.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

static cc::ConcurrentSignal kSig;

using Point3D = std::vector<int>;

struct Player {
    Player(std::string_view name) : name_(name) {}
    ~Player() {}

    void on_pos(Point3D pos) { fmt::print("[player {}] pos {}\n", name_, pos); }

    void on_vel(int x, int y) {
        x_ = x;
        y_ = y;
        fmt::print("[player {}] speed [{}, {}]\n", name_, x, y);
    }
    void on_game_over() { fmt::print("[player {}] game over\n", name_); }

    std::string name_;
    int x_, y_;
};

void on_game_over1() {
    fmt::print("[module 1] game over\n");
}
void on_game_over2() {
    fmt::print("[module 2] game over\n");
}
void on_vel(int x, int y) {
    fmt::print("[unknown] speed x: {}, y: {}\n", x, y);
    if (x < 0) {
        kSig.emit("game_over");
    }
}

void on_pos(Point3D pos) {
    fmt::print("[unknown] pos: {}\n", pos);
}

int main() {
    auto& signal = kSig;
    signal.register_handler("vel", on_vel);
    signal.register_handler("game_over", on_game_over1);
    signal.register_handler("game_over", on_game_over2);
    signal.register_handler("pos", on_pos);

    // player by shared_ptr
    {
        auto player = std::make_shared<Player>("shared_ptr");
        signal.register_handler("vel", &Player::on_vel, player);
        signal.register_handler("game_over", &Player::on_game_over, player);
        signal.register_handler("pos", &Player::on_pos, player);
    }

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
    signal.emit("pos", Point3D{1, 2, 3});
    Point3D pt = {3, 4, 5};
    signal.emit("pos", pt);
    signal.emit("game_over");
    return 0;
}
