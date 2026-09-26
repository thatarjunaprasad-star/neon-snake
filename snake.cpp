// ============================================================================
//  NEON SNAKE - a console Snake game in modern C++
//
//  The game's momentum is carried by four classic constructs:
//    CONSTRUCTOR - every subsystem self-initialises (C++'s answer to __init__)
//    DECORATOR   - the frame is rendered by a wrapping renderer stack:
//                  Level( HUD( Border( Field ) ) )
//    COMPARATOR  - Vec2::operator< orders cells inside a std::set for fast
//                  collision checks; RunResultComparator ranks the
//                  session leaderboard
//    DESTRUCTOR  - when the snake is annihilated, its destructor prints the
//                  run report and the LEVEL MARKINGS
//
//  Build:  g++ -std=c++17 -O2 -Wall -Wextra snake.cpp -o snake.exe
//          cl /std:c++17 /EHsc snake.cpp
//  Play:   WASD or arrow keys, [P] pause, [Q] quit
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

namespace neon {

// ---------------------------------------------------------------- geometry --
struct Vec2 {
    int x{};
    int y{};

    constexpr Vec2() = default;
    constexpr Vec2(int px, int py) noexcept : x{px}, y{py} {}

    [[nodiscard]] constexpr Vec2 operator+(const Vec2& o) const noexcept { return {x + o.x, y + o.y}; }
    [[nodiscard]] constexpr bool operator==(const Vec2& o) const noexcept { return x == o.x && y == o.y; }
    // COMPARATOR: lexicographic order - this is what lets Vec2 be a std::set key
    [[nodiscard]] constexpr bool operator<(const Vec2& o) const noexcept {
        return x != o.x ? x < o.x : y < o.y;
    }
};

enum class Direction { Up, Down, Left, Right };

[[nodiscard]] constexpr Vec2 to_delta(Direction d) noexcept {
    switch (d) {
        case Direction::Up:    return {0, -1};
        case Direction::Down:  return {0, 1};
        case Direction::Left:  return {-1, 0};
        case Direction::Right: return {1, 0};
    }
    return {0, 0};
}

[[nodiscard]] constexpr bool is_opposite(Direction a, Direction b) noexcept {
    const Vec2 da = to_delta(a);
    const Vec2 db = to_delta(b);
    return da.x + db.x == 0 && da.y + db.y == 0;
}

enum class DeathCause { WallImpact, SelfCollision, Withdrawn, Transcended };

[[nodiscard]] constexpr std::string_view to_text(DeathCause c) noexcept {
    switch (c) {
        case DeathCause::WallImpact:    return "WALL IMPACT";
        case DeathCause::SelfCollision: return "SELF COLLISION";
        case DeathCause::Withdrawn:     return "ORDERLY WITHDRAWAL";
        case DeathCause::Transcended:   return "BOARD TRANSCENDED";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view level_mark(int level) noexcept {
    if (level <= 1) return "HATCHLING";
    if (level == 2) return "CRAWLER";
    if (level == 3) return "HUNTER";
    if (level == 4) return "PREDATOR";
    if (level == 5) return "APEX";
    return "LEGENDARY";
}

// ------------------------------------------------------------------ layout --
constexpr int FIELD_W = 32;                 // playfield cells across
constexpr int FIELD_H = 18;                 // playfield cells down
constexpr Vec2 FIELD_ORIGIN{6, 3};          // screen position of field cell (0,0)
constexpr int FOODS_PER_LEVEL = 5;
constexpr int MSG_ROW = FIELD_ORIGIN.y + FIELD_H + 2;
constexpr int BANNER_ROW = MSG_ROW + 1;
constexpr int BANNER_LINES = 11;
constexpr int PROMPT_ROW = BANNER_ROW + BANNER_LINES;
constexpr int SCREEN_W = 46;
constexpr int SCREEN_H = PROMPT_ROW + 2;

// ----------------------------------------------------------------- palette --
namespace color {
constexpr WORD border = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD body   = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD head   = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD food   = FOREGROUND_RED | FOREGROUND_INTENSITY;
constexpr WORD hud    = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
constexpr WORD level  = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
constexpr WORD hint   = FOREGROUND_GREEN | FOREGROUND_BLUE;
constexpr WORD death  = FOREGROUND_RED | FOREGROUND_INTENSITY;
constexpr WORD dim    = FOREGROUND_INTENSITY;
}  // namespace color

// ----------------------------------------------------------------- console --
// RAII wrapper around the Win32 console.
class Console {
public:
    // CONSTRUCTOR - the "__init__" of the terminal: hide the caret and lock
    // the frame size so the game owns the whole screen.
    explicit Console(int width, int height)
        : width_{width}, height_{height}, handle_{GetStdHandle(STD_OUTPUT_HANDLE)} {
        CONSOLE_CURSOR_INFO info{};
        GetConsoleCursorInfo(handle_, &info);
        saved_cursor_ = info;
        info.bVisible = FALSE;
        SetConsoleCursorInfo(handle_, &info);

        const SMALL_RECT window{0, 0, static_cast<SHORT>(width - 1), static_cast<SHORT>(height - 1)};
        SetConsoleWindowInfo(handle_, TRUE, &window);
        SetConsoleScreenBufferSize(handle_, COORD{static_cast<SHORT>(width), static_cast<SHORT>(height)});
        clear();
    }

    // DESTRUCTOR - undo everything the constructor changed.
    ~Console() {
        place(0, height_ - 2);
        attr(7);
        saved_cursor_.bVisible = TRUE;
        SetConsoleCursorInfo(handle_, &saved_cursor_);
    }

    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    void place(int x, int y) const {
        SetConsoleCursorPosition(handle_, COORD{static_cast<SHORT>(x), static_cast<SHORT>(y)});
    }
    void attr(WORD a) const { SetConsoleTextAttribute(handle_, a); }
    void write(int x, int y, std::string_view text, WORD a) const {
        place(x, y);
        attr(a);
        std::cout << text;
    }
    void clear() const {
        const std::string blank(width_, ' ');
        for (int y = 0; y < height_; ++y) write(0, y, blank, 7);
        place(0, 0);
    }

private:
    int width_;
    int height_;
    HANDLE handle_;
    CONSOLE_CURSOR_INFO saved_cursor_{};
};

// ------------------------------------------------------------------- stats --
struct Stats {
    int score{0};
    int level{1};
    int food_eaten{0};

    [[nodiscard]] int food_progress() const noexcept { return food_eaten % FOODS_PER_LEVEL; }
};

struct RunResult {
    int score{};
    int level{};
    DeathCause cause{};
};

// COMPARATOR - keeps the leaderboard ordered: higher score first, then level.
struct RunResultComparator {
    [[nodiscard]] bool operator()(const RunResult& a, const RunResult& b) const noexcept {
        if (a.score != b.score) return a.score > b.score;
        if (a.level != b.level) return a.level > b.level;
        return a.cause < b.cause;
    }
};

// --------------------------------------------------------------- rendering --
struct RenderContext {
    Vec2 food{};
    Vec2 head{};
    const std::set<Vec2>* body{};
    const Stats* stats{};
    std::string_view flash{};
};

// ---------------------------------------------------------------- DECORATOR -
// GoF Decorator: every renderer wraps another one and adds a layer of
// presentation. The Game assembles the stack: Level( HUD( Border( Field ) ) ).
class IRenderer {
public:
    virtual ~IRenderer() = default;  // virtual DESTRUCTOR for the base class
    virtual void render(const RenderContext& ctx, const Console& con) const = 0;
};

class FieldRenderer final : public IRenderer {
public:
    FieldRenderer() : blank_{FIELD_W, ' '} {}  // CONSTRUCTOR pre-builds the blank row

    void render(const RenderContext& ctx, const Console& con) const override {
        for (int y = 0; y < FIELD_H; ++y) {
            con.write(FIELD_ORIGIN.x, FIELD_ORIGIN.y + y, blank_, color::dim);
        }
        const Vec2 food = FIELD_ORIGIN + ctx.food;
        con.write(food.x, food.y, "*", color::food);
        for (const Vec2& cell : *ctx.body) {
            const Vec2 at = FIELD_ORIGIN + cell;
            if (cell == ctx.head) con.write(at.x, at.y, "@", color::head);
            else con.write(at.x, at.y, "o", color::body);
        }
    }

private:
    std::string blank_;
};

class RendererDecorator : public IRenderer {
public:
    explicit RendererDecorator(std::unique_ptr<IRenderer> inner) : inner_{std::move(inner)} {}

    void render(const RenderContext& ctx, const Console& con) const override { inner_->render(ctx, con); }

protected:
    std::unique_ptr<IRenderer> inner_;
};

class BorderDecorator final : public RendererDecorator {
public:
    explicit BorderDecorator(std::unique_ptr<IRenderer> inner)
        : RendererDecorator{std::move(inner)}, frame_{'+' + std::string(FIELD_W, '-') + '+'} {}

    void render(const RenderContext& ctx, const Console& con) const override {
        const int top = FIELD_ORIGIN.y - 1;
        const int bottom = FIELD_ORIGIN.y + FIELD_H;
        const int left = FIELD_ORIGIN.x - 1;
        const int right = FIELD_ORIGIN.x + FIELD_W;
        con.write(left, top, frame_, color::border);
        con.write(left, bottom, frame_, color::border);
        for (int y = top + 1; y < bottom; ++y) {
            con.write(left, y, "|", color::border);
            con.write(right, y, "|", color::border);
        }
        con.write(left, bottom + 1, " [WASD]/[ARROWS] steer  [P]ause  [Q]uit", color::hint);
        RendererDecorator::render(ctx, con);  // pass the baton to the wrapped renderer
    }

private:
    std::string frame_;
};

class HUDDecorator final : public RendererDecorator {
public:
    using RendererDecorator::RendererDecorator;

    void render(const RenderContext& ctx, const Console& con) const override {
        const int len = static_cast<int>(ctx.body->size());
        const int cells = std::clamp(ctx.stats->level, 1, 10);
        std::ostringstream os;
        os << " SCORE " << std::setw(4) << ctx.stats->score
           << "   LEN " << std::setw(3) << len
           << "   SPEED [" << std::string(cells, '>') << std::string(10 - cells, '.') << "]";
        con.write(2, FIELD_ORIGIN.y - 3, os.str(), color::hud);
        RendererDecorator::render(ctx, con);
    }
};

class LevelDecorator final : public RendererDecorator {
public:
    using RendererDecorator::RendererDecorator;

    void render(const RenderContext& ctx, const Console& con) const override {
        const int progress = ctx.stats->food_progress();
        std::ostringstream os;
        os << " LEVEL " << ctx.stats->level << "  " << level_mark(ctx.stats->level)
           << "  [" << std::string(progress, '#') << std::string(FOODS_PER_LEVEL - progress, '-')
           << "] " << progress << "/" << FOODS_PER_LEVEL << " to next";
        con.write(2, FIELD_ORIGIN.y - 2, os.str(), color::level);
        if (ctx.flash.empty()) {
            con.write(2, MSG_ROW, std::string(42, ' '), color::hint);
        } else {
            con.write(2, MSG_ROW, ctx.flash, color::level);
        }
        RendererDecorator::render(ctx, con);
    }
};

// ----------------------------------------------------------------- banner ---
namespace banner {

std::string rule(char c) { return "  +" + std::string(38, c) + '+'; }

std::string center(std::string_view text) {
    std::string inner{text};
    if (inner.size() > 38) inner.resize(38);
    const int pad = static_cast<int>(38 - inner.size()) / 2;
    return "  |" + std::string(pad, ' ') + inner + std::string(38 - pad - inner.size(), ' ') + "|";
}

std::string field(std::string_view label, std::string_view value) {
    std::string inner = std::string{label} + " : " + std::string{value};
    if (inner.size() > 38) inner.resize(38);
    return "  |" + inner + std::string(38 - inner.size(), ' ') + "|";
}

}  // namespace banner

// ------------------------------------------------------------------- snake --
class Snake {
public:
    // CONSTRUCTOR - the "__init__" of the snake: builds the starting body and
    // the occupied-cell set in one shot.
    Snake(const Console& console, const Stats& stats, Vec2 spawn, Direction heading, int length)
        : console_{console}, stats_{stats}, heading_{heading} {
        for (int i = 0; i < length; ++i) {
            segments_.push_back(spawn + Vec2{-i, 0});
        }
        occupied_.insert(segments_.begin(), segments_.end());
    }

    // DESTRUCTOR - fires the instant the snake is annihilated (or withdrawn):
    // prints the run report and the LEVEL MARKINGS.
    ~Snake() {
        const bool died = cause_ == DeathCause::WallImpact || cause_ == DeathCause::SelfCollision;
        const std::string_view title =
            died                  ? "xXx  SNAKE ANNIHILATED  xXx"
            : cause_ == DeathCause::Transcended ? "PERFECT RUN"
                                                : "SNAKE WITHDRAWN";
        const WORD title_color =
            died ? color::death : (cause_ == DeathCause::Transcended ? color::head : color::hud);

        const int progress = stats_.food_progress();
        const std::string prog =
            "[" + std::string(progress, '#') + std::string(FOODS_PER_LEVEL - progress, '-') + "]";
        const int cells = std::clamp(stats_.level, 1, 10);
        const std::string speed = std::string(cells, '>') + std::string(10 - cells, '.');

        console_.attr(title_color);
        std::cout << banner::rule('=') << '\n'
                  << banner::center(title) << '\n'
                  << banner::rule('=') << '\n';
        console_.attr(color::hud);
        std::cout << banner::field("CAUSE OF DEATH", to_text(cause_)) << '\n'
                  << banner::field("FINAL SCORE",
                                   std::to_string(stats_.score) + "    LENGTH " + std::to_string(length()))
                  << '\n'
                  << banner::rule('-') << '\n';
        console_.attr(color::level);
        std::cout << banner::center(">> LEVEL MARKINGS <<") << '\n'
                  << banner::field("  LEVEL REACHED",
                                   std::to_string(stats_.level) + "  (" + std::string{level_mark(stats_.level)} + ")")
                  << '\n'
                  << banner::field("  PROGRESS TO NEXT",
                                   prog + "  " + std::to_string(progress) + "/" + std::to_string(FOODS_PER_LEVEL))
                  << '\n'
                  << banner::field("  SPEED CLASS", speed) << '\n';
        console_.attr(title_color);
        std::cout << banner::rule('=');
        console_.attr(7);
        std::cout << std::flush;
    }

    Snake(const Snake&) = delete;
    Snake& operator=(const Snake&) = delete;

    void steer(Direction d) noexcept {
        if (!is_opposite(heading_, d)) heading_ = d;
    }
    [[nodiscard]] Direction heading() const noexcept { return heading_; }
    [[nodiscard]] Vec2 head() const noexcept { return segments_.front(); }
    [[nodiscard]] const std::set<Vec2>& body() const noexcept { return occupied_; }
    [[nodiscard]] int length() const noexcept { return static_cast<int>(segments_.size()); }
    [[nodiscard]] bool occupies(Vec2 cell) const { return occupied_.count(cell) > 0; }

    void drop_tail() {
        occupied_.erase(segments_.back());
        segments_.pop_back();
    }
    void push_head(Vec2 cell) {
        segments_.push_front(cell);
        occupied_.insert(cell);
    }
    void set_cause(DeathCause cause) noexcept { cause_ = cause; }

private:
    const Console& console_;
    const Stats& stats_;
    Direction heading_;
    std::deque<Vec2> segments_;
    std::set<Vec2> occupied_;  // ordered by Vec2::operator<  (COMPARATOR)
    DeathCause cause_ = DeathCause::WallImpact;
};

// -------------------------------------------------------------------- game --
class Game {
public:
    // CONSTRUCTOR - the "__init__ phase" of the game: console lock, renderer
    // stack, RNG seed and leaderboard all come to life here.
    Game()
        : console_{SCREEN_W, SCREEN_H},
          renderer_{std::make_unique<LevelDecorator>(
              std::make_unique<HUDDecorator>(
                  std::make_unique<BorderDecorator>(
                      std::make_unique<FieldRenderer>())))},
          rng_{std::random_device{}()} {}

    // DESTRUCTOR - final session report, already ranked by the COMPARATOR.
    ~Game() {
        console_.clear();
        console_.write(13, 3, "SESSION LEADERBOARD", color::hud);
        console_.write(10, 4, "ranked by RunResultComparator", color::hint);
        console_.write(3, 5, std::string(40, '-'), color::border);
        if (leaderboard_.empty()) {
            console_.write(14, 8, "no runs recorded", color::hint);
        } else {
            console_.write(6, 7, "RANK   SCORE   LVL   MARKING", color::level);
            int row = 9;
            int rank = 1;
            for (const RunResult& run : leaderboard_) {  // multiset iterates in comparator order
                if (rank > 5) break;
                std::ostringstream os;
                os << " #" << rank << "      " << std::setw(4) << run.score << "    "
                   << std::setw(2) << run.level << "     " << level_mark(run.level);
                console_.write(6, row++, os.str(), rank == 1 ? color::head : color::hud);
                ++rank;
            }
        }
        console_.write(12, 15, "thanks for playing NEON SNAKE", color::hint);
        console_.write(8, 16, "all destructors fired - console restored", color::hint);
        std::cout << std::flush;
    }

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    void run() {
        title_screen();
        for (;;) {
            play_round();
            console_.write(4, PROMPT_ROW, "PLAY AGAIN?   [Y] yes    [N] no", color::hud);
            console_.place(4 + 31, PROMPT_ROW);
            for (;;) {
                const int key = _getch();
                if (key == EOF || key == 27 || key == 'n' || key == 'N' || key == 'q' || key == 'Q') return;
                if (key == 'y' || key == 'Y') break;
            }
        }
    }

private:
    void title_screen() {
        console_.clear();
        const std::string bar(44, '=');
        console_.write(1, 4, bar, color::border);
        console_.write(14, 5, "N E O N   S N A K E", color::head);
        console_.write(9, 6, "a modern C++ pattern showcase", color::hint);
        console_.write(1, 7, bar, color::border);
        console_.write(3, 9, "CONSTRUCTOR  the __init__ of C++", color::level);
        console_.write(3, 10, "DECORATOR    layered renderer stack", color::level);
        console_.write(3, 11, "COMPARATOR   ranks the leaderboard", color::level);
        console_.write(3, 12, "DESTRUCTOR   reports each annihilation", color::level);
        console_.write(2, 15, "steer [WASD]/[ARROWS]  pause [P]  quit [Q]", color::hud);
        console_.write(2, 16, "eat *, dodge walls, never bite yourself", color::hud);
        console_.write(2, 17, "every 5 foods => LEVEL UP (faster)", color::hud);
        console_.write(14, 20, "-- press any key --", color::food);
        std::cout << std::flush;
        _getch();
    }

    void reset_round() {
        console_.clear();
        stats_ = Stats{};
        flash_.clear();
        flash_ticks_ = 0;
        // CONSTRUCTOR in action: a brand-new Snake self-assembles in one call
        snake_ = std::make_unique<Snake>(console_, stats_,
                                          Vec2{FIELD_W / 2 + 2, FIELD_H / 2},
                                          Direction::Right, 3);
        // fresh 3-cell snake leaves 573 free cells - placement cannot fail here
        (void)place_food();
        flash_ = "> constructor online - snake spawned";
        flash_ticks_ = 12;
        draw();
    }

    void play_round() {
        reset_round();
        for (;;) {
            const auto frame_start = std::chrono::steady_clock::now();
            if (!pump_input()) {
                annihilate(DeathCause::Withdrawn);
                return;
            }
            if (!step()) return;  // step() already annihilated the snake
            draw();
            std::this_thread::sleep_until(frame_start + tick());
        }
    }

    bool pump_input() {
        while (_kbhit()) {
            const int key = _getch();
            if (key == 0 || key == 224) {  // extended-key prefix (arrows)
                switch (_getch()) {
                    case 'H': snake_->steer(Direction::Up); break;
                    case 'P': snake_->steer(Direction::Down); break;
                    case 'K': snake_->steer(Direction::Left); break;
                    case 'M': snake_->steer(Direction::Right); break;
                    default: break;
                }
            } else if (key == 'q' || key == 'Q' || key == 27) {
                return false;
            } else if (key == 'p' || key == 'P') {
                if (!pause_dialog()) return false;
            } else {
                switch (key) {
                    case 'w': case 'W': snake_->steer(Direction::Up); break;
                    case 's': case 'S': snake_->steer(Direction::Down); break;
                    case 'a': case 'A': snake_->steer(Direction::Left); break;
                    case 'd': case 'D': snake_->steer(Direction::Right); break;
                    default: break;
                }
            }
        }
        return true;
    }

    bool pause_dialog() {
        console_.write(2, MSG_ROW, "** PAUSED - [P] resume, [Q] quit **", color::hint);
        std::cout << std::flush;
        for (;;) {
            const int key = _getch();
            if (key == EOF) return false;
            if (key == 'p' || key == 'P') return true;
            if (key == 'q' || key == 'Q' || key == 27) return false;
        }
    }

    bool step() {
        const Vec2 next = snake_->head() + to_delta(snake_->heading());
        if (next.x < 0 || next.x >= FIELD_W || next.y < 0 || next.y >= FIELD_H) {
            annihilate(DeathCause::WallImpact);
            return false;
        }
        const bool grow = (next == food_);
        if (!grow) snake_->drop_tail();  // the tail vacates first...
        if (snake_->occupies(next)) {    // ...so the cell it left can be re-entered
            annihilate(DeathCause::SelfCollision);
            return false;
        }
        snake_->push_head(next);
        if (grow && !on_food()) {
            annihilate(DeathCause::Transcended);  // the board is full: perfect run
            return false;
        }
        return true;
    }

    bool on_food() {
        stats_.score += 10 * stats_.level;
        ++stats_.food_eaten;
        if (stats_.food_eaten % FOODS_PER_LEVEL == 0) {
            ++stats_.level;
            flash_ = ">> LEVEL " + std::to_string(stats_.level) + ": " +
                     std::string{level_mark(stats_.level)} + " - speed up <<";
            flash_ticks_ = 16;
        }
        return place_food();
    }

    [[nodiscard]] bool place_food() {
        std::vector<Vec2> free_cells;
        free_cells.reserve(static_cast<std::size_t>(FIELD_W) * FIELD_H);
        for (int y = 0; y < FIELD_H; ++y) {
            for (int x = 0; x < FIELD_W; ++x) {
                if (!snake_->occupies(Vec2{x, y})) free_cells.push_back(Vec2{x, y});
            }
        }
        if (free_cells.empty()) return false;
        std::uniform_int_distribution<std::size_t> pick{0, free_cells.size() - 1};
        food_ = free_cells[pick(rng_)];
        return true;
    }

    void draw() {
        const RenderContext ctx{food_, snake_->head(), &snake_->body(), &stats_, flash_};
        renderer_->render(ctx, console_);
        if (flash_ticks_ > 0 && --flash_ticks_ == 0) flash_.clear();
    }

    void annihilate(DeathCause cause) {
        snake_->set_cause(cause);
        leaderboard_.insert(RunResult{stats_.score, stats_.level, cause});
        console_.place(0, BANNER_ROW);
        snake_.reset();  // DESTRUCTOR fires here: annihilation report + level markings
    }

    [[nodiscard]] std::chrono::milliseconds tick() const {
        const int ms = std::max(55, 150 - (stats_.level - 1) * 15);
        return std::chrono::milliseconds{ms};
    }

    Console console_;
    std::unique_ptr<IRenderer> renderer_;                                  // DECORATOR stack
    std::mt19937 rng_;
    std::multiset<RunResult, RunResultComparator> leaderboard_;            // COMPARATOR
    Stats stats_;
    Vec2 food_{};
    std::unique_ptr<Snake> snake_;
    std::string flash_;
    int flash_ticks_{0};
};

}  // namespace neon

int main() {
    neon::Game game;  // CONSTRUCTOR: the "__init__ phase" of the game
    game.run();       // the momentum: input -> step -> render, tick after tick
    return 0;         // DESTRUCTORS: ~Game prints the leaderboard, ~Console restores the terminal
}
