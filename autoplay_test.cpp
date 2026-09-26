// End-to-end autoplay test for NEON SNAKE.
//
// Strategy:
//   1. Pre-include every header snake.cpp uses, BEFORE any macros, so the
//      standard library is compiled untouched.
//   2. Include the real game source with three renames:
//        main          -> game_main     (we drive the Game object ourselves)
//        _kbhit/_getch -> test_*        (scripted input replaces the keyboard)
//        private       -> public        (lets the auto-player see live state)
//      With the headers already included, the access override now only
//      applies to snake.cpp's own classes.
//
// Round 1 takes no steering input and dies on the wall (WALL IMPACT).
// Round 2 auto-steers toward the food, tests pause/resume once, and either
// dies naturally or withdraws at the frame cap. The full pipeline - title
// screen, rounds, destructor reports, leaderboard - runs for real.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <conio.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

int test_kbhit();
int test_getch();

#define private public
#define main game_main
#define _kbhit test_kbhit
#define _getch test_getch
#include "snake.cpp"
#undef _getch
#undef _kbhit
#undef main
#undef private

namespace {

neon::Game* g_game = nullptr;
std::deque<char> g_keys;
int g_prompts_seen = 0;
int g_rounds_started = 0;
int g_frames = 0;
bool g_in_round = false;
bool g_just_filled = false;
bool g_quit_pushed = false;

constexpr int PAUSE_FRAME = 40;
constexpr int FRAME_CAP = 250;

char key_for(neon::Direction d) {
    switch (d) {
        case neon::Direction::Up: return 'w';
        case neon::Direction::Down: return 's';
        case neon::Direction::Left: return 'a';
        case neon::Direction::Right: return 'd';
    }
    return 'd';
}

bool in_bounds(neon::Vec2 p) {
    return p.x >= 0 && p.x < neon::FIELD_W && p.y >= 0 && p.y < neon::FIELD_H;
}

// Greedy: step toward the food along its dominant axis, skipping any
// direction that would reverse, leave the board, or hit the body.
char steer_key() {
    const neon::Snake& s = *g_game->snake_;
    const neon::Vec2 head = s.head();
    const neon::Vec2 food = g_game->food_;
    const int dx = food.x - head.x;
    const int dy = food.y - head.y;

    neon::Direction order[2];
    int n = 0;
    if (dx != 0 && dx * dx >= dy * dy) order[n++] = dx > 0 ? neon::Direction::Right : neon::Direction::Left;
    if (dy != 0) order[n++] = dy > 0 ? neon::Direction::Down : neon::Direction::Up;
    if (dx != 0 && dx * dx < dy * dy) order[n++] = dx > 0 ? neon::Direction::Right : neon::Direction::Left;

    for (int i = 0; i < n; ++i) {
        const neon::Vec2 next = head + neon::to_delta(order[i]);
        if (!neon::is_opposite(order[i], s.heading()) && in_bounds(next) && !s.occupies(next)) {
            return key_for(order[i]);
        }
    }
    const neon::Direction all[4] = {neon::Direction::Up, neon::Direction::Down,
                                    neon::Direction::Left, neon::Direction::Right};
    for (const neon::Direction d : all) {
        if (!neon::is_opposite(d, s.heading())) return key_for(d);
    }
    return 'd';
}

}  // namespace

int test_kbhit() {
    if (!g_keys.empty()) return 1;
    if (g_just_filled) {  // one auto-generated key per frame, then let the pump end
        g_just_filled = false;
        return 0;
    }
    if (g_game == nullptr || g_game->snake_ == nullptr) return 0;
    if (!g_in_round) {
        g_in_round = true;
        ++g_rounds_started;
    }
    if (g_rounds_started == 1) return 0;  // round 1: ride straight into the wall
    ++g_frames;
    if (g_frames == PAUSE_FRAME) {
        g_keys.push_back('p');  // pause...
        g_keys.push_back('p');  // ...and resume
    } else if (g_frames >= FRAME_CAP) {
        if (g_quit_pushed) return 0;
        g_quit_pushed = true;
        g_keys.push_back('q');  // orderly withdrawal
    } else {
        g_keys.push_back(steer_key());
    }
    g_just_filled = true;
    return 1;
}

int test_getch() {
    if (!g_keys.empty()) {
        const int k = g_keys.front();
        g_keys.pop_front();
        return k;
    }
    if (g_game != nullptr && g_game->snake_ == nullptr) {  // title or play-again prompt
        g_in_round = false;  // a fresh round will re-arm the round counter
        ++g_prompts_seen;
        return g_prompts_seen <= 2 ? 'y' : 'n';  // play round 1 and 2, then exit
    }
    return 'p';  // default: resume if paused
}

int main() {
    neon::Game game;  // CONSTRUCTOR: console lock, renderer stack, RNG
    g_game = &game;
    game.run();       // two full rounds, then the leaderboard destructor fires
    g_game = nullptr;
    return 0;
}
