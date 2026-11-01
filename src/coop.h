#pragma once
#include "types.h"

// Simultaneous co-op (phase 1): extra players run the game's own player code
// with their state swapped in, and are drawn as an original character on top
// of the frame. See docs/COOP.md.

enum { kCoopMaxPlayers = 4 };  // player 0 is the game's own player

extern bool g_coop_enabled;
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
