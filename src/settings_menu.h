#pragma once
#include "types.h"
#include <SDL.h>

// In-game options menu drawn with the game's own status-bar font.

enum {
  kMenuNav_Up = 1,
  kMenuNav_Down = 2,
  kMenuNav_Left = 4,
  kMenuNav_Right = 8,
};

void SettingsMenu_Init(void);
bool SettingsMenu_IsOpen(void);
void SettingsMenu_Open(const uint8 *frame, int width, int height);
void SettingsMenu_Close(void);
// Keyboard event while the menu is open. game_cmd is the mapped command for the key (0 if none).
void SettingsMenu_OnKey(SDL_Keycode key, bool pressed, bool is_repeat, int game_cmd);
// Gamepad buttons while the menu is open (kGamepadBtn_*).
void SettingsMenu_OnGamepadButton(int button, bool pressed);
// Called once per displayed frame with the currently held gamepad directions (kMenuNav_*).
void SettingsMenu_Tick(uint32 held_dirs);
void SettingsMenu_Draw(uint8 *pixels, int pitch, int width, int height);

// Controller families, used to show the right button names
enum {
  kPadFamily_None,
  kPadFamily_Generic,
  kPadFamily_Xbox360,
  kPadFamily_XboxOne,
  kPadFamily_PS3,
  kPadFamily_PS4,
  kPadFamily_PS5,
  kPadFamily_Nintendo,
};

// Implemented in main.c
int Platform_GetPadFamily(void);  // family of the first connected controller
int Platform_GetDisplayMode(void);  // 0 = window, 1 = borderless fullscreen, 2 = exclusive
void Platform_SetDisplayMode(int mode);
int Platform_GetWindowScale(void);
int Platform_GetMaxWindowScale(void);
void Platform_SetWindowScale(int scale);
void Platform_ApplyVideoSettings(void);
void Platform_ApplySpriteLimit(void);
void Platform_ApplyVolume(void);
void Platform_ReopenAudio(void);
void Platform_ResetGame(void);
void Platform_Quit(void);
