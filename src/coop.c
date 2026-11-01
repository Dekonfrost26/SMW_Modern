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
#include <stdio.h>
#include <stdlib.h>
#include "coop_art.h"

bool g_coop_enabled;
uint8 g_coop_current_player;
uint16 g_coop_pad_input[kCoopMaxPlayers];

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

enum {
  kCtxSize = 128,
  kGuardSize = 32,
  kJoinFlash = 0x20,     // frames of invincibility when joining
  kRespawnFlash = 0x60,  // ... and when coming back after dying
};

typedef struct CoopState {
  uint8 active[kCoopMaxPlayers];
  uint8 respawn_pending;
  uint8 reserved[11];
  uint8 carry_owner[kSprSlotsMax];  // player carrying each normal sprite slot
  uint8 ctx[kCoopMaxPlayers][kCtxSize];
  // Phase 3
  uint8 bubble[kCoopMaxPlayers];        // frames spent in a bubble (0 = not in one)
  uint16 bubble_x[kCoopMaxPlayers], bubble_y[kCoopMaxPlayers];  // bubble position (player box, world)
  uint8 pad_prev[kCoopMaxPlayers][2];   // previous held buttons, controllers 3-4
  // Phase 4: the main player's bubble (frames, 0 = not in one) and position
  uint8 main_bubble;
  uint16 main_bubble_x, main_bubble_y;
} CoopState;

_Static_assert(sizeof(CoopState) <= kRamCoopSize, "co-op state must fit the RAM extension area");

static CoopState *Coop(void) {
  return (CoopState *)(g_ram + kSnesRamSize + kRamCoopOffset);
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
// small and standing, flashing (invincible) for `flash` frames.
static void SpawnAtMainPlayer(int p, uint8 flash) {
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
  // P2 joins ahead of the main player (toward the rest of the level), P3 behind, P4 further ahead
  static const int8 kJoinOffset[kCoopMaxPlayers] = { 0, 20, -20, 40 };
  uint16 x = player_xpos + kJoinOffset[p];
  CtxByte(ctx, 0x94)[0] = (uint8)x, CtxByte(ctx, 0x94)[1] = x >> 8;
  *CtxByte(ctx, 0x1497) = flash;  // hurt-flash invincibility
  cs->active[p] = 1;
}

void Coop_OnLevelLoad(void) {
  // The main player isn't placed yet; rejoin on the first in-level frame.
  Coop()->respawn_pending = 1;
  memset(Coop()->carry_owner, 0, sizeof(Coop()->carry_owner));
}

// ---------------------------------------------------------------------------
// Bubbles: a player who dies or falls floats back to the main player and pops
// out with B/Y/A/X once close (or by itself after a while).

enum {
  kBubbleMinFrames = 40,    // before a bubble can pop
  kBubbleAutoPop = 240,     // pops by itself after this many frames near the main player
  kBubbleSpeed = 2,         // pixels per frame
};

static void EnterBubble(int p) {
  CoopState *cs = Coop();
  uint8 *ctx = cs->ctx[p];
  int16 x = CtxWord(ctx, 0x94), y = CtxWord(ctx, 0x96);
  int16 cy = mirror_current_layer1_ypos;
  // Start somewhere visible (falling players are below the screen)
  if (y > cy + 0xB0) y = cy + 0xB0;
  if (y < cy - 8) y = cy - 8;
  cs->bubble_x[p] = x, cs->bubble_y[p] = y;
  cs->bubble[p] = 1;
  *CtxByte(ctx, 0x71) = 0;  // not dying any more
  // Carried items fall out of the bubble
  for (int k = 0; k < kSprSlotsMax; k++)
    if (cs->carry_owner[k] == p)
      cs->carry_owner[k] = 0, SPR(spr_current_status, k) = (SPR(spr_current_status, k) == 0x0B) ? 9 : SPR(spr_current_status, k);
}

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static bool IsLivingExtra(int p) {
  CoopState *cs = Coop();
  return cs->active[p] && !cs->bubble[p] && *CtxByte(cs->ctx[p], 0x71) != 9;
}

// Position of the living player nearest to (x, y), excluding extra player
// `skip`; the main player counts unless they're in a bubble. False if nobody
// is alive.
static bool NearestLivingPlayer(int x, int y, int skip, int *ox, int *oy) {
  CoopState *cs = Coop();
  int best = 1 << 30;
  if (!cs->main_bubble) {
    best = abs((int16)player_xpos - x) + abs((int16)player_ypos - y);
    *ox = (int16)player_xpos, *oy = (int16)player_ypos;
  }
  for (int p = 1; p < kCoopMaxPlayers; p++) {
    if (p == skip || !IsLivingExtra(p))
      continue;
    int px = (int16)CtxWord(cs->ctx[p], 0x94), py = (int16)CtxWord(cs->ctx[p], 0x96);
    int d = abs(px - x) + abs(py - y);
    if (d < best)
      best = d, *ox = px, *oy = py;
  }
  return best != (1 << 30);
}

static void UpdateBubble(int p, uint8 press1, uint8 press2) {
  CoopState *cs = Coop();
  static const int8 kJoinOffsetX[kCoopMaxPlayers] = { 0, 20, -20, 40 };
  int bx = (int16)cs->bubble_x[p], by = (int16)cs->bubble_y[p];
  // Float toward the main player, or the nearest living player if the main
  // player is in a bubble too
  int tx = (int16)player_xpos, ty = (int16)player_ypos;
  if (cs->main_bubble && !NearestLivingPlayer(bx, by, p, &tx, &ty))
    tx = (int16)player_xpos, ty = (int16)player_ypos;
  tx += kJoinOffsetX[p], ty -= 24;
  bx += Clamp(tx - bx, -kBubbleSpeed, kBubbleSpeed);
  by += Clamp(ty - by, -kBubbleSpeed, kBubbleSpeed);
  cs->bubble_x[p] = bx, cs->bubble_y[p] = by;
  if (cs->bubble[p] < 255)
    cs->bubble[p]++;
  bool close = abs(tx - bx) < 20 && abs(ty - by) < 24;
  if (cs->bubble[p] >= kBubbleMinFrames && close && (((press1 | press2) & 0xC0) || cs->bubble[p] >= kBubbleAutoPop) &&
      !cs->main_bubble) {
    cs->bubble[p] = 0;
    SpawnAtMainPlayer(p, kJoinFlash);
    // Pop where the bubble is
    uint8 *ctx = cs->ctx[p];
    CtxByte(ctx, 0x94)[0] = (uint8)bx, CtxByte(ctx, 0x94)[1] = (uint8)(bx >> 8);
    CtxByte(ctx, 0x96)[0] = (uint8)by, CtxByte(ctx, 0x96)[1] = (uint8)(by >> 8);
    *CtxByte(ctx, 0x7D) = 0xD0;  // small hop out of the bubble
    *CtxByte(ctx, 0x72) = 0x0B;  // in the air
    io_sound_ch1 = 0x01;
  }
}

// Controller input as the player code sees it (same merge as PollJoypadInputs:
// A and X also count as B/Y in hold1/press1).
typedef struct PadIn { uint8 hold1, press1, hold2, press2; } PadIn;

static PadIn ReadPad(int p) {
  PadIn in;
  if (p == 1) {
    uint8 h2 = io_controller_hold2_copyp2, pr2 = io_controller_press2_copyp2;
    in.hold1 = io_controller_hold1_copyp2 | (h2 & 0xC0);
    in.press1 = io_controller_press1_copyp2 | (pr2 & 0x40);
    in.hold2 = h2, in.press2 = pr2;
    return in;
  }
  // Controllers 3-4: 12-bit joypad word -> JOY registers -> hold/press bytes
  uint16 bits = g_coop_pad_input[p], r = 0;
  for (int i = 0; i < 16; i++, bits >>= 1)
    r = (uint16)(r * 2 + (bits & 1));
  uint8 h1 = r >> 8, h2 = r & 0xF0;
  uint8 *prev = Coop()->pad_prev[p];
  uint8 p1 = h1 & (prev[0] ^ h1), p2 = h2 & (prev[1] ^ h2);
  prev[0] = h1, prev[1] = h2;
  in.hold1 = h1 | (h2 & 0xC0), in.press1 = p1 | (p2 & 0x40);
  in.hold2 = h2, in.press2 = p2;
  return in;
}

uint16 Coop_NearestPlayerPos(uint16 sx, uint16 sy, uint16 main_x, uint16 main_y, bool current_copy, bool want_y) {
  CoopState *cs = Coop();
  if (!g_coop_enabled || g_coop_current_player)
    return want_y ? main_y : main_x;
  uint16 bx = main_x, by = main_y;
  // A main player in a bubble isn't a target
  int best = cs->main_bubble ? (1 << 30) : abs((int16)(main_x - sx)) + abs((int16)(main_y - sy));
  for (int p = 1; p < kCoopMaxPlayers; p++) {
    if (!cs->active[p] || cs->bubble[p])
      continue;
    uint8 *ctx = cs->ctx[p];
    uint16 x = CtxWord(ctx, current_copy ? 0xD1 : 0x94), y = CtxWord(ctx, current_copy ? 0xD3 : 0x96);
    int d = abs((int16)(x - sx)) + abs((int16)(y - sy));
    if (d < best)
      best = d, bx = x, by = y;
  }
  return want_y ? by : bx;
}

// Swapping an extra player's state in and out of RAM, with the world guard.
typedef struct PlayerSwap {
  uint8 main_ctx[kCtxSize];
  uint8 guard[kGuardSize];
} PlayerSwap;

static void EnterPlayer(PlayerSwap *s, int p) {
  SaveRanges(kPlayerRam, countof(kPlayerRam), s->main_ctx);
  SaveRanges(kWorldGuard, countof(kWorldGuard), s->guard);
  LoadRanges(kPlayerRam, countof(kPlayerRam), Coop()->ctx[p]);
  // PlayerDraw normally computes these for the main player; the screen-edge
  // clamp and solid-sprite checks read them.
  player_on_screen_pos_x = player_xpos - mirror_current_layer1_xpos;
  player_on_screen_pos_y = player_ypos - mirror_current_layer1_ypos;
  g_coop_current_player = p;
}

static void LeavePlayer(PlayerSwap *s, int p) {
  SaveRanges(kPlayerRam, countof(kPlayerRam), Coop()->ctx[p]);
  LoadRanges(kWorldGuard, countof(kWorldGuard), s->guard);
  LoadRanges(kPlayerRam, countof(kPlayerRam), s->main_ctx);
  g_coop_current_player = 0;
}

// Runs one extra player's frame with its state swapped into RAM.
static void RunExtraPlayer(int p, uint8 hold1, uint8 press1, uint8 hold2, uint8 press2) {
  CoopState *cs = Coop();
  uint8 before[kCtxSize];
  PlayerSwap swap;
  EnterPlayer(&swap, p);

  io_controller_hold1 = hold1, io_controller_press1 = press1;
  io_controller_hold2 = hold2, io_controller_press2 = press2;
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

  LeavePlayer(&swap, p);
  (void)cs;
  if (died || fell_off)
    EnterBubble(p);
}

void Coop_FanOut(CoopSpriteFn *fn, uint8 k, bool normal_sprite) {
  CoopState *cs = Coop();
  if (!g_coop_enabled || g_coop_current_player)
    return;
  for (int p = 1; p < kCoopMaxPlayers; p++) {
    if (!cs->active[p] || cs->bubble[p] || *CtxByte(cs->ctx[p], 0x71) == 9)
      continue;
    PlayerSwap swap;
    EnterPlayer(&swap, p);
    uint8 status_before = normal_sprite ? SPR(spr_current_status, k) : 0;
    uint8 r = fn(k);
#ifdef COOP_DEBUG
    if (normal_sprite) {
      int dx = (int16)(player_xpos - GetSprXPos(k)), dy = (int16)(player_ypos - GetSprYPos(k));
      if (dx > -40 && dx < 40 && dy > -48 && dy < 48)
        fprintf(stderr, "coop: P%d near sprite %d id %02x dx %d dy %d r %d status %02x->%02x state %d hurt %d layer %d/%d cd %d\n",
                p + 1, k, SPR(spr_spriteid, k), dx, dy, r, status_before, SPR(spr_current_status, k), player_current_state,
                timer_player_hurt, player_current_layer_priority, SPR(spr_table1632, k), SPR(spr_decrementing_table154c, k));
    }
#else
    (void)r;
#endif
    if (normal_sprite && SPR(spr_current_status, k) == 0x0B && status_before != 0x0B)
      cs->carry_owner[k] = p;
    bool died = player_current_state == 9;
    LeavePlayer(&swap, p);
    if (died)
      EnterBubble(p);
  }
}

void Coop_SetCarryOwner(uint8 k, uint8 player) {
  if (k < kSprSlotsMax)
    Coop()->carry_owner[k] = player;
}

bool Coop_RunAsCarrier(CoopSpriteFn *fn, uint8 k) {
  CoopState *cs = Coop();
  uint8 p = k < kSprSlotsMax ? cs->carry_owner[k] : 0;
  if (!p || g_coop_current_player || !g_coop_enabled) {
    fn(k);
    return true;
  }
  if (p >= kCoopMaxPlayers || !cs->active[p]) {
    cs->carry_owner[k] = 0;
    return false;
  }
  PlayerSwap swap;
  EnterPlayer(&swap, p);
  fn(k);
  bool died = player_current_state == 9;
  LeavePlayer(&swap, p);
  if (died)
    EnterBubble(p);
  return true;
}

void Coop_BeginSpriteFrame(void) {
  CoopState *cs = Coop();
  if (!g_coop_enabled)
    return;
  for (int p = 1; p < kCoopMaxPlayers; p++) {
    if (!cs->active[p])
      continue;
    uint8 *ctx = cs->ctx[p];
    // Same as the start of ProcessNormalSprites for the main player
    *CtxByte(ctx, 0x1470) = *CtxByte(ctx, 0x148F);
    *CtxByte(ctx, 0x148F) = 0;
    *CtxByte(ctx, 0x1471) = 0;
    *CtxByte(ctx, 0x18C2) = 0;
  }
}

// ---------------------------------------------------------------------------
// Phase 4: main player bubble

static bool g_allow_main_death;

bool Coop_TryBubbleMainPlayer(void) {
  CoopState *cs = Coop();
  if (!g_coop_enabled || g_coop_current_player || g_allow_main_death || misc_game_mode != 0x14 || cs->main_bubble)
    return false;
  // Time up ends the level for everyone
  if (!(counter_timer_ones | counter_timer_tens | counter_timer_hundreds))
    return false;
  bool someone_alive = false;
  for (int p = 1; p < kCoopMaxPlayers; p++)
    someone_alive |= IsLivingExtra(p);
  if (!someone_alive)
    return false;
  int16 x = player_xpos, y = player_ypos, cy = mirror_current_layer1_ypos;
  cs->main_bubble_x = x;
  cs->main_bubble_y = Clamp(y, cy - 8, cy + 0xB0);
  cs->main_bubble = 1;
  // Inert while in the bubble: state 0x0D does nothing, and sprite contact
  // ignores players whose state isn't 0.
  player_current_state = 0x0D;
  player_xspeed = player_yspeed = 0;
  player_spin_jump_flag = 0;
  player_riding_yoshi_flag = 0;
  for (int k = 0; k < SprNumSlots(); k++)  // drop anything carried
    if (SPR(spr_current_status, k) == 0x0B && cs->carry_owner[k] == 0)
      SPR(spr_current_status, k) = 9;
  player_carrying_something_flag1 = player_carrying_something_flag2 = 0;
  io_sound_ch1 = 0x04;
  return true;
}

static void UpdateMainBubble(void) {
  CoopState *cs = Coop();
  int bx = (int16)cs->main_bubble_x, by = (int16)cs->main_bubble_y, tx, ty;
  if (!NearestLivingPlayer(bx, by, 0, &tx, &ty)) {
    // Nobody left: the original death (and level restart) happens now.
    cs->main_bubble = 0;
    player_current_state = 0;
    g_allow_main_death = true;
    DamagePlayer_Kill();
    g_allow_main_death = false;
    return;
  }
  ty -= 24;
  bx += Clamp(tx - bx, -kBubbleSpeed, kBubbleSpeed);
  by += Clamp(ty - by, -kBubbleSpeed, kBubbleSpeed);
  cs->main_bubble_x = bx, cs->main_bubble_y = by;
  if (cs->main_bubble < 255)
    cs->main_bubble++;
  // The main player (and so the camera) rides the bubble
  player_xpos = bx, player_ypos = by;
  player_xspeed = player_yspeed = 0;
  player_current_state = 0x0D;
  bool close = abs(tx - bx) < 24 && abs(ty - by) < 28;
  if (cs->main_bubble >= kBubbleMinFrames && close &&
      (((io_controller_press1 | io_controller_press2) & 0xC0) || cs->main_bubble >= kBubbleAutoPop)) {
    cs->main_bubble = 0;
    player_current_state = 0;
    player_yspeed = 0xD0;  // small hop out of the bubble
    player_in_air_flag = 0x0B;
    timer_player_hurt = kJoinFlash;
    io_sound_ch1 = 0x01;
  }
}

void Coop_UpdateExtraPlayers(void) {
  CoopState *cs = Coop();
  if (!g_coop_enabled || flag_two_player_game || misc_game_mode != 0x14) {
    memset(cs->active, 0, sizeof(cs->active));
    memset(cs->bubble, 0, sizeof(cs->bubble));
    if (cs->main_bubble && player_current_state == 0x0D)
      player_current_state = 0;
    cs->main_bubble = 0;
    return;
  }
  if (cs->main_bubble)
    UpdateMainBubble();
  if (cs->respawn_pending) {
    cs->respawn_pending = 0;
    if (cs->main_bubble && player_current_state == 0x0D)
      player_current_state = 0;
    cs->main_bubble = 0;
    for (int p = 1; p < kCoopMaxPlayers; p++)
      if (cs->active[p])
        cs->bubble[p] = 0, SpawnAtMainPlayer(p, kJoinFlash);
  }
  // The world is frozen (main player dying, powering up, keyhole...).
  bool frozen = flag_sprites_locked || player_freeze_player_flag || player_current_state == 9;
  for (int p = 1; p < kCoopMaxPlayers; p++) {
    PadIn in = ReadPad(p);  // every frame, to keep press detection right
    // Start joins; Start+Select leaves.
    if (!cs->active[p]) {
      if (in.press1 & 0x10)
        cs->bubble[p] = 0, SpawnAtMainPlayer(p, kJoinFlash);
      continue;
    }
    if ((in.hold1 & 0x30) == 0x30 && (in.press1 & 0x30)) {
      cs->active[p] = 0, cs->bubble[p] = 0;
      continue;
    }
    if (frozen)
      continue;
    if (cs->bubble[p])
      UpdateBubble(p, in.press1, in.press2);
    else
      RunExtraPlayer(p, in.hold1, in.press1, in.hold2, in.press2);
  }
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

static uint32 Blend(uint32 dst, uint32 src, int a256) {
  uint32 rb = ((src & 0xff00ff) * a256 + (dst & 0xff00ff) * (256 - a256)) >> 8;
  uint32 g = ((src & 0x00ff00) * a256 + (dst & 0x00ff00) * (256 - a256)) >> 8;
  return (rb & 0xff00ff) | (g & 0x00ff00);
}

static void DrawArt(uint8 *pixels, int pitch, int width, int height, const uint8 (*art)[kCoopArtW],
                    int sx, int sy, bool face_right, const uint32 *pal) {
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

// A soap-bubble ring around (cx, cy): translucent fill, light rim, highlight.
static void DrawBubble(uint8 *pixels, int pitch, int width, int height, int cx, int cy, int r) {
  for (int y = -r; y <= r; y++) {
    int py = cy + y;
    if ((unsigned)py >= (unsigned)height)
      continue;
    uint32 *row = (uint32 *)(pixels + py * pitch);
    for (int x = -r; x <= r; x++) {
      int px = cx + x, d2 = x * x + y * y;
      if ((unsigned)px >= (unsigned)width || d2 > r * r)
        continue;
      if (d2 >= (r - 1) * (r - 1))
        row[px] = 0xd8f4ff;                              // rim
      else if (x < -r / 3 && y < -r / 3 && d2 >= (r - 4) * (r - 4) && d2 < (r - 2) * (r - 2))
        row[px] = 0xffffff;                              // highlight
      else
        row[px] = Blend(row[px], 0xa8e0ff, 64);          // tinted inside
    }
  }
}

void Coop_DrawOverlay(uint8 *pixels, int pitch, int width, int height) {
  CoopState *cs = Coop();
  uint8 mode = misc_game_mode;
  if (!g_coop_enabled || (mode != 0x13 && mode != 0x14 && mode != 0x15))
    return;
  int margin = (width - 256) / 2;
  if (cs->main_bubble) {
    // The game draws the main player; add the bubble around them
    int cx = (int16)(player_xpos - mirror_current_layer1_xpos) + margin + 8;
    int cy = (int16)(player_ypos - mirror_current_layer1_ypos) + 20 + ((cs->main_bubble >> 3) & 1);
    DrawBubble(pixels, pitch, width, height, cx, cy, 16);
  }
  for (int p = 1; p < kCoopMaxPlayers; p++) {
    if (!cs->active[p])
      continue;
    uint8 *ctx = cs->ctx[p];
    const uint32 *pal = kCoopPlayerColors[(p - 1) % 3];
    if (cs->bubble[p]) {
      // Gently bobbing bubble with the player curled up inside
      int bob = ((cs->bubble[p] >> 3) & 1);
      int sx = (int16)(cs->bubble_x[p] - mirror_current_layer1_xpos) + margin;
      int sy = (int16)(cs->bubble_y[p] - mirror_current_layer1_ypos) + 32 - kCoopArtH + bob;
      DrawArt(pixels, pitch, width, height, kCoopArt_duck, sx, sy, true, pal);
      DrawBubble(pixels, pitch, width, height, sx + 8, sy + 12, 14);
      continue;
    }
    // Flicker while invincible after a hit or respawn
    if (*CtxByte(ctx, 0x1497) && (counter_global_frames & 2))
      continue;
    // The player's box is 16x32 from (xpos, ypos); stand the art on its bottom.
    int sx = (int16)(CtxWord(ctx, 0x94) - mirror_current_layer1_xpos) + margin;
    int sy = (int16)(CtxWord(ctx, 0x96) - mirror_current_layer1_ypos) + 32 - kCoopArtH;
    DrawArt(pixels, pitch, width, height, PickFrame(ctx), sx, sy, *CtxByte(ctx, 0x76) != 0, pal);
  }
}
