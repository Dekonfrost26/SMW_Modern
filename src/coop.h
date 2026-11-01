#pragma once
#include "types.h"

// Simultaneous co-op (phase 1): extra players run the game's own player code
// with their state swapped in, and are drawn as an original character on top
// of the frame. See docs/COOP.md.

enum { kCoopMaxPlayers = 4 };  // player 0 is the game's own player

extern bool g_coop_enabled;

// Called once per in-level frame, after the main player's update.
void Coop_UpdateExtraPlayers(void);
// Called when a level or sublevel loads: extra players rejoin at the main player.
void Coop_OnLevelLoad(void);
// Draws the extra players onto a finished frame (ARGB8888, width includes
// any widescreen margins).
void Coop_DrawOverlay(uint8 *pixels, int pitch, int width, int height);
// Number of extra players currently in the game (0..3).
int Coop_ActiveExtraPlayers(void);
