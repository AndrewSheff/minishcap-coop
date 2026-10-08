<p align="center">
  <img src="docs/picori-logo.png" alt="Project Picori" width="240">
</p>

# Picori Co-op — local co-op for The Minish Cap

[Русская версия](README.ru.md)

Picori Co-op adds a second Link to [Project Picori](https://github.com/999sian/tmc), the native PC port of *The Legend of Zelda: The Minish Cap*. Two players go through the whole game together on one screen, using keyboard or gamepads.

Version **v0.1.0**, built on port 0.9.5. The window title shows the version. The original port's README is in [README-PICORI.md](README-PICORI.md).

## Features

- **A second Link** with his own A/B items and his own hearts. Hearts can be shared instead (`coop_shared_hearts`).
- **The camera** keeps both Links in view. They can't walk more than a screen apart.
- **Enemies and bosses** go after the nearer Link.
- **Player 2 can:**
  - press floor buttons and pressure plates;
  - lift, carry and throw pots;
  - push blocks;
  - open chests and doors;
  - read signs;
  - talk to NPCs and fuse kinstones.
- **Moving platforms, lilypads, clouds, minecarts, the Deepwood barrel, Gleerok and Gyorg** carry both Links.
- **Player 1 leads room transitions, portals, cutscenes, shops and minigames.** Player 2 rides along or steps aside, then reappears next to player 1. Both Links shrink to Minish size together.
- **Knock-out:** if player 2 runs out of hearts, he sits out for 3 seconds and comes back next to player 1. The game is over only when player 1 runs out of hearts.
- **Saves remember player 2.** His items and hearts are stored per save slot in `tmc.coop`; `tmc.sav` itself is unchanged. Quicksaves include player 2 too.

## Building

You need your own dump of *The Minish Cap* (USA). This repository contains no ROM and no data extracted from one.

```bash
python3 build.py --usa
```

The game ends up in `dist/USA/tmc_pc`. See [INSTALL.md](INSTALL.md) for dependencies and other platforms.

## Controls

| Action | Player 1 (keyboard) | Player 2 (keyboard) |
|---|---|---|
| Move | arrow keys | I / J / K / L |
| A (sword, action) | X | O |
| B (item) | Z | U |
| R (action, lift, roll) | S | H |
| L (kinstones) | A | Y |
| Start (menu) | Enter | N |

- **Gamepads:** the first connected gamepad goes to player 1, the second to player 2. If player 1's gamepad is unplugged, player 2 keeps his. A gamepad plugged back in goes to the player without one.
- **Joining:** player 2 joins when you press any of his keys or a button on his gamepad. If a game was saved with two players, player 2 comes back on his own when that save is loaded.
- **Player 2's menu** (N key): choose his A/B items. His item buttons and hearts are shown at the top of the screen.
- **Text boxes** can be advanced by either player.

## Settings (`dist/USA/config.json`)

| Key | Default | Effect |
|---|---|---|
| `coop_p2_join` | `true` | Player 2 joins when one of his keys is pressed. `false` prevents joining by accident |
| `coop_shared_hearts` | `false` | `true`: one shared heart bar, as in single player |
| `p2_gamepad` | `"auto"` | `"auto"`: the second gamepad belongs to player 2. `"first"`: the first (only) gamepad belongs to player 2 and player 1 uses the keyboard. `"none"`: player 2 uses the keyboard only |
| `p2_bindings` | see the table above | Player 2's keys |

## Co-op test mode

Test mode jumps straight into a room to try a mechanic: enemies, buttons, portals, bosses. Pick a room with the buttons below **Play** on the launch screen, or from **F8 → Coop Test**. From a terminal:

```bash
cd dist/USA
TMC_COOP_TEST=enemies ./tmc_pc
```

The presets are defined in `assets/coop_tests.json`.

## Known limitations

- Shops, story-driven kinstone fusions, Minish portals, minecarts and clouds are led by player 1. Player 2 rides along or waits nearby.
- Player 2 steps aside during minigames: Anju's cucco game and Borlov's chest game.
- An NPC in conversation turns to player 1, even when player 2 started the talk.
- Cheap keyboards drop some key presses when both players hold several keys at once. Gamepads work better.
- Quicksaves from earlier versions of the port don't load in this version. Regular saves (`tmc.sav`) work.

## Credits and license

- [999sian/tmc — Project Picori](https://github.com/999sian/tmc): the PC port this mod is built on.
- [zeldaret/tmc](https://github.com/zeldaret/tmc): the decompilation of *The Minish Cap*.
- [agbplay](https://github.com/ipatix/agbplay) and the other libraries listed in [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

The co-op mod was developed with the help of [Claude Code](https://claude.com/claude-code).

Licensed under GPL-3.0, like the port; see [LICENSE](LICENSE). *The Legend of Zelda* and *The Minish Cap* are trademarks of Nintendo. This project contains no ROM and no data taken from one.
