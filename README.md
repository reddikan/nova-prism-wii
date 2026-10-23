# NOVA PRISM  (Wii homebrew)

A bright, low-poly side-view space shooter for the Nintendo Wii, played with the **Wii Classic Controller**.
Everything is drawn by the Wii's GPU as flat-shaded triangles with additive glow, at a locked **60 fps** in **16:9**.

Fly through the wrap-around **Crystal Caverns**, fight enemies coming from both sides, keep your
multiplier climbing, and take down the **Hexacore**.

## Controls (Classic Controller)

| Button | Action |
|---|---|
| Left stick / D-Pad | fly |
| **A** | **Pulse Blaster** - hold for rapid fire. Watch the heat ring: overheat and it locks up for a second |
| **B** | **Nova Blade** - hold to spin twin energy sabres around the ship. Slices enemies and cuts bullets out of the air |
| **X** | **Swarm Missiles** - 8 homing missiles that each pick a different target |
| **Y** | **Prism Lance** - a short charge, then a screen-wide rainbow beam that pierces everything |
| **L / R** (or ZL / ZR) | turn the ship around to shoot behind you |
| + | pause (resume / restart / sound on-off / quit) |
| - | switch between full 16:9 and a letterboxed picture for a 4:3 TV |
| HOME | quit |

B, X and Y use **energy** (the yellow bar). Destroyed enemies drop **yellow crystals** - fly near them and they
get pulled in to refill it. Green hexagons restore a shield pip.

**Scoring:** every kill raises your **multiplier** (up to 30x); getting hit halves it. Kills within a couple of
seconds form a **chain** (big chains pay a bonus), and clearing a wave without getting hit is a **PERFECT** bonus.
Your rank (S / A / B / C) is shown at the end of the level.

## Level 1: Crystal Caverns

10 waves: blue **Shards** that hunt you down (and lunge when they flash red), green **Zip** flocks,
purple **Spores** that fire bullet fans, segmented **Serpents** that chain-explode when you kill the head,
and the boss, the **Hexacore**, with six orbiting crystal shields and an overdrive phase at half health.

## Build it (no toolchain needed on your computer)

Push this folder to a GitHub repository. The included GitHub Actions workflow
(`.github/workflows/build.yml`) builds it in the official devkitPro container on every push.
Open the run under the **Actions** tab and download the **nova-prism-wii** artifact; it contains `boot.dol`
and an `apps/novaprism/` folder (boot.dol + meta.xml + icon.png) for the Homebrew Channel.

To build locally instead: install devkitPro (`sudo dkp-pacman -S wii-dev`) and run `make`.

## Run it in Dolphin

1. **Controllers > Wii Remote 1 = Emulated Wii Remote > Configure > Extension = Classic Controller**, then map
   the Classic Controller's buttons **and its left stick** (set this before starting the game).
2. Config > Wii: **Aspect Ratio = 16:9**. Graphics: Aspect Ratio **Auto**, VSync on.
3. Open `boot.dol`.
4. Because everything is real geometry, raising Dolphin's **internal resolution** (2x / 3x) makes the
   game look razor sharp. On Android, 2x is a good balance.

On a real Wii: copy `apps/novaprism/` to the `apps` folder on the SD card.

## Adding levels

Levels live in `source/levels.c`. Copy the LEVEL 1 block, give it a new name, colours and terrain seed, and
write its waves (each wave is a list of `{enemy type, count, side, delay}` groups). The title screen gets a
level picker automatically once there is more than one level, and clearing a level continues to the next one.

## Testing (no Wii needed)

`make test` (or `sh tools/run_tests.sh`) builds the game natively and runs a harness that:

* software-renders the exact triangle list the Wii's GPU receives and saves screenshots to `tools/out/`
* checks controls (turning, overheating, energy costs, blade hold/release, pause)
* plays the whole level with bots of three skill levels over many random seeds (the "clumsy" bot clears
  about 60% of the time; doing nothing loses)
* measures CPU time and triangle counts per frame

`sh tools/run_tests.sh reel` also renders a gameplay preview video with the game's own sounds.

> **Honest note:** the game was tested natively as above, and `source/main.c` (the only Wii-specific file) was
> type-checked for the Wii's PowerPC CPU against the real libogc headers, but it has not been run on a Wii or in
> Dolphin yet. If something looks off, the likely spot is the GX setup in `video_init()`.

## Layout

```
source/game.c        rules: ship, weapons, enemies, boss, waves, scoring, menus
source/draw.c        all rendering (low-poly models, glow, particles, HUD, screens)
source/levels.c      level definitions (add new levels here)
source/gfx.c/.h      triangle-list builder + pixel font (platform independent)
source/audio.c/.h    synthesized sound effects (no audio files)
source/main.c        the Wii layer: GX graphics, Classic Controller, ASND audio
tools/test.c         native test harness, software rasterizer, bots, preview recorder
```
