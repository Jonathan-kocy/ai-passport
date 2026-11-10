<p align="right">
  <a href="flappy-bird.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Flappy Bird (feature/flappy-bird)

A classic one-more-try flying game for FoloToy AI Passport. Guide an original
orange bird through endless pipe gaps; each gap passed scores a point. The
application replaces the baseline hardware-test menu with its own screens.

## Controls

| Button | Playing | Menus and cards |
| --- | --- | --- |
| `UP` press | Flap (rise about 36 px at the default strength) | Title and settings: move selection up; Ready: start; Paused: resume |
| `DOWN` press | Lower the volume: `HIGH`, `MED`, `LOW`, `MUTE`, then back to `HIGH` | Title and settings: move selection down; Ready and Paused: lower the volume; Tutorial: ignored; Game over: back to title |
| `OK` press | Pause | Title: select; Settings: change the selected value; Game over: retry (after a 0.7 s guard) |
| `OK` tap while paused | Resume | Tutorial step 3: resume |
| `OK` hold while paused | Quit to title | Tutorial: skip; Ready: back to title |

The game acts on press edges (`BSP_BTN_PRESS`), not clicks, so flaps have no
click-detection delay. A hold only counts when it starts on a fresh press: the
press that pauses never also quits.

## Screens

- **Title**: logo, `PLAY` / `TUTORIAL` / `SETTINGS` menu, and the best score for
  the current difficulty.
- **Settings**: `UP` and `DOWN` choose a row, `OK` changes its value, and the
  `BACK` row returns to the title.
  - `VOLUME`: `HIGH`, `MED`, `LOW`, or `MUTE`.
  - `FLAP`: how far one press lifts the bird: `SOFT` about 26 px, `NORMAL` about
    36 px, `STRONG` about 49 px.
  - `DIFFICULTY`: `EASY`, `NORMAL`, or `HARD`, with its own best score for each.
  - `THEME`: `DAY`, `SUNSET` (with a sun), or `NIGHT` (with a moon and stars).
- **Tutorial**: runs automatically before the first game (and from the title
  menu). Five interactive steps: flap with `UP`, pause with `OK`, resume with `OK`, fly
  through one wide practice gate, then read where to find `SETTINGS`. The bird
  cannot lose during the tutorial; a crash at the gate simply restarts it.
  Holding `OK` skips the tutorial at any step except the welcome card.
- **Get ready**: the bird hovers until `UP` starts the run.
- **Playing**: score at the top, volume level in the top-left corner, battery
  level in the top-right corner.
- **Paused**: dimmed scene with resume/quit instructions.
- **Game over**: score, best score, and a `NEW BEST!` badge for a record.

Each run gets gently harder over its first 20-30 points. Neighbouring gap
centres differ by at most 70 px.

| Difficulty | Pipe speed | Gap height |
| --- | --- | --- |
| `EASY` | About 105 to 140 px/s | 116 to 96 px |
| `NORMAL` | 125 to about 170 px/s | 100 to 80 px |
| `HARD` | About 145 to 203 px/s | 92 to 76 px |

## Sound, storage, and battery

- Short synthesised effects (flap, volume preview, score, crash, fall, record,
  menu, tutorial step, pause) play through the ES8311 at 16 kHz from a dedicated
  worker task. If audio initialisation fails, the game runs silently.
- Four volume levels set the codec output: `HIGH` 65 %, `MED` 50 %, `LOW` 35 %,
  and `MUTE`, which plays nothing. `HIGH` is the default. Each change plays a
  short preview at the new level, except on `MUTE`.
- Best scores, the tutorial-completed flag, and settings are stored in NVS
  namespace `flappy` (`best_e`, `best`, `best_h`, `tut_done`, `volume`, `flap`,
  `diff`, `theme`). Writes happen in a worker task, only when a value changes.
  Scores saved before difficulties existed count as `NORMAL`.
- Battery state of charge is polled every 15 s. If the CW2017 gauge is
  unavailable, only an empty battery outline is shown, never a number.

Flashing the merged image at `0x0` may reset NVS, which clears the saved best
scores and settings and shows the tutorial again. See
[flashing and stored data](development/engineering/firmware-layout.md#flashing-and-stored-data).

## Implementation map

| File | Role |
| --- | --- |
| `main/flappy_model.[ch]` | Portable rules: fixed 10 ms physics in Q8, pipes, scoring, tutorial, every button rule |
| `main/flappy_sprite.[ch]` | Procedural bird sprite (three wing frames), no binary asset |
| `main/flappy_sfx.[ch]` | Procedural sound effects rendered in 16 ms chunks |
| `main/flappy_text.h` | All user-visible strings (printable ASCII only) |
| `main/flappy_ui.[ch]` | LVGL screen built once; objects are repositioned each frame |
| `main/flappy_services.[ch]` | Audio, NVS, and battery workers |
| `main/main.c` | Start-up, button queue, and the 16 ms LVGL-timer game loop |

Button callbacks only enqueue events. The game loop is an LVGL timer, so the
model and all LVGL objects are touched only in the LVGL task. Sound, storage,
and battery reads never run in the button or LVGL context.

The UI uses the built-in Montserrat 14/20/32/48 fonts. They cover printable
ASCII and LVGL battery symbols; `tests/test_flappy_fonts.py` fails on any
non-ASCII string or an unconfigured font size. No Chinese text is displayed.

## Validation

```bash
./tools/validate.sh --static   # includes test_flappy_model, test_flappy_assets, test_flappy_fonts
./tools/validate.sh            # full gate; requires an activated ESP-IDF 5.5.3
```

The model tests cover the whole tutorial, skip rules, menus, pause/quit
press-and-hold rules, game over, best score, volume control, the settings
screen, every difficulty and flap combination, frame clamping,
and a 60-second autopilot run across 20 seeds proving generated levels stay
passable.

## On-device acceptance

| Case | Setup and input | Expected result |
| --- | --- | --- |
| First boot | Flash at `0x0`, power on | Tutorial welcome card; battery shown top-right |
| Tutorial | Follow all five steps | Each step advances only on its key; `DOWN` leaves the volume badge unchanged; practice gate retries after a crash; the settings card waits for `OK`; then Get ready |
| Skip | Hold `OK` on step 1 | Get ready; after a reboot, the title appears instead of the tutorial |
| Play | `UP` to start, play several pipes | Smooth motion, one flap per press, score increments once per pipe, sounds play |
| Volume | Press `DOWN` four times during play, listening each time | Badge steps through `MED`, `LOW`, `MUTE`, `HIGH`; sound gets quieter, then silent; the bird's flight does not change |
| Volume memory | Choose `LOW`, then power off and on | Badge still shows `LOW`; sounds stay quiet |
| Settings menu | On the title, select `SETTINGS`; press `UP` and `DOWN`; press `OK` on each row; choose `BACK` | `UP` and `DOWN` only move the highlight; `OK` changes the value; `BACK` returns to the title |
| Flap | Play with `SOFT`, then `STRONG` | Each press lifts the bird visibly less, then more |
| Difficulty | Play `EASY`, then `HARD` | Pipes are slower with wider gaps, then faster with narrower gaps; each keeps its own best score |
| Theme | Choose each theme | Sky and scenery change; text stays readable |
| Settings memory | Change all rows, then power off and on | All values are kept |
| Pause | `OK`, wait, tap `OK`; pause again and hold `OK` | Scene freezes and resumes; the hold quits to title |
| Game over | Crash | Fall to ground, game-over card; presses are ignored for 0.7 s |
| Best score | Beat the record, reboot | `NEW BEST!` badge; best score survives power-off |
| Battery | Observe on battery and with the gauge absent | Percentage and icon, red below 15 %; an outline only when unavailable |

Also check the `stats:` line that the firmware logs about every 15 s: LVGL
peak memory against the 48 KB pool, free heap, and the longest frame.
