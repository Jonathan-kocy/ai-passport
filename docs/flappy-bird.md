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
| `UP` press | Flap (rise about 36 px) | Title: move selection up; Ready: start; Paused: resume |
| `DOWN` press | Dive: a quick downward swoop (0.45 s cooldown) | Title: move selection down; Game over: back to title |
| `OK` press | Pause | Title: select; Game over: retry (after a 0.7 s guard) |
| `OK` tap while paused | Resume | Tutorial step 4: resume |
| `OK` hold while paused | Quit to title | Tutorial: skip; Ready: back to title |

The game acts on press edges (`BSP_BTN_PRESS`), not clicks, so flaps have no
click-detection delay. A hold only counts when it starts on a fresh press: the
press that pauses never also quits.

## Screens

- **Title**: logo, `PLAY` / `TUTORIAL` menu, best score.
- **Tutorial**: runs automatically before the first game (and from the title
  menu). Five interactive steps: flap with `UP`, dive with `DOWN`, pause with
  `OK`, resume with `OK`, then fly through one wide practice gate. The bird
  cannot lose during the tutorial; a crash at the gate simply restarts it.
  Holding `OK` skips the tutorial at any step except the welcome card.
- **Get ready**: the bird hovers until `UP` starts the run.
- **Playing**: score at the top, battery level in the top-right corner.
- **Paused**: dimmed scene with resume/quit instructions.
- **Game over**: score, best score, and a `NEW BEST!` badge for a record.

Difficulty rises gently: pipes speed up from 125 px/s to about 170 px/s and
gaps narrow from 100 px to 80 px over the first 20-30 points. Neighbouring gap
centres differ by at most 70 px.

## Sound, storage, and battery

- Short synthesised effects (flap, dive, score, crash, fall, record, menu,
  tutorial step, pause) play through the ES8311 at 16 kHz from a dedicated
  worker task. If audio initialisation fails, the game runs silently.
- The best score and the tutorial-completed flag are stored in NVS namespace
  `flappy` (`best`, `tut_done`). Writes happen in a worker task, only when the
  value changes.
- Battery state of charge is polled every 15 s. If the CW2017 gauge is
  unavailable, only an empty battery outline is shown, never a number.

Flashing the merged image at `0x0` may reset NVS, which clears the saved best
score and shows the tutorial again. See
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
press-and-hold rules, game over, best score, dive cooldown, frame clamping,
and a 60-second autopilot run across 20 seeds proving generated levels stay
passable.

## On-device acceptance

| Case | Setup and input | Expected result |
| --- | --- | --- |
| First boot | Flash at `0x0`, power on | Tutorial welcome card; battery shown top-right |
| Tutorial | Follow all five steps | Each step advances only on its key; practice gate retries after a crash; then Get ready |
| Skip | Hold `OK` on step 1 | Get ready; after a reboot, the title appears instead of the tutorial |
| Play | `UP` to start, play several pipes | Smooth motion, one flap per press, score increments once per pipe, sounds play |
| Dive | Press `DOWN` twice quickly | One dive; a second dive only after the cooldown |
| Pause | `OK`, wait, tap `OK`; pause again and hold `OK` | Scene freezes and resumes; the hold quits to title |
| Game over | Crash | Fall to ground, game-over card; presses are ignored for 0.7 s |
| Best score | Beat the record, reboot | `NEW BEST!` badge; best score survives power-off |
| Battery | Observe on battery and with the gauge absent | Percentage and icon, red below 15 %; an outline only when unavailable |

Also measure frame cadence during play, free heap, and the LVGL memory
monitor. The host preview peaked at about 40 KB of the LVGL heap on a 64-bit
build; the 32-bit device figure is expected to be lower than the 48 KB pool
but must be confirmed on hardware.
