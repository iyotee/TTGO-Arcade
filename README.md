# 🎮 TTGO-Arcade

> Retro arcade game collection for the **LilyGO TTGO T-Display V1.1** (ESP32).  
> Featuring **50 playable two-button games**, **2 pocket tools**, a lightweight animated UI, persistent high scores, and a modular single-file architecture for makers.

---

## ✨ Features

- **50 Arcade, Action, Puzzle & Retro Games** — All designed specifically for a **135 × 240 portrait** IPS screen and **2 physical buttons**.
- **2 Pocket Tools** — Built-in **Stopwatch/Timer** (with lap tracking) and **Flashlight** (White, Warm, Red, SOS, Strobe).
- **Smooth Animated Menu** — 60 fps scrolling list with live animated game icons, mini battery/USB status badge, and fast scroll on `Hold L`.
- **Persistent Saves (NVS)** — Remembers high scores, last played game, brightness, auto-sleep timer, and USB logging state across power-offs.
- **Power & Diagnostics** — 5 brightness levels, configurable auto-sleep (`30s` / `60s` / `2m` / `5m` / `Off`), deep sleep with single-button wake, battery voltage curve, and crash reporting.
- **Easy to Extend** — Add a new game with one `src/<game>.cpp` file and two one-line registrations.

---

## 🕹️ Controls Overview

The board has two buttons:
- **L** — `GPIO0` (also the `BOOT` button)
- **R** — `GPIO35` (also wakes the board from deep sleep)

### Universal Navigation
- **Menu**: `Tap L` next item · `Hold L` fast scroll · `Tap R` open / play
- **Title Screen**: press any button to start
- **Game Over**: `L` return to menu · `R` play again

---

## 🎯 Games (50)

### Arcade & Action
| Game | Controls | Goal |
|---|---|---|
| **Pinball** | `Hold L / R` flippers · `Hold & release R` launch | Hit bumpers, drop targets & top lanes (up to 5x); 3 balls |
| **Bricks** | `Hold L / R` move paddle | Breakout: clear the wall, 3 lives, faster each level |
| **Tennis** | `Hold L / R` move | Beat the CPU; paddle edges give sharp angles (first to 5) |
| **Pong 2P** | `Tap L` P1 flip dir · `Tap R` P2 flip dir | Local 2-player Pong on one board (first to 5; best = max rally) |
| **Shooter** | `Hold L / R` fly (auto-fire) | Destroy saucers and cruisers in growing waves; 3 lives |
| **Invaders** | `Hold L / R` move cannon (auto-fire) | Clear alien waves behind 3 destructible shields; 3 lives |
| **Meteors** | `Hold L / R` rotate · `L + R` thrust (auto-fire) | Blast splitting asteroids in wrap-around space; 3 lives |
| **River** | `Hold L / R` steer · `L + R` boost (auto-fire) | Shoot choppers & ships; fly over `F` tanks to refuel |
| **Missile** | `Hold L / R` sweep turret (auto-fires flak) | Intercept incoming missiles to protect 3 cities |
| **Bumper** | `Hold L / R` steer · `L + R` turbo ram | Knock AI bots out of the circular sumo arena |
| **Maze** | `Tap L / R` turn left/right · `Hold L / R` U-turn | Eat all dots & power pellets, dodge ghosts; 3 lives |
| **Snake** | `Tap L / R` turn left/right (relative) | Eat food, avoid walls and yourself |
| **Tron** | `Tap L / R` turn 90° left/right | Light cycles: trap the AI in your trail; each round is faster |
| **Tank** | `Hold L / R` aim barrel · `Hold L + R` charge, release to fire | Artillery duel vs a CPU tank over hills and wind; 3 HP each |
| **Sub** | `Hold L` dive · `Hold R` rise · `L + R` torpedo | Grab sea-floor chests, sink sharks & mines, surface for oxygen; 3 hulls |

### Endless Runners, Jumpers & Dodgers
| Game | Controls | Goal |
|---|---|---|
| **Flappy** | `Tap L` or `Tap R` flap | Fly through pipe gaps; ramps up after ~30 points |
| **Doodle** | `Hold L / R` steer left/right | Bounce upward on platforms; cyan moves, yellow boosts |
| **Falldown** | `Hold L / R` roll left/right | Drop through gaps before rising floors crush you |
| **Dino** | `Tap R` jump · `Hold L` duck / fast-fall | Leap cacti and duck under birds |
| **Cave** | `Hold L` or `Hold R` thrust up | Fly through a narrowing cave and dodge rock pillars |
| **Ski** | `Hold L / R` carve left/right | Dodge trees and rocks; pass flag gates for +50 |
| **Race** | `Hold L / R` steer | Dodge traffic on a winding road; grass slows you down |
| **Crossy** | `Tap L / R` forward · `Hold L / R` sideways · `L + R` back | Cross roads and rivers; keep moving before the eagle comes |
| **Duet** | `Hold L / R` spin orbit CCW / CW | Thread both orbiting spheres past falling blocks |
| **Lunar** | `Hold L / R` push left/right · `L + R` main thrust | Land softly on the flat green pad before fuel runs out |
| **Frog** | `Tap L / R` hop forward · `Hold L / R` hop left/right | Cross traffic, ride logs, fill all 5 lily-pad homes; 3 lives |
| **Gravity** | `Tap L / R` fall to the left/right wall | Flip between walls to dodge spikes and saws; gems +10 |
| **Swing** | `Hold L / R` grapple (rope reels in) · release to fly | Swing from anchor to anchor; don't fall or get left behind |
| **Chute** | `Hold L / R` steer · `L + R` open chute (below 350 m) | Skydive through rings, dodge birds, land on the pad |

### Timing, Reflex & Skill
| Game | Controls | Goal |
|---|---|---|
| **Stack** | `Tap L` or `Tap R` drop block | Stack cleanly; 3 perfect drops in a row widen the block |
| **Rhythm** | `Tap L / R` hit left/right beat | Hit falling notes on the target ring (up to 4x combo) |
| **Climb** | `Tap L / R` chop left/right | Climb fast without hitting branches or timing out |
| **Catch** | `Hold L / R` move basket | Catch coins & gems, avoid red bombs; 3 lives |
| **Whack** | `Tap L / R` whack left/right burrow | Hit moles (+10 / +30), skip red bombs; 3 lives |
| **Golf** | `Hold L / R` aim · `Hold & release L + R` putt | Sink holes before running out of shots (+3 shots/hole) |
| **Plinko** | `Tap L / R` drop ball · `Hold L / R` nudge balls | 12 balls through a peg board; the 100 slot gives a free ball |
| **Math** | `Tap L` false · `Tap R` true | Judge equations before the timer bar empties; 3 lives |

### Puzzle, Memory & Cards
| Game | Controls | Goal |
|---|---|---|
| **Tetris** | `Tap L / R` move · `Hold L` rotate · `Hold R` soft drop · `L + R` hard drop | Clear lines; speeds up every 10 lines |
| **Columns** | `Tap L / R` move · `Hold L` cycle gems · `Hold R` fast drop | Match 3+ gems horizontally, vertically, or diagonally |
| **2048** | `Tap L / R` up/right · `Hold L / R` left/down | Merge matching tiles to reach 2048 and beyond |
| **Simon** | `Tap L / R` cyan / magenta pad | Watch and repeat the growing color sequence |
| **Memory** | `Tap L` next card · `Tap R` flip card | Match all 8 pairs within limited tries |
| **Mines** | `Tap L` cell · `Hold L` row · `Tap R` reveal · `Hold R` flag | Clear minefields without hitting a mine |
| **21** | `Tap L` hit · `Tap R` stand / next hand | Arcade Blackjack; reach the highest chip stack |
| **Hi-Lo** | `Tap L` lower · `Tap R` higher | Guess the next card; streaks build up to a 5x multiplier; 3 lives |
| **Slots** | `Tap L` bet · `Tap R` spin / stop reel · `Hold L` cash out | 3-reel fruit machine from 100 credits; best = peak credits |
| **Pop** | `Tap L` next tile · `Hold L` next row · `Tap R` pop group | Pop groups of 2+ same tiles; bigger groups score much more |
| **Lights** | `Tap L` next cell · `Hold L` next row · `Tap R` toggle | Lights Out 5×5: switch every light off within the move limit |
| **Code** | `Tap L` cycle color · `Hold L` previous peg · `Tap R` next peg / submit | Crack the hidden 4-peg color code from the clues |
| **Drop 4** | `Tap L` next column · `Tap R` drop disc | Connect four vs the CPU; keep your win streak going |

---

## 🛠️ Pocket Tools

| Tool | Controls | Description |
|---|---|---|
| **Timer** | `Tap R` start/stop · `Tap L` lap/reset · `Hold L` menu | Precision stopwatch with up to 5 lap splits |
| **Torch** | `Tap L` cycle mode · `Tap R` menu | Full-screen flashlight at max brightness (`White`, `Warm`, `Red`, `SOS`, `Strobe`) |

---

## ⚙️ Menu, Settings & Sleep

- **Menu**: Shows a live battery/USB badge top-left and remembers the last played game.
- **Settings**:
  - **Battery**: Live charge percentage and voltage (or `USB power` while plugged in).
  - **Brightness**: 5 PWM levels (`R` cycles and saves to NVS).
  - **USB log**: Toggle diagnostic serial logging (`115200` baud).
  - **Auto-sleep**: Configurable idle timeout (`30s`, `60s`, `2m`, `5m`, or `Off`).
  - **Reset scores**: Submenu to clear **All games** or any individual game's best score (with confirmation).
  - **Crash indicator**: Displays an orange warning at the bottom if a previous reset/watchdog crash occurred (e.g. `! crash in Flappy`).
- **Sleep**: Puts the ESP32 into deep sleep with backlight and battery ADC disabled. Press **R** (`GPIO35`) to wake back to the menu.

---

## 🚀 Building & Flashing

Requires [PlatformIO](https://platformio.org/) (VS Code extension or `pio` CLI). Dependencies (`espressif32@6.9.0`, `TFT_eSPI@^2.5.43`) are fetched automatically on first build.

```sh
pio run               # Compile firmware
pio run -t upload     # Compile and flash over USB
pio device monitor    # Serial monitor (115200 baud, exception decoder)
```

> **Note:** All `TFT_eSPI` pin and driver flags live in `platformio.ini`, so no manual edits inside `.pio/libdeps/` are required.  
> If your display appears upside down, set `SCREEN_ROTATION` to `2` in `src/common.h` (this also swaps L/R button mapping).

---

## 🧩 Project Structure

```text
├── platformio.ini        # Board, libraries, TFT_eSPI build flags, upload config
├── README.md             # Project documentation
└── src/
    ├── common.h          # Pin definitions, shared helpers, game declarations
    ├── main.cpp          # Scrolling menu, shared HUD/Game Over UI, setup/loop
    ├── power.cpp         # PWM backlight, battery ADC curve, deep sleep, auto-sleep
    ├── diag.cpp          # USB logging & persistent crash diagnostics
    ├── settings.cpp      # Settings UI, Reset Scores submenu & confirmation modal
    ├── timer.cpp         # Stopwatch tool
    ├── torch.cpp         # Flashlight tool
    └── <game>.cpp        # One self-contained namespace per game
```

**Adding a game:** create `src/<name>.cpp` with a `namespace <name>` exposing `begin()`, `update(now)` and `drawIcon(cx, cy, dim, t)` (copy the structure of `gravity.cpp` or `stack.cpp`), then add `DECLARE_GAME(<name>)` to `common.h` and a `GAME("Name", "<name>_hi", <name>)` line to `ITEMS[]` in `main.cpp`. The high score is kept in NVS under `<name>_hi` (15 characters max).

---

## 🔧 Troubleshooting

- **Upload fails** (`No serial data received` / `serial noise`): Run `pio run -t upload` again (the CH9102 USB-serial chip occasionally needs a retry), or hold **L (BOOT)** while upload starts. Keep `upload_speed = 460800`.
- **Screen is black**: The board is in deep sleep — press **R** (`GPIO35`) to wake it.
- **Debugging**: Enable `Settings → USB log` and run `pio device monitor`.

---

## 🖥️ Hardware

- **Board**: [LilyGO TTGO T-Display V1.1](https://lilygo.cc/products/t-display)
  - ESP32 Xtensa dual-core LX6 @ 240 MHz (4 MB Flash)
  - 1.14" ST7789V IPS LCD (`135 × 240`, SPI, full color)
  - CH9102 USB-to-Serial
  - 2-pin 1.25 mm JST LiPo battery connector
- **Battery**: 3.7 V LiPo (e.g., `102540`)
- **3D Printable Case**: [Kivo Case for LilyGO T-Display](https://makerworld.com/en/models/3387545-kivo-case-lilygo-t-display-project?from=search#profileId-3855076)
