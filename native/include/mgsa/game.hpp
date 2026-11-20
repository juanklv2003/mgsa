// Portable core of the native Metal Gear AC!D-style card game.
//
// No platform headers, no rendering, no randomness: the whole ruleset is reachable through
// mgsa_act(). Every action validates first and leaves the state untouched when rejected, so
// touch input can never produce a half-applied move.
#ifndef MGSA_GAME_HPP
#define MGSA_GAME_HPP

#include <cstdint>

namespace mgsa {

constexpr int kBoardSize = 8;
constexpr int kHandSize = 5;
constexpr int kDeckSize = 18;
constexpr int kMaxGuards = 3;

enum class Cell : std::uint8_t { Floor, Wall, Exit };
enum class Direction : std::uint8_t { North, East, South, West };
enum class CardType : std::uint8_t { Move, Attack, HandToHand, Knock, Redraw, Heal };
enum class Phase : std::uint8_t { Title, Briefing, Board, Result };
enum class Outcome : std::uint8_t { None, Win, Lose };

struct Card {
    const char *name;
    CardType type;
    std::uint8_t cost;
    std::uint8_t range;
    std::uint8_t power;
    bool stun = false;
};

struct Guard {
    int x;
    int y;
    int hp;
    Direction facing;
    bool alert;
    bool stunned;
    int patrol_ax;
    int patrol_ay;
    int patrol_bx;
    int patrol_by;
    int patrol_step;
};

struct Game {
    Phase phase;
    Outcome outcome;
    Cell cells[kBoardSize][kBoardSize];
    int player_x;
    int player_y;
    int player_hp;
    Direction player_facing;
    Guard guards[kMaxGuards];
    int guard_count;
    int cost_left;
    int turn;
    int cards_played;
    int hand[kHandSize];
    int hand_count;
    int deck[kDeckSize];
    int deck_top;
    int discard[kDeckSize];
    int discard_count;
};

struct Action {
    enum class Kind : std::uint8_t { Briefing, StartMission, PlayCard, EndTurn, Redraw, Restart };
    Kind kind;
    int a;   // hand index (PlayCard, Redraw first index)
    int b;   // Redraw second index
    int x;   // target cell
    int y;
};

// Fixed reference data, in deck order. Names come from the extracted game strings.
const Card &card_info(int card_id);
int card_count();

// Reset to the title screen.
void init_game(Game &game);

// Apply one action. Returns false on an illegal action and does not change `game`.
// mgsa_last_error() then describes the rejection for the UI.
bool mgsa_act(Game &game, const Action &action);
const char *mgsa_last_error();

// Read-only queries used by the renderer and the tests.
bool cell_walkable(const Game &game, int x, int y);
bool line_clear(const Game &game, int x0, int y0, int x1, int y1);
bool guard_alive(const Guard &guard);
int living_guards(const Game &game);

}  // namespace mgsa

#endif  // MGSA_GAME_HPP
