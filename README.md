# NEON SNAKE

A console Snake game in modern C++17, built as a showcase of four classic
object-oriented constructs: **constructor**, **destructor**, **decorator**,
and **comparator**.

```
+--------------------------------+
|                                |
|   o o o @            *         |
|                                |
+--------------------------------+
 LEVEL 2  CRAWLER  [###--] 3/5 to next
 SCORE   40   LEN    3   SPEED [>>........]
```

## How the four constructs power the game

| Construct | Where it lives |
|-----------|----------------|
| **Constructor** | Every subsystem self-initializes — the "starting phase" of the game. `Game`, `Snake`, `Console`, and all renderers come to life fully formed. |
| **Decorator** | The frame is drawn by a wrapping renderer stack: `Level( HUD( Border( Field ) ) )`, assembled in one expression. |
| **Comparator** | `Vec2::operator<` orders cells inside a `std::set` for O(log n) collision checks; `RunResultComparator` ranks the session leaderboard. |
| **Destructor** | The moment the snake is annihilated, `~Snake` prints the boxed death report and **LEVEL MARKINGS** (level, rank name, progress bar, speed class); `~Game` prints the ranked leaderboard. |

## Gameplay

- Steer with **WASD** or **arrow keys**, pause with **P**, quit with **Q**
- Eat `*` to grow — every 5 foods is a **LEVEL UP**: a new rank name
  (HATCHLING → CRAWLER → HUNTER → PREDATOR → APEX → LEGENDARY) and faster ticks
- Score scales with level; fill the whole board for a PERFECT RUN
- On death: annihilation report, level markings, and a ranked session leaderboard

## Build & run (Windows)

```bat
build.bat     :: compiles snake.cpp -> snake.exe
snake.exe     :: play!
```

Manual build with any C++17 compiler:

```sh
g++ -std=c++17 -O2 -Wall -Wextra snake.cpp -o snake.exe
```

No third-party dependencies — only the standard library and the Win32 console API.

## Project layout

| File | Purpose |
|------|---------|
| `snake.cpp` | The game (single translation unit, ~600 lines) |
| `build.bat` | One-command build (includes a GCC specs workaround) |
| `test_snake.cpp` | Headless unit tests for comparators, steering, lifecycle |
| `autoplay_test.cpp` | Integration test that drives the real game loop via scripted input |

## Testing

```sh
g++ -std=c++17 -O2 test_snake.cpp -o test && ./test      # unit tests
g++ -std=c++17 -O2 autoplay_test.cpp -o at && ./at       # full autoplay round
```

The autoplay harness swaps `_kbhit`/`_getch` for scripted input and runs
complete rounds — wall death, food chasing, leveling, pause/resume — verifying
scoring, level markings, and the destructor reports end to end.
