#include "mgsa/game.hpp"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char *what, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL line %d: %s\n", line, what);
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

mgsa::Game fresh() {
    mgsa::Game game;
    mgsa::init_game(game);
    mgsa::Action start{};
    start.kind = mgsa::Action::Kind::StartMission;
    CHECK(mgsa::mgsa_act(game, start));
    return game;
}

mgsa::Action play(int hand_index, int x, int y) {
    mgsa::Action action{};
    action.kind = mgsa::Action::Kind::PlayCard;
    action.a = hand_index;
    action.x = x;
    action.y = y;
    return action;
}

bool unchanged_after_reject(mgsa::Game &game, const mgsa::Action &action) {
    mgsa::Game before = game;
    const bool accepted = mgsa::mgsa_act(game, action);
    return !accepted && std::memcmp(&before, &game, sizeof(mgsa::Game)) == 0;
}

void test_setup() {
    mgsa::Game game;
    mgsa::init_game(game);
    CHECK(game.phase == mgsa::Phase::Title);
    CHECK(game.outcome == mgsa::Outcome::None);
    CHECK(mgsa::card_count() == 18);
    CHECK(std::strcmp(mgsa::card_info(0).name, "5.56mm") == 0);
    CHECK(std::strcmp(mgsa::card_info(6).name, "Hand-to-Hand") == 0);
    CHECK(mgsa::card_info(6).cost == 4);
}

void test_mission_start() {
    mgsa::Game game = fresh();
    CHECK(game.phase == mgsa::Phase::Board);
    CHECK(game.hand_count == 5);
    CHECK(game.cost_left == 10);
    CHECK(game.turn == 1);
    CHECK(game.guard_count == 3);
    CHECK(mgsa::living_guards(game) == 3);
    CHECK(game.cells[0][0] == mgsa::Cell::Floor);
    CHECK(game.cells[1][1] == mgsa::Cell::Wall);
    CHECK(game.cells[7][7] == mgsa::Cell::Exit);
}

void test_movement() {
    mgsa::Game game = fresh();
    CHECK(game.hand[4] == 4);  // Move 2, range 2
    CHECK(mgsa::mgsa_act(game, play(4, 2, 0)));
    CHECK(game.player_x == 2 && game.player_y == 0);
    CHECK(game.cost_left == 8);
    CHECK(game.cards_played == 1);
    CHECK(game.player_facing == mgsa::Direction::East);
    CHECK(unchanged_after_reject(game, play(99, 3, 1)));  // invalid slot
    CHECK(unchanged_after_reject(game, play(0, 1, 1)));  // wall
    CHECK(unchanged_after_reject(game, play(0, 7, 0)));  // out of range
}

void test_attack_and_guard_death() {
    mgsa::Game game = fresh();
    CHECK(mgsa::mgsa_act(game, play(4, 2, 0)));
    const int hp_before = game.guards[0].hp;
    CHECK(mgsa::mgsa_act(game, play(0, 3, 1)));  // 5.56mm, range 3, power 2
    CHECK(game.guards[0].hp == hp_before - 2);
    CHECK(game.cost_left == 5);

    game.guards[0].hp = 2;
    game.hand[1] = 1;  // 9mm, power 2
    CHECK(mgsa::mgsa_act(game, play(1, 3, 1)));
    CHECK(!mgsa::guard_alive(game.guards[0]));
    CHECK(mgsa::living_guards(game) == 2);
    CHECK(unchanged_after_reject(game, play(2, 3, 1)));  // empty cell
}

void test_hand_to_hand_and_stun() {
    mgsa::Game game = fresh();
    game.hand[0] = 6;  // Hand-to-Hand, cost 4, range 1, power 3
    game.hand[1] = 2;  // Stun Grenade
    game.player_x = 2;
    game.player_y = 1;
    CHECK(mgsa::mgsa_act(game, play(0, 3, 1)));
    CHECK(game.guards[0].hp == 1);
    CHECK(game.cost_left == 6);
    CHECK(mgsa::mgsa_act(game, play(1, 3, 1)));
    CHECK(game.guards[0].stunned);
}

void test_knock_and_redraw() {
    mgsa::Game game = fresh();
    CHECK(mgsa::mgsa_act(game, play(3, 0, 0)));  // Knock, cost 0
    CHECK(game.cost_left == 10);
    CHECK(game.guards[0].alert);

    game.hand[0] = 7;  // Redraw
    mgsa::Action redraw{};
    redraw.kind = mgsa::Action::Kind::Redraw;
    redraw.a = 0;
    redraw.b = 1;
    CHECK(mgsa::mgsa_act(game, redraw));
    CHECK(game.hand_count == 4);  // two cards replaced, hand size unchanged
    CHECK(game.cost_left == 4);
    CHECK(unchanged_after_reject(game, redraw));  // no Redraw card left in hand
}

void test_turn_cost_and_detection() {
    mgsa::Game game = fresh();
    mgsa::Action end{};
    end.kind = mgsa::Action::Kind::EndTurn;
    CHECK(mgsa::mgsa_act(game, end));
    CHECK(game.cost_left == 10);  // fresh turn
    CHECK(game.turn == 2);

    // No cards played: ending the turn costs 4 before the guards move.
    mgsa::Game tracked = fresh();
    const int start_x = tracked.guards[0].x;
    CHECK(mgsa::mgsa_act(tracked, end));
    CHECK(tracked.guards[0].x != start_x || tracked.guards[0].alert);
}

void test_win_and_lose() {
    mgsa::Game win = fresh();
    win.hand[0] = 5;  // Move 3
    win.player_x = 7;
    win.player_y = 6;
    CHECK(mgsa::mgsa_act(win, play(0, 7, 7)));
    CHECK(win.phase == mgsa::Phase::Result);
    CHECK(win.outcome == mgsa::Outcome::Win);

    mgsa::Game loss = fresh();
    loss.player_hp = 1;
    loss.guards[0].x = 1;
    loss.guards[0].y = 0;
    loss.guards[0].alert = true;
    loss.guards[0].facing = mgsa::Direction::West;
    mgsa::Action end{};
    end.kind = mgsa::Action::Kind::EndTurn;
    CHECK(mgsa::mgsa_act(loss, end));
    CHECK(loss.phase == mgsa::Phase::Result);
    CHECK(loss.outcome == mgsa::Outcome::Lose);
}

void test_restart() {
    mgsa::Game game = fresh();
    mgsa::Action restart{};
    restart.kind = mgsa::Action::Kind::Restart;
    CHECK(mgsa::mgsa_act(game, restart));
    CHECK(game.phase == mgsa::Phase::Title);
}

}  // namespace

int main() {
    test_setup();
    test_mission_start();
    test_movement();
    test_attack_and_guard_death();
    test_hand_to_hand_and_stun();
    test_knock_and_redraw();
    test_turn_cost_and_detection();
    test_win_and_lose();
    test_restart();
    std::printf("mgsa game tests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
