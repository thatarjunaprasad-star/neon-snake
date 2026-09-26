// Headless test harness for NEON SNAKE.
// Includes the game source (with main renamed) and exercises the pure logic:
// the Vec2 comparator, the leaderboard comparator, level markings, steering,
// and the Snake destructor's annihilation report.
#define main game_main
#include "snake.cpp"
#undef main

#include <cassert>

int main() {
    using namespace neon;

    // --- COMPARATOR: Vec2::operator< orders cells inside std::set ---
    {
        std::set<Vec2> cells;
        cells.insert(Vec2{5, 0});
        cells.insert(Vec2{1, 0});
        cells.insert(Vec2{1, 2});
        cells.insert(Vec2{1, 2});  // duplicate must be rejected
        assert(cells.size() == 3);
        assert((*cells.begin() == Vec2{1, 0}));
        assert((*std::prev(cells.end()) == Vec2{5, 0}));
    }

    // --- COMPARATOR: RunResultComparator ranks the leaderboard ---
    {
        std::multiset<RunResult, RunResultComparator> board;
        board.insert(RunResult{10, 1, DeathCause::WallImpact});
        board.insert(RunResult{50, 3, DeathCause::SelfCollision});
        board.insert(RunResult{50, 4, DeathCause::Withdrawn});
        board.insert(RunResult{20, 2, DeathCause::Transcended});
        assert(board.begin()->score == 50);
        assert(board.begin()->level == 4);  // higher level wins the tie
        assert(std::next(board.begin())->level == 3);
        assert(std::prev(board.end())->score == 10);  // lowest score last
    }

    // --- level markings ---
    static_assert(level_mark(1) == "HATCHLING");
    static_assert(level_mark(2) == "CRAWLER");
    static_assert(level_mark(5) == "APEX");
    static_assert(level_mark(9) == "LEGENDARY");

    // --- direction helpers ---
    assert(is_opposite(Direction::Left, Direction::Right));
    assert(is_opposite(Direction::Up, Direction::Down));
    assert(!is_opposite(Direction::Left, Direction::Up));
    assert((to_delta(Direction::Up) == Vec2{0, -1}));
    assert((to_delta(Direction::Right) == Vec2{1, 0}));

    // --- CONSTRUCTOR + DESTRUCTOR: Snake lifecycle ---
    {
        Console con{4, 4};  // headless console; Win32 calls fail harmlessly on a pipe
        Stats stats;
        {
            Snake s{con, stats, Vec2{10, 9}, Direction::Right, 3};
            assert(s.length() == 3);
            assert((s.head() == Vec2{10, 9}));
            assert(s.occupies(Vec2{8, 9}));   // tail segment
            assert(!s.occupies(Vec2{7, 9}));  // one past the tail

            // move: tail vacates, head advances
            s.drop_tail();
            s.push_head(Vec2{11, 9});
            assert(s.length() == 3);
            assert((s.head() == Vec2{11, 9}));
            assert(!s.occupies(Vec2{8, 9}));  // old tail is free again
            assert(s.occupies(Vec2{11, 9}));

            // steering must never allow a 180-degree reversal
            s.steer(Direction::Left);
            assert(s.heading() == Direction::Right);
            s.steer(Direction::Up);
            assert(s.heading() == Direction::Up);

            stats.score = 120;
            stats.level = 3;
            stats.food_eaten = 7;
            s.set_cause(DeathCause::SelfCollision);
        }  // <- DESTRUCTOR fires here: prints the annihilation report + level markings
    }

    // --- DECORATOR: the renderer stack assembles and renders headlessly ---
    {
        Console con{SCREEN_W, SCREEN_H};
        std::unique_ptr<IRenderer> renderer = std::make_unique<LevelDecorator>(
            std::make_unique<HUDDecorator>(
                std::make_unique<BorderDecorator>(std::make_unique<FieldRenderer>())));
        Stats stats{40, 2, 3};
        std::set<Vec2> body{{5, 5}, {6, 5}, {7, 5}};
        const RenderContext ctx{Vec2{9, 9}, Vec2{7, 5}, &body, &stats, "test flash"};
        renderer->render(ctx, con);  // must not throw; output goes to the pipe
    }

    std::cout << "\nALL HEADLESS TESTS PASSED\n";
    return 0;
}
