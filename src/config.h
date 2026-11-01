#pragma once
#include "types.h"
#include <SDL_keycode.h>

enum {
  kKeys_Null,
  kKeys_Controls,
  kKeys_Controls_Last = kKeys_Controls + 11,

  kKeys_ControlsP2,
  kKeys_ControlsP2_Last = kKeys_ControlsP2 + 11,

  kKeys_Load,
  kKeys_Load_Last = kKeys_Load + 19,
  kKeys_Save,
  kKeys_Save_Last = kKeys_Save + 19,
  kKeys_Replay,
  kKeys_Replay_Last = kKeys_Replay + 19,
  kKeys_LoadRef,
  kKeys_LoadRef_Last = kKeys_LoadRef + 19,
  kKeys_ReplayRef,
  kKeys_ReplayRef_Last = kKeys_ReplayRef + 19,
  kKeys_CheatLife,
  kKeys_CheatJump,
  kKeys_ToggleWhichFrame,
  kKeys_ClearKeyLog,
  kKeys_StopReplay,
  kKeys_Fullscreen,
  kKeys_Reset,
  kKeys_Pause,
  kKeys_PauseDimmed,
  kKeys_Turbo,
  kKeys_ReplayTurbo,
  kKeys_WindowBigger,
  kKeys_WindowSmaller,
  kKeys_DisplayPerf,
  kKeys_ToggleRenderer,
  kKeys_VolumeUp,
  kKeys_VolumeDown,
  kKeys_Total,
};

enum {
  kOutputMethod_SDL,
  kOutputMethod_SDLSoftware,
  kOutputMethod_OpenGL,
};

typedef struct Config {
  int window_width;
  int window_height;
  bool enhanced_mode7;
  bool new_renderer;
  bool ignore_aspect_ratio;
  uint8 fullscreen;
  uint8 window_scale;
  bool enable_audio;
  bool linear_filtering;
  uint8 output_method;
  uint16 audio_freq;
  uint8 audio_channels;
  uint16 audio_samples;
  bool autosave;
  uint8 extended_aspect_ratio;
  bool extend_y;
  bool no_sprite_limits;
  bool display_perf_title;
  uint8 enable_msu;
  bool resume_msu;
  bool disable_frame_delay;
  bool save_playthrough;
  bool verify_against_rom;
  uint8 msuvolume;
  uint32 features0;

  const char *link_graphics;
  char *memory_buffer;
  const char *shader;
  const char *msu_path;

  bool enable_gamepad[2];

  // Native PC settings (editable from the in-game options menu)
  bool integer_scaling;
  bool vsync;
  bool show_fps;
  bool pause_on_focus_loss;
  uint8 volume;  // 0..100

  // Which players have keyboard controls
  uint8 has_keyboard_controls;
} Config;

enum {
  kMsuEnabled_Msu = 1,
  kMsuEnabled_MsuDeluxe = 2,
  kMsuEnabled_Opuz = 4,
};
enum {
  kGamepadBtn_Invalid = -1,
  kGamepadBtn_A,
  kGamepadBtn_B,
  kGamepadBtn_X,
  kGamepadBtn_Y,
  kGamepadBtn_Back,
  kGamepadBtn_Guide,
  kGamepadBtn_Start,
  kGamepadBtn_L3,
  kGamepadBtn_R3,
  kGamepadBtn_L1,
  kGamepadBtn_R1,
  kGamepadBtn_DpadUp,
  kGamepadBtn_DpadDown,
  kGamepadBtn_DpadLeft,
  kGamepadBtn_DpadRight,
  kGamepadBtn_L2,
  kGamepadBtn_R2,
  kGamepadBtn_Count,
};

extern Config g_config;

void ParseConfigFile(const char *filename);

// Options menu support
uint16 Config_GetControlKey(int button);           // button 0..11 (Up..R), returns packed key or 0
// Rebinds, swapping with another SNES button if needed. Returns the non-SNES command
// that previously used the key (now unbound), 0 if none, or -1 if the key can't be used.
int Config_SetControlKey(int button, SDL_Keycode key);
void Config_ResetControlKeys(void);
uint16 Config_PackSdlKey(SDL_Keycode code);
const char *Config_ControlKeyName(int button);     // SDL key name ("" if unbound)
bool Config_Save(void);                            // writes settings back to the loaded .ini
int Config_GetPadButton(int button);                // SNES button 0..11 -> kGamepadBtn_* (or -1)
void Config_SetPadButton(int button, int pad_btn);  // rebinds for both gamepads, swapping if needed
void Config_ResetPadButtons(void);
int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod);
int FindCmdForGamepadButton(int button, uint32 modifiers);
