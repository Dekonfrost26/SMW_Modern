// Simultaneous co-op, phase 1 (see docs/COOP.md).
//
// Extra players reuse the game's own player code: their per-player RAM state
// is stored in the RAM extension (so save states include it) and swapped into
// SNES RAM around their update. World state that a player update may change
// (game mode, sprite lock, music, level end, camera scrolling, lives) is
// restored afterwards, so in this phase an extra player can't end the level,
// freeze the world or move the camera. Extra players don't touch enemies yet
// (phase 2). They are drawn as an original character ("Sprout") on top of the
// finished frame.

#include "coop.h"
#include "variables.h"
#include "funcs.h"
#include "smw_rtl.h"
#include <string.h>
#include "coop_art.h"

bool g_coop_enabled;

typedef struct RamRange { uint16 addr, len; } RamRange;

// Per-player RAM (from the player-state audit). Scratch bytes used only
// inside a player update are included so each update starts consistent.
static const RamRange kPlayerRam[] = {
  {0x15, 4},      // controller hold/press as seen by the player code
  {0x19, 1},      // power-up
  {0x71, 0x14},   // state, air/duck/climb/swim, facing, blocked, speeds, screen pos, slope ptr
  {0x88, 0x14},   // pipe timers, collision scratch, position
  {0xD1, 4},      // position copy used by sprites
  {0x13DA, 0x0C}, // sub positions, walking frame, pose, cape image, P-meter...
  {0x13E8, 9},    // cape hitbox, sliding, on-ground flags
  {0x13F3, 1}, {0x13F9, 3}, {0x13FF, 1}, {0x1406, 4}, {0x140D, 1},
  {0x1410, 1}, {0x1419, 1}, {0x141E, 1},
  {0x1470, 2},    // carrying, on solid sprite
  {0x148F, 2},    // carrying (next frame), star timer
  {0x1496, 0x11}, // player timers (hurt flash, poses, cape...)
  {0x1697, 1}, {0x185C, 1}, {0x1878, 1}, {0x187A, 1}, {0x188A, 2}, {0x1891, 1},
  {0x18AF, 1}, {0x18BD, 2}, {0x18C2, 1}, {0x18D2, 2}, {0x18DB, 2}, {0x18E7, 1},
  {0x191C, 1},
};

// World state an extra player's update must not change in phase 1.
static const RamRange kWorldGuard[] = {
  {0x9D, 1},      // flag_sprites_locked
  {0x100, 1},     // game mode
  {0xDBE, 1},     // lives
  {0xDD5, 1},     // exit level action
  {0xDDA, 1},     // music backup
  {0x13D3, 2},    // pause
  {0x13F1, 1},    // vertical scroll enable
  {0x13FD, 9},    // L/R scrolling, scroll-up flag, about-to-warp flag
  {0x142A, 6},    // camera scroll positions
  {0x1434, 1},    // keyhole timer
  {0x1492, 2},    // victory pose, end of level
  {0x1DFB, 1},    // music
};

enum { kCtxSize = 128, kGuardSize = 32 };

typedef struct CoopState {
  uint8 active[kCoopMaxPlayers];
  uint8 respawn_pending;
  uint8 reserved[11];
  uint8 ctx[kCoopMaxPlayers][kCtxSize];
} CoopState;

_Static_assert(sizeof(CoopState) <= kRamCoopSize, "co-op state must fit the RAM extension area");

static CoopState *Coop(void) {
  return (CoopState *)(g_ram + kSnesRamSize + kRamCoopOffset);
}

static int RangesSize(const RamRange *r, int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    s += r[i].len;
  return s;
}

static void SaveRanges(const RamRange *r, int n, uint8 *dst) {
  for (int i = 0; i < n; i++, dst += r[i - 1].len)
    memcpy(dst, g_ram + r[i].addr, r[i].len);
}

static void LoadRanges(const RamRange *r, int n, const uint8 *src) {
  for (int i = 0; i < n; i++, src += r[i - 1].len)
    memcpy(g_ram + r[i].addr, src, r[i].len);
}

// Pointer to the byte for SNES address `addr` inside a saved player context.
static uint8 *CtxByte(uint8 *ctx, uint16 addr) {
  for (int i = 0; i < (int)countof(kPlayerRam); ctx += kPlayerRam[i++].len)
    if (addr >= kPlayerRam[i].addr && addr < kPlayerRam[i].addr + kPlayerRam[i].len)
      return ctx + (addr - kPlayerRam[i].addr);
  return NULL;
}

static uint16 CtxWord(uint8 *ctx, uint16 addr) {
  uint8 *p = CtxByte(ctx, addr);
  return p ? (uint16)(p[0] | p[1] << 8) : 0;
}

int Coop_ActiveExtraPlayers(void) {
  int n = 0;
  for (int p = 1; p < kCoopMaxPlayers; p++)
    n += Coop()->active[p] != 0;
  return n;
}

// Places extra player p next to the main player (whose state is in RAM),
// small, standing, with a short invincibility flash.
static void SpawnAtMainPlayer(int p) {
  CoopState *cs = Coop();
  uint8 *ctx = cs->ctx[p];
  SaveRanges(kPlayerRam, countof(kPlayerRam), ctx);
  // Start from a clean state rather than a copy of whatever the main player
  // is doing (carrying, riding Yoshi, flying, in a pipe...).
  static const uint16 kClear[] = { 0x71, 0x72, 0x73, 0x74, 0x75, 0x7A, 0x7B, 0x7C, 0x7D, 0x88, 0x89,
    0x13E5, 0x13ED, 0x1407, 0x1408, 0x1409, 0x140D, 0x1419, 0x1470, 0x1471, 0x148F, 0x1490,
    0x187A, 0x18C2, 0x191C, 0x1891 };
  for (size_t i = 0; i < countof(kClear); i++) {
    uint8 *b = CtxByte(ctx, kClear[i]);
    if (b) *b = 0;
  }
  *CtxByte(ctx, 0x19) = 0;  // small
  uint16 x = player_xpos + (p & 1 ? -20 : 20);
  CtxByte(ctx, 0x94)[0] = (uint8)x, CtxByte(ctx, 0x94)[1] = x >> 8;
  *CtxByte(ctx, 0x1497) = 0x60;  // hurt-flash invincibility
  cs->active[p] = 1;
}

void Coop_OnLevelLoad(void) {
  // The main player isn't placed yet; rejoin on the first in-level frame.
  Coop()->respawn_pending = 1;
}

// Runs one extra player's frame with its state swapped into RAM.
static void RunExtraPlayer(int p, uint8 hold1, uint8 press1, uint8 hold2, uint8 press2) {
  CoopState *cs = Coop();
  uint8 main_ctx[kCtxSize], before[kCtxSize], guard[kGuardSize];
  SaveRanges(kPlayerRam, countof(kPlayerRam), main_ctx);
  SaveRanges(kWorldGuard, countof(kWorldGuard), guard);
  LoadRanges(kPlayerRam, countof(kPlayerRam), cs->ctx[p]);

  io_controller_hold1 = hold1, io_controller_press1 = press1;
  io_controller_hold2 = hold2, io_controller_press2 = press2;
  // PlayerDraw normally computes these; the screen-edge clamp needs them.
  player_on_screen_pos_x = player_xpos - mirror_current_layer1_xpos;
  player_on_screen_pos_y = player_ypos - mirror_current_layer1_ypos;
  UpdateCurrentPlayerPositionRAM();
  for (uint16 a = 0x1496; a <= 0x14A6; a++)  // this player's timers
    if (g_ram[a])
      g_ram[a]--;

  SaveRanges(kPlayerRam, countof(kPlayerRam), before);
  GameMode14_InLevel_HandlePlayerState();

  uint8 state = player_current_state;
  bool died = (state == 9);
  // Phase 1: pipes, warps, doors and cutscene states are main-player only.
  bool blocked = !died && state >= 5;
  if (blocked) {
    LoadRanges(kPlayerRam, countof(kPlayerRam), before);
    player_xspeed = player_yspeed = 0;
  }
  bool fell_off = (int16)(player_ypos - mirror_current_layer1_ypos) > 0x120;

  SaveRanges(kPlayerRam, countof(kPlayerRam), cs->ctx[p]);
  LoadRanges(kWorldGuard, countof(kWorldGuard), guard);
  LoadRanges(kPlayerRam, countof(kPlayerRam), main_ctx);

  if (died || fell_off)
    SpawnAtMainPlayer(p);
}

void Coop_UpdateExtraPlayers(void) {
  CoopState *cs = Coop();
  if (!g_coop_enabled || flag_two_player_game || misc_game_mode != 0x14) {
    memset(cs->active, 0, sizeof(cs->active));
    return;
  }
  if (cs->respawn_pending) {
    cs->respawn_pending = 0;
    for (int p = 1; p < kCoopMaxPlayers; p++)
      if (cs->active[p])
        SpawnAtMainPlayer(p);
  }
  // Player 2 uses the second controller: Start joins, Start+Select leaves.
  uint8 hold1 = io_controller_hold1_copyp2, press1 = io_controller_press1_copyp2;
  uint8 hold2 = io_controller_hold2_copyp2, press2 = io_controller_press2_copyp2;
  if (!cs->active[1]) {
    if (press1 & 0x10)
      SpawnAtMainPlayer(1);
    return;
  }
  if ((hold1 & 0x30) == 0x30 && (press1 & 0x30)) {
    cs->active[1] = 0;
    return;
  }
  // The world is frozen (main player dying, powering up, keyhole...).
  if (flag_sprites_locked || player_freeze_player_flag || player_current_state == 9)
    return;
  RunExtraPlayer(1, hold1, press1, hold2, press2);
}

static const uint8 (*PickFrame(uint8 *ctx))[kCoopArtW] {
  if (*CtxByte(ctx, 0x73))  // ducking
    return kCoopArt_duck;
  if (*CtxByte(ctx, 0x72))  // in the air
    return kCoopArt_jump;
  if (*CtxByte(ctx, 0x7B) == 0)  // standing still
    return kCoopArt_idle;
  return (*CtxByte(ctx, 0x13DB) & 1) ? kCoopArt_walk1 : kCoopArt_walk2;
}

void Coop_DrawOverlay(uint8 *pixels, int pitch, int width, int height) {
  CoopState *cs = Coop();
  uint8 mode = misc_game_mode;
  if (!g_coop_enabled || (mode != 0x13 && mode != 0x14 && mode != 0x15))
    return;
  int margin = (width - 256) / 2;
  for (int p = 1; p < kCoopMaxPlayers; p++) {
    if (!cs->active[p])
      continue;
    uint8 *ctx = cs->ctx[p];
    // Flicker while invincible after a hit or respawn
    if (*CtxByte(ctx, 0x1497) && (counter_global_frames & 2))
      continue;
    const uint8 (*art)[kCoopArtW] = PickFrame(ctx);
    bool face_right = *CtxByte(ctx, 0x76) != 0;
    // The player's box is 16x32 from (xpos, ypos); stand the art on its bottom.
    int sx = (int16)(CtxWord(ctx, 0x94) - mirror_current_layer1_xpos) + margin;
    int sy = (int16)(CtxWord(ctx, 0x96) - mirror_current_layer1_ypos) + 32 - kCoopArtH;
    const uint32 *pal = kCoopPlayerColors[(p - 1) % 3];
    for (int y = 0; y < kCoopArtH; y++) {
      int py = sy + y;
      if ((unsigned)py >= (unsigned)height)
        continue;
      uint32 *row = (uint32 *)(pixels + py * pitch);
      for (int x = 0; x < kCoopArtW; x++) {
        uint8 c = art[y][face_right ? x : kCoopArtW - 1 - x];
        int px = sx + x;
        if (!c || (unsigned)px >= (unsigned)width)
          continue;
        // Keys: . k w p l f | b h d s S
        row[px] = c <= 5 ? kCoopCommonColors[c - 1] : pal[c - 6];
      }
    }
  }
}
