# More sprite slots

Status: implemented (steps 1-4 below). Enabled with `[Features] MoreSprites`
(default on) or Options > Gameplay > Enemy Slots; changes apply at the next
level load.

Goal: raise the normal sprite slot count from 12 (slots 0-11) to 24. With
the feature off, behaviour must stay identical to the original game: running
with `VerifyAgainstRom = 1` must show no differences.

## Storage: split, not moved

There are 49 per-slot tables, each 12 bytes at a fixed RAM address
(variables.h; for example `spr_spriteid` 9E and the 14C8-1686 block). The
plan:

- Slots 0-11 stay at their original addresses. Bulk clears, save states and
  ROM verification keep working unchanged.
- Slots 12-23 live in a RAM extension placed after the 128 KB SNES RAM
  (`g_ram` becomes larger). Each table gets 12 extra bytes there.
- All table accesses `spr_x[k]` become `SPR(x, k)`, which returns an lvalue
  in the correct storage. This is a mechanical rewrite.
- Save states and the input recorder must include the extension (they only
  cover 0x20000 bytes today: `snes/snes.c` `snes_saveload`,
  `common_rtl.c` `StateRecorder_RecordPatchByte`).
- Wherever the original bulk-clears RAM that covers sprite tables, also
  clear the extension:
  - `ClearOverworldAndCutsceneRAM` 00a1a6
  - `InitializeAllSpritesOnLevelLoad` 02abf2
  - `InitializeFirst8KBOfRAM` 008a4e
- Keep the alias `spr7c_princess_peach_spawn_fireworks_timer` (1FEB) at
  slot 9 of `spr_decrementing_table1fe2`.

## Drawing: a second OAM page

Sprites address OAM relative to `oam_buf` with 8-bit indexes (about 900
sites). While slots 12-23 are processed, switch the OAM base and the size
buffer base to a second page. The PPU draws that page behind the regular
sprites (`Ppu.extOam`, already implemented). Restore the base before
`ProcessClusterSprites` (smw_01.c, at the end of `ProcessNormalSprites`).
A slot k >= 12 uses the OAM index row entry of k-12. Sprites that write
fixed OAM entries also move to page 1 for those slots, so they lose
priority over the player. That's acceptable.

## Slot bounds

- Main loop `ProcessNormalSprites` 01808c: 11..0 → 23..0.
- Spawning: the `kParseLevelSpriteList_SpriteSlot*` tables (02a7f6 area)
  and `FindFreeNormalSpriteSlot` 02a9ef. Also hand out slots 12-23 after
  the original range is full. Reserved slots 10/11 keep their meaning.
- Full-range loops starting at 11 (key, keyhole, Yoshi, bounce, cape,
  berries, moving ledge, baby Yoshi, Big Boo, hidden 1-up, goal tape,
  ClearOutNormalSpriteSlots, InitializeAllSpritesOnLevelLoad) → 23.
- Partial loops 9..0 (Lakitu, Magikoopa, Bob-omb explosion, cape ground
  pound, fireball collision, Pokey, silver coins, skull raft, light switch,
  spotlight, DespawnNonBossSprites, ...) → also cover 12-23.
- Boss and special fixed ranges (bonus game, Bowser, Koopa kids, Peach)
  stay as they are. Those rooms use the original slot layout.
- `sprites_sprite_memory_setting` can index past the end of the slot tables
  for values ≥ 0x13 (an upstream overrun); clamp it.
- Raw read bug at 02862f (`GrabThrowBlockBlock`, smw_02.c ~856):
  `*((uint8*)&player_xpos + j + 1)` should be `HIBYTE(player_xpos)`.

Do not change the 11/9 loops that belong to minor extended sprites
(`mextspr_*`, 12 slots) or extended sprites (10 slots).

## Steps (all done)

1. `SPR()` accessor rewrite with storage still 12 slots. Verify there are
   no differences against the ROM.
2. RAM extension, clears, save-state support. Verify again (extra slots
   stay unused).
3. Main loop, OAM page switch, spawning with the extra slots. Enable with
   `[Features] MoreSprites = 1` (off in VerifyAgainstRom mode).
4. Widen the full and partial scan loops. Test busy levels.

Deliberately left on the original slot layout: the bonus game, the coin game
cloud, reserve item drops and bounce-block spawns that overwrite slots 10/11,
and an unused Yoshi routine.
