#pragma once
#include "types.h"

// Simultaneous co-op (phase 1): extra players run the game's own player code
// with their state swapped in, and are drawn as an original character on top
// of the frame. See docs/COOP.md.

enum { kCoopMaxPlayers = 4 };  // player 0 is the game's own player

extern bool g_coop_enabled;
// Buttons of controllers 3 and 4 (index 2 and 3), in the same 12-bit layout
// as player 1/2 input. Set by the host each frame; players 1 and 2 use the
// SNES joypad path instead.
extern uint16 g_coop_pad_input[kCoopMaxPlayers];
// Player whose state is currently swapped into RAM (0 = the game's own player).
extern uint8 g_coop_current_player;

// Called once per in-level frame, after the main player's update.
void Coop_UpdateExtraPlayers(void);
// Called when a level or sublevel loads: extra players rejoin at the main player.
void Coop_OnLevelLoad(void);
// Draws the extra players onto a finished frame (ARGB8888, width includes
// any widescreen margins).
void Coop_DrawOverlay(uint8 *pixels, int pitch, int width, int height);
// Number of extra players currently in the game (0..3).
int Coop_ActiveExtraPlayers(void);

// Phase 2: sprite interaction for every player.
typedef uint8 CoopSpriteFn(uint8 k);
// Runs fn(k) for each extra player with that player's state swapped in.
// Records which player picked a normal sprite up (status 0x0B) when
// `normal_sprite` is set. Does nothing while an extra player is swapped in.
void Coop_FanOut(CoopSpriteFn *fn, uint8 k, bool normal_sprite);
// Carried normal sprites remember who carries them (0 = the main player).
void Coop_SetCarryOwner(uint8 k, uint8 player);
// Runs fn(k) as the player carrying sprite k. Returns false if that player
// left the game (the caller should drop the sprite).
bool Coop_RunAsCarrier(CoopSpriteFn *fn, uint8 k);
// Per-frame player flag resets the sprite pass does for the main player.
void Coop_BeginSpriteFrame(void);

// Phase 3: the X or Y position of the player nearest to (sx, sy), for enemy
// AI that faces or chases "the player". main_x/main_y are the main player's
// position; current_copy picks the D1/D3 copy instead of 94/96. Returns the
// main player's value when no extra player is nearer (or while an extra
// player's own state is swapped in).
// Phase 4: when the main player would die while another player is still
// alive, they go into a bubble instead and the level continues. Returns true
// if that happened (the caller skips the death). Called from DamagePlayer_PitFall.
bool Coop_TryBubbleMainPlayer(void);

uint16 Coop_NearestPlayerPos(uint16 sx, uint16 sy, uint16 main_x, uint16 main_y, bool current_copy, bool want_y);
