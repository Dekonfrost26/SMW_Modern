# Simultaneous co-op (up to 4 players): design

Status: phase 1 is implemented (`src/coop.c`, `[Features] Coop`, Options >
Gameplay > Co-op). This design is based on a full audit of player state and
the in-level frame flow (function names and SNES addresses are listed below so
they can be checked).

Phase 1 as built: player 2 (second controller, Start to join, Start+Select
to leave) runs the game's player code on a swapped-in context stored in the
RAM extension, so save states include it. Pipes, doors and death bring them
back beside player 1. They are drawn as the original character on top of the
frame.

Phase 2 as built: the player-contact entry points fan out to every extra
player with that player's state swapped in: `CheckPlayerToNormalSpriteCollision`
(stomp, hurt, kick, pick up), `SolidSpriteBlock` (platforms) and
`CheckMarioToExtendedSpriteCollision` (enemy projectiles). Callers still only
see the main player's result. A normal sprite picked up by an extra player
remembers its carrier, and `SprStatus0B_Carried` runs as that player. A hurt
extra player doesn't drop the main player's reserve item. Build with
`-DCOOP_DEBUG` to log extra-player contacts. Tested: an extra player stomps a
walking enemy and is hurt (and respawned) by touching one from the side.

## Characters

- P1 is the game's own player.
- P2, P3 and P4 are an **original character** made for this port ("Sprout",
  `src/coop_art.h`), in teal, orange and purple. It is not a copy or recolour
  of a Nintendo character. It uses the same movement and power-up rules.

## Why the original game can't just run twice

- Player contact is checked inside each sprite's own AI routine (about 54
  call sites of `CheckPlayerToNormalSpriteCollision`, 01a7e4), not in a
  separate pass. Running sprites once per player would run every enemy twice.
- The player's graphics have one home: dynamic tiles 00-05/10-15 and 7F in
  VRAM (`UploadPlayerGFX`, 00:2307), one OAM block (`kPlayerGFXRt_*OAMIndex`,
  00:239-240) and one palette (CGRAM 0x86-0x8F).
- Input is merged into `io_controller_hold1/press1` (15/16) by
  `PollJoypadInputs` (008650). Only two pads exist in the host input path
  (main.c, bits 0-23).

## Architecture

1. **Player context.** A table of the per-player RAM variables (positions,
   speeds, state, timers 1496-14A6, cape, Yoshi riding, carry flags, graphics
   pointers D82-D99, input 15-18, and so on). It is saved to and loaded from
   a per-player block. The "P" entries from the audit are the starting list.
2. **Player update.** For each active player: load its context, run
   `GameMode14_InLevel_00C47E` → `HandlePlayerState`, then save the context.
   `UpdateCurrentPlayerPositionRAM` (D1/D3) runs inside the context.
3. **Collision fan-out.** Wrap the collision entry points so that each one
   loops over players, loading each player's context for the check:
   - `CheckPlayerToNormalSpriteCollision` (01a7e4)
   - `SolidSpriteBlock` and its entries (01b44f)
   - `StandardSpriteToSpriteCollisionChecks_GetMarioClipping` (03b664)
   - `CheckMarioToExtendedSpriteCollision` (02a0ac area)
   - the cluster sprite contact functions
   The shared per-sprite cooldown `spr_decrementing_table154c` needs a
   per-player version. Carried sprites and Yoshi need an owner field.
4. **Position queries.** `CheckPlayerPositionRelativeToSprite_*` (78 uses)
   pick the nearest live player instead of "the" player.
5. **Drawing.** Each extra player draws through `PlayerDraw` into an extra
   OAM page. Its tiles and palette come from a separate tile and palette
   store in the PC renderer, so it does not compete with the game's VRAM.
   Building on: `Ppu.extOam` (snes/ppu.c). It needs per-page tile and
   palette sources added.
6. **Camera.** Follows the leading live player. Players who fall too far off
   screen float back in a bubble, which is drop-in/drop-out friendly.
7. **World effects.** A player's death doesn't lock the world
   (`flag_sprites_locked`). It becomes a bubble while others are alive.
   Level end, pipes, doors and keyholes are triggered by any player and
   bring the others along. Lives, coins and score are kept per player.
8. **Input.** Extend the host input path to 4 pads (main.c, `RtlRunFrame`
   bits), and give each player context its own `io_controller_*` values.

## Phases

1. Context table, plus a second player updated and drawn (no enemy contact).
   Shared camera. (done)
2. Collision fan-out: stomp, hurt, platforms, kick/carry with owners. (done)
3. Bubbles, death and respawn, drop-in/out, 4 pads, P3/P4 characters. (done:
   controllers 3-4 feed `g_coop_pad_input` with the controller-2 button layout;
   extra players who die or fall float back in a bubble and pop out with any
   button near the main player; `CheckPlayerPositionRelativeToSprite_*` use the
   nearest player via `Coop_NearestPlayerPos`.)
4. Yoshi, cape, pipes, doors, keyholes, goal, overworld handling, saves.

Each phase keeps single-player behaviour identical. Check with
`VerifyAgainstRom = 1` while only one player is active.
