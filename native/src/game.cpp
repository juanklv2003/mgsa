#include "mgsa/game.hpp"

#include <cstdio>
#include <cstring>

namespace mgsa {
namespace {

const Card kCards[kDeckSize] = {
    {"5.56mm",             CardType::Attack,     3, 3, 2},
    {"9mm",                CardType::Attack,     2, 2, 2},
    {"Stun Grenade",       CardType::Attack,     3, 2, 1, true},
    {"Knock on the wall",  CardType::Knock,      0, 0, 0},
    {"Move 2",             CardType::Move,       2, 2, 0},
    {"Move 3",             CardType::Move,       3, 3, 0},
    {"Hand-to-Hand",       CardType::HandToHand, 4, 1, 3},
    {"Redraw",             CardType::Redraw,     6, 0, 2},
    {"Ration",             CardType::Heal,       3, 0, 2},
    {"Quick Draw",         CardType::Move,       1, 2, 0},
    {"Preemptive Atk.",    CardType::Attack,     4, 3, 2},
    {"Double Action:Cost+",CardType::Move,       2, 2, 0},
    {"Tie-In Card",        CardType::Move,       2, 3, 0},
    {"Support Atk.",       CardType::Attack,     3, 4, 1},
    {"Cost -4",            CardType::Move,       0, 2, 0},
    {"Cost -8",            CardType::Move,       0, 3, 0},
    {"5.56mm REA:70",      CardType::Attack,     5, 4, 3},
    {"9mm",                CardType::Attack,     3, 3, 2},
};

char g_error[128] = "ok";

bool fail(const char *message) {
    std::snprintf(g_error, sizeof(g_error), "%s", message);
    return false;
}

bool in_bounds(int x, int y) {
    return x >= 0 && y >= 0 && x < kBoardSize && y < kBoardSize;
}

int manhattan(int x0, int y0, int x1, int y1) {
    const int dx = x0 > x1 ? x0 - x1 : x1 - x0;
    const int dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return dx + dy;
}

int guard_at(const Game &game, int x, int y) {
    for (int i = 0; i < game.guard_count; ++i) {
        if (guard_alive(game.guards[i]) && game.guards[i].x == x && game.guards[i].y == y)
            return i;
    }
    return -1;
}

void draw_to(Game &game, int count) {
    for (int i = 0; i < count && game.hand_count < kHandSize; ++i) {
        if (game.deck_top >= kDeckSize) {
            // Deterministic recycle: discard order becomes the new deck order.
            if (game.discard_count == 0) return;
            for (int j = 0; j < game.discard_count; ++j) game.deck[j] = game.discard[j];
            game.deck_top = 0;
            game.discard_count = 0;
        }
        game.hand[game.hand_count++] = game.deck[game.deck_top++];
    }
}

void remove_from_hand(Game &game, int index) {
    game.discard[game.discard_count++] = game.hand[index];
    for (int i = index; i + 1 < game.hand_count; ++i) game.hand[i] = game.hand[i + 1];
    --game.hand_count;
}

void check_outcome(Game &game) {
    if (game.player_hp <= 0) {
        game.player_hp = 0;
        game.outcome = Outcome::Lose;
        game.phase = Phase::Result;
    } else if (living_guards(game) == 0 ||
               (in_bounds(game.player_x, game.player_y) &&
                game.cells[game.player_y][game.player_x] == Cell::Exit)) {
        game.outcome = Outcome::Win;
        game.phase = Phase::Result;
    }
}

bool detects(const Game &game, const Guard &guard) {
    int dx = 0, dy = 0;
    switch (guard.facing) {
        case Direction::North: dy = -1; break;
        case Direction::East:  dx = 1;  break;
        case Direction::South: dy = 1;  break;
        case Direction::West:  dx = -1; break;
    }
    // Forward cone, three cells deep, plus the two adjacent cells one step ahead.
    for (int step = 1; step <= 3; ++step) {
        const int x = guard.x + dx * step;
        const int y = guard.y + dy * step;
        if (!in_bounds(x, y) || game.cells[y][x] == Cell::Wall) break;
        if (x == game.player_x && y == game.player_y) return true;
    }
    const int ax = guard.x + dx + (dy != 0 ? 1 : 0);
    const int ay = guard.y + dy + (dx != 0 ? 1 : 0);
    const int bx = guard.x + dx - (dy != 0 ? 1 : 0);
    const int by = guard.y + dy - (dx != 0 ? 1 : 0);
    if (in_bounds(ax, ay) && ax == game.player_x && ay == game.player_y &&
        line_clear(game, guard.x, guard.y, ax, ay)) return true;
    if (in_bounds(bx, by) && bx == game.player_x && by == game.player_y &&
        line_clear(game, guard.x, guard.y, bx, by)) return true;
    return false;
}

void step_toward(Game &game, Guard &guard, int tx, int ty) {
    const int dx = tx - guard.x;
    const int dy = ty - guard.y;
    const bool horizontal = (dx > 0 ? dx : -dx) >= (dy > 0 ? dy : -dy);
    const int nx = guard.x + (horizontal ? (dx > 0 ? 1 : -1) : 0);
    const int ny = guard.y + (horizontal ? 0 : (dy > 0 ? 1 : -1));
    if (!cell_walkable(game, nx, ny) || guard_at(game, nx, ny) >= 0 ||
        (nx == game.player_x && ny == game.player_y)) return;
    guard.x = nx;
    guard.y = ny;
}

void patrol_step(Game &game, Guard &guard) {
    const bool to_a = guard.patrol_step == 0;
    const int tx = to_a ? guard.patrol_ax : guard.patrol_bx;
    const int ty = to_a ? guard.patrol_ay : guard.patrol_by;
    if (guard.x == tx && guard.y == ty) {
        guard.patrol_step = to_a ? 1 : 0;
        return;
    }
    step_toward(game, guard, tx, ty);
}

void guards_turn(Game &game) {
    for (int i = 0; i < game.guard_count; ++i) {
        Guard &guard = game.guards[i];
        if (!guard_alive(guard)) continue;
        if (guard.stunned) {
            guard.stunned = false;
            continue;
        }
        if (detects(game, guard)) guard.alert = true;
        if (guard.alert) {
            const int distance = manhattan(guard.x, guard.y, game.player_x, game.player_y);
            if (distance <= 1 && line_clear(game, guard.x, guard.y, game.player_x, game.player_y)) {
                --game.player_hp;
            } else {
                step_toward(game, guard, game.player_x, game.player_y);
            }
        } else {
            patrol_step(game, guard);
        }
    }
    check_outcome(game);
}

bool play_card(Game &game, int hand_index, int x, int y) {
    if (hand_index < 0 || hand_index >= game.hand_count) return fail("No card in that slot.");
    const Card &card = card_info(game.hand[hand_index]);
    if (card.cost > game.cost_left) return fail("Not enough COST.");
    if (card.type == CardType::Redraw) return fail("Use the redraw action for this card.");

    if (card.type == CardType::Move) {
        if (!in_bounds(x, y)) return fail("Pick a cell on the board.");
        if (!cell_walkable(game, x, y)) return fail("That cell is blocked.");
        if (x != game.player_x && y != game.player_y) return fail("Move along a row or column.");
        const int distance = manhattan(game.player_x, game.player_y, x, y);
        if (distance == 0 || distance > card.range) return fail("Out of movement range.");
        if (!line_clear(game, game.player_x, game.player_y, x, y)) return fail("A wall blocks the path.");
        if (guard_at(game, x, y) >= 0) return fail("A guard occupies that cell.");
    } else if (card.type == CardType::Attack || card.type == CardType::HandToHand) {
        if (!in_bounds(x, y)) return fail("Pick a guard on the board.");
        const int target = guard_at(game, x, y);
        if (target < 0) return fail("There is no guard there.");
        if (manhattan(game.player_x, game.player_y, x, y) > card.range)
            return fail("Target is out of range.");
        if (!line_clear(game, game.player_x, game.player_y, x, y))
            return fail("A wall blocks the shot.");
    } else if (card.type == CardType::Knock) {
        for (int i = 0; i < game.guard_count; ++i) {
            if (guard_alive(game.guards[i]) &&
                manhattan(game.guards[i].x, game.guards[i].y, game.player_x, game.player_y) <= 4)
                game.guards[i].alert = true;
        }
    }

    const int id = game.hand[hand_index];
    const Card resolved = card_info(id);
    if (resolved.type == CardType::Attack || resolved.type == CardType::HandToHand) {
        const int target = guard_at(game, x, y);
        game.guards[target].hp -= resolved.power;
        if (resolved.stun) game.guards[target].stunned = true;
        if (!guard_alive(game.guards[target])) game.guards[target].alert = false;
    } else if (resolved.type == CardType::Heal) {
        if (game.player_hp >= 8) return fail("Health is already full.");
        game.player_hp = game.player_hp + resolved.power > 8 ? 8 : game.player_hp + resolved.power;
    } else if (resolved.type == CardType::Move) {
        const int dx = x - game.player_x;
        const int dy = y - game.player_y;
        if (dx > 0) game.player_facing = Direction::East;
        else if (dx < 0) game.player_facing = Direction::West;
        else if (dy > 0) game.player_facing = Direction::South;
        else game.player_facing = Direction::North;
        game.player_x = x;
        game.player_y = y;
    }

    remove_from_hand(game, hand_index);
    game.cost_left -= resolved.cost;
    ++game.cards_played;
    check_outcome(game);
    return true;
}

bool start_mission(Game &game) {
    static const char layout[kBoardSize][kBoardSize + 1] = {
        "........",
        ".#....#.",
        ".#....#.",
        "........",
        "........",
        ".#....#.",
        ".#....#.",
        ".......X",
    };
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            game.cells[y][x] = layout[y][x] == '#' ? Cell::Wall
                             : layout[y][x] == 'X' ? Cell::Exit : Cell::Floor;
        }
    }
    game.player_x = 0;
    game.player_y = 0;
    game.player_hp = 8;
    game.player_facing = Direction::East;

    game.guard_count = 3;
    game.guards[0] = {3, 1, 4, Direction::South, false, false, 2, 1, 5, 1, 0};
    game.guards[1] = {4, 4, 5, Direction::West,  false, false, 4, 3, 4, 6, 0};
    game.guards[2] = {1, 7, 4, Direction::North, false, false, 1, 7, 5, 7, 0};

    for (int i = 0; i < kDeckSize; ++i) game.deck[i] = i;
    game.deck_top = 0;
    game.discard_count = 0;
    game.hand_count = 0;
    draw_to(game, kHandSize);
    game.cost_left = 10;
    game.turn = 1;
    game.cards_played = 0;
    game.outcome = Outcome::None;
    game.phase = Phase::Board;
    return true;
}

bool redraw(Game &game, int first, int second) {
    if (first < 0 || second < 0 || first >= game.hand_count || second >= game.hand_count)
        return fail("Pick two cards in your hand.");
    if (first == second) return fail("Pick two different cards.");
    if (card_info(game.hand[first]).type != CardType::Redraw &&
        card_info(game.hand[second]).type != CardType::Redraw)
        return fail("Select a Redraw card.");
    if (game.cost_left < 6) return fail("Not enough COST.");
    remove_from_hand(game, second);
    remove_from_hand(game, first);
    game.cost_left -= 6;
    ++game.cards_played;
    draw_to(game, 2);
    return true;
}

}  // namespace

const Card &card_info(int card_id) { return kCards[card_id]; }
int card_count() { return kDeckSize; }
const char *mgsa_last_error() { return g_error; }

bool guard_alive(const Guard &guard) { return guard.hp > 0; }

int living_guards(const Game &game) {
    int count = 0;
    for (int i = 0; i < game.guard_count; ++i)
        if (guard_alive(game.guards[i])) ++count;
    return count;
}

bool cell_walkable(const Game &game, int x, int y) {
    return in_bounds(x, y) && game.cells[y][x] != Cell::Wall;
}

bool line_clear(const Game &game, int x0, int y0, int x1, int y1) {
    // Integer Bresenham over cell centres. Endpoints are excluded: the wall check is about what
    // lies between the shooter and the target, and the target cell itself is already validated.
    int x = x0;
    int y = y0;
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx - dy;
    while (x != x1 || y != y1) {
        const int twice = 2 * error;
        if (twice > -dy) { error -= dy; x += sx; }
        if (twice < dx) { error += dx; y += sy; }
        if (!in_bounds(x, y)) return false;
        if ((x != x1 || y != y1) && !cell_walkable(game, x, y)) return false;
    }
    return true;
}

void init_game(Game &game) {
    std::memset(&game, 0, sizeof(game));
    game.phase = Phase::Title;
    game.outcome = Outcome::None;
    game.player_hp = 8;
    game.player_facing = Direction::East;
}

bool mgsa_act(Game &game, const Action &action) {
    // Validate on a copy so a rejected action cannot leave a partially applied state.
    Game trial = game;
    bool ok = false;
    switch (action.kind) {
        case Action::Kind::Briefing:
            ok = trial.phase == Phase::Title ? (trial.phase = Phase::Briefing, true)
                                             : fail("Already in a mission.");
            break;
        case Action::Kind::StartMission:
            ok = start_mission(trial);
            break;
        case Action::Kind::PlayCard:
            ok = play_card(trial, action.a, action.x, action.y);
            break;
        case Action::Kind::EndTurn:
            if (trial.phase != Phase::Board) ok = fail("No mission in progress.");
            else {
                if (trial.cards_played == 0) {
                    trial.cost_left = trial.cost_left >= 4 ? trial.cost_left - 4 : 0;
                }
                guards_turn(trial);
                if (trial.outcome == Outcome::None) {
                    ++trial.turn;
                    trial.cost_left = 10;
                    trial.cards_played = 0;
                    draw_to(trial, kHandSize - trial.hand_count);
                }
                ok = true;
            }
            break;
        case Action::Kind::Redraw:
            ok = redraw(trial, action.a, action.b);
            break;
        case Action::Kind::Restart:
            init_game(trial);
            ok = true;
            break;
    }
    if (ok) game = trial;
    return ok;
}

}  // namespace mgsa
