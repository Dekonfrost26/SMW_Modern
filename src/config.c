#include "config.h"
#include "types.h"
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "util.h"

enum {
  kKeyMod_ScanCode = 0x200,
  kKeyMod_Alt = 0x400,
  kKeyMod_Shift = 0x800,
  kKeyMod_Ctrl = 0x1000,
};

Config g_config;

#define REMAP_SDL_KEYCODE(key) ((key) & SDLK_SCANCODE_MASK ? kKeyMod_ScanCode : 0) | (key) & (kKeyMod_ScanCode - 1)
#define _(x) REMAP_SDL_KEYCODE(x)
#define S(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Shift
#define A(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Alt
#define C(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Ctrl
#define N 0
static const uint16 kDefaultKbdControls[kKeys_Total] = {
  0,
  // Controls
  _(SDLK_UP), _(SDLK_DOWN), _(SDLK_LEFT), _(SDLK_RIGHT), _(SDLK_RSHIFT), _(SDLK_RETURN), _(SDLK_x), _(SDLK_z), _(SDLK_s), _(SDLK_a), _(SDLK_c), _(SDLK_v),
  // ControlsP2
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  // LoadState
  _(SDLK_F1), _(SDLK_F2), _(SDLK_F3), _(SDLK_F4), _(SDLK_F5), _(SDLK_F6), _(SDLK_F7), _(SDLK_F8), _(SDLK_F9), _(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // SaveState
  S(SDLK_F1), S(SDLK_F2), S(SDLK_F3), S(SDLK_F4), S(SDLK_F5), S(SDLK_F6), S(SDLK_F7), S(SDLK_F8), S(SDLK_F9), S(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // Replay State
  C(SDLK_F1), C(SDLK_F2), C(SDLK_F3), C(SDLK_F4), C(SDLK_F5), C(SDLK_F6), C(SDLK_F7), C(SDLK_F8), C(SDLK_F9), C(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // Load Ref State
  _(SDLK_1), _(SDLK_2), _(SDLK_3), _(SDLK_4), _(SDLK_5), _(SDLK_6), _(SDLK_7), _(SDLK_8), _(SDLK_9), _(SDLK_0), _(SDLK_MINUS), _(SDLK_EQUALS), _(SDLK_BACKSPACE), N, N, N, N, N, N, N,
  // Replay Ref State
  C(SDLK_1), C(SDLK_2), C(SDLK_3), C(SDLK_4), C(SDLK_5), C(SDLK_6), C(SDLK_7), C(SDLK_8), C(SDLK_9), C(SDLK_0), C(SDLK_MINUS), C(SDLK_EQUALS), C(SDLK_BACKSPACE), N, N, N, N, N, N, N,
  // CheatLife, CheatJump, ToggleWhichFrame,
  _(SDLK_w), _(SDLK_q), S(SDLK_r),
  // ClearKeyLog, StopReplay, Fullscreen, Reset, Pause, PauseDimmed, Turbo, ReplayTurbo, WindowBigger, WindowSmaller, DisplayPerf, ToggleRenderer
  _(SDLK_k), _(SDLK_l), A(SDLK_RETURN), C(SDLK_r), S(SDLK_p), _(SDLK_p), _(SDLK_TAB), _(SDLK_t), N, N, _(SDLK_f), _(SDLK_r),
  // VolumeUp VolumeDown
  0, 0,
};
#undef _
#undef A
#undef C
#undef S
#undef N

typedef struct KeyNameId {
  const char *name;
  uint16 id, size;
} KeyNameId;

#define M(n) {#n, kKeys_##n, kKeys_##n##_Last - kKeys_##n + 1}
#define S(n) {#n, kKeys_##n, 1}
static const KeyNameId kKeyNameId[] = {
  {"Null", kKeys_Null, 65535},
  M(Controls), M(ControlsP2),
  M(Load), M(Save), M(Replay), M(LoadRef), M(ReplayRef),
  S(CheatLife), S(CheatJump), S(ToggleWhichFrame),
  S(ClearKeyLog), S(StopReplay), S(Fullscreen), S(Reset),
  S(Pause), S(PauseDimmed), S(Turbo), S(ReplayTurbo), S(WindowBigger), S(WindowSmaller), S(VolumeUp), S(VolumeDown), S(DisplayPerf), S(ToggleRenderer),
};
#undef S
#undef M
typedef struct KeyMapHashEnt {
  uint16 key, cmd, next;
} KeyMapHashEnt;

static uint16 keymap_hash_first[255];
static KeyMapHashEnt *keymap_hash;
static int keymap_hash_size;
static bool has_keynameid[countof(kKeyNameId)];

static bool KeyMapHash_Add(uint16 key, uint16 cmd) {
  if (!key)
    return false;

  if (cmd == kKeys_Controls)
    g_config.has_keyboard_controls |= 1;
  else if (cmd == kKeys_ControlsP2)
    g_config.has_keyboard_controls |= 2;

  if ((keymap_hash_size & 0xff) == 0) {
    if (keymap_hash_size > 10000)
      Die("Too many keys");
    keymap_hash = (KeyMapHashEnt*)realloc(keymap_hash, sizeof(KeyMapHashEnt) * (keymap_hash_size + 256));
  }
  int i = keymap_hash_size++;
  KeyMapHashEnt *ent = &keymap_hash[i];
  ent->key = key;
  ent->cmd = cmd;
  ent->next = 0;
  int j = (uint32)key % 255;

  uint16 *cur = &keymap_hash_first[j];
  while (*cur) {
    KeyMapHashEnt *ent = &keymap_hash[*cur - 1];
    if (ent->key == key) {
      if (ent->cmd == 0) {  // slot freed by a rebind; reuse it
        ent->cmd = cmd;
        keymap_hash_size--;
        return true;
      }
      keymap_hash_size--;
      return false;
    }
    cur = &ent->next;
  }
  *cur = i + 1;
  return true;
}

static int KeyMapHash_Find(uint16 key) {
  int i = keymap_hash_first[key % 255];
  while (i) {
    KeyMapHashEnt *ent = &keymap_hash[i - 1];
    if (ent->key == key)
      return ent->cmd;
    i = ent->next;
  }
  return 0;
}

int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod) {
  if (code & ~(SDLK_SCANCODE_MASK | 0x1ff))
    return 0;
  int key = 0;
  if (code != SDLK_LALT && code != SDLK_RALT)
    key |= mod & KMOD_ALT ? kKeyMod_Alt : 0;
  if (code != SDLK_LCTRL && code != SDLK_RCTRL)
    key |= mod & KMOD_CTRL ? kKeyMod_Ctrl : 0;
  if (code != SDLK_LSHIFT && code != SDLK_RSHIFT)
    key |= mod & KMOD_SHIFT ? kKeyMod_Shift : 0;
  key |= REMAP_SDL_KEYCODE(code);
  int cmd = KeyMapHash_Find(key);
  if (cmd == 0 && (key & (kKeyMod_Alt | kKeyMod_Ctrl | kKeyMod_Shift))) {
    // Allow SNES buttons to work while a modifier key is held, e.g. when
    // Shift or Ctrl itself is bound to run/jump.
    int plain = KeyMapHash_Find(REMAP_SDL_KEYCODE(code));
    if ((plain >= kKeys_Controls && plain <= kKeys_Controls_Last) ||
        (plain >= kKeys_ControlsP2 && plain <= kKeys_ControlsP2_Last))
      cmd = plain;
  }
  return cmd;
}

static void ParseKeyArray(char *value, int cmd, int size) {
  char *s;
  int i = 0;
  for (; i < size && (s = NextDelim(&value, ',')) != NULL; i++, cmd += (cmd != 0)) {
    if (*s == 0)
      continue;
    int key_with_mod = 0;
    for (;;) {
      if (StringStartsWithNoCase(s, "Shift+")) {
        key_with_mod |= kKeyMod_Shift, s += 6;
      } else if (StringStartsWithNoCase(s, "Ctrl+")) {
        key_with_mod |= kKeyMod_Ctrl, s += 5;
      } else if (StringStartsWithNoCase(s, "Alt+")) {
        key_with_mod |= kKeyMod_Alt, s += 4;
      } else {
        break;
      }
    }
    SDL_Keycode key = SDL_GetKeyFromName(s);
    if (key == SDLK_UNKNOWN) {
      fprintf(stderr, "Unknown key: '%s'\n", s);
      continue;
    }
    if (!KeyMapHash_Add(key_with_mod | REMAP_SDL_KEYCODE(key), cmd))
      fprintf(stderr, "Duplicate key: '%s'\n", s);
  }
}

typedef struct GamepadMapEnt {
  uint32 modifiers;
  uint16 cmd, next;
} GamepadMapEnt;

static uint16 joymap_first[kGamepadBtn_Count * 2];  // 2 gamepads
static GamepadMapEnt *joymap_ents;
static int joymap_size, joymap_cap;
static uint16 joymap_free;  // 1-based index of the first reusable entry
static uint8 has_assigned_joypad_controls;

static int CountBits32(uint32 n) {
  int count = 0;
  for (; n != 0; count++)
    n &= (n - 1);
  return count;
}

static void GamepadMap_Add(int button, uint32 modifiers, uint16 cmd) {
  int i;
  if (joymap_free) {
    // Reuse an entry released by a rebind
    i = joymap_free - 1;
    joymap_free = joymap_ents[i].next;
  } else {
    if (joymap_size == joymap_cap) {
      if (joymap_cap >= 4096)
        Die("Too many joypad keys");
      joymap_cap = joymap_cap ? joymap_cap * 2 : 64;
      joymap_ents = (GamepadMapEnt*)realloc(joymap_ents, sizeof(GamepadMapEnt) * joymap_cap);
      if (!joymap_ents) Die("realloc failure");
    }
    i = joymap_size++;
  }
  uint16 *p = &joymap_first[button];
  // Insert it as early as possible but before after any entry with more modifiers.
  int cb = CountBits32(modifiers);
  while (*p && cb < CountBits32(joymap_ents[*p - 1].modifiers))
    p = &joymap_ents[*p - 1].next;
  GamepadMapEnt *ent = &joymap_ents[i];
  ent->modifiers = modifiers;
  ent->cmd = cmd;
  ent->next = *p;
  *p = i + 1;
}

int FindCmdForGamepadButton(int button, uint32 modifiers) {
  GamepadMapEnt *ent;
  for(int e = joymap_first[button]; e != 0; e = ent->next) {
    ent = &joymap_ents[e - 1];
    if ((modifiers & ent->modifiers) == ent->modifiers)
      return ent->cmd;
  }
  return 0;
}

static int ParseGamepadButtonName(const char **value) {
  const char *s = *value;
  // Longest substring first
  static const char *const kGamepadKeyNames[] = {
    "Back", "Guide", "Start", "L3", "R3",
    "L1", "R1", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "L2", "R2",
    "Lb", "Rb", "A", "B", "X", "Y"
  };
  static const uint8 kGamepadKeyIds[] = {
    kGamepadBtn_Back, kGamepadBtn_Guide, kGamepadBtn_Start, kGamepadBtn_L3, kGamepadBtn_R3,
    kGamepadBtn_L1, kGamepadBtn_R1, kGamepadBtn_DpadUp, kGamepadBtn_DpadDown, kGamepadBtn_DpadLeft, kGamepadBtn_DpadRight, kGamepadBtn_L2, kGamepadBtn_R2,
    kGamepadBtn_L1, kGamepadBtn_R1, kGamepadBtn_A, kGamepadBtn_B, kGamepadBtn_X, kGamepadBtn_Y,
  };
  for (size_t i = 0; i != countof(kGamepadKeyNames); i++) {
    const char *r = StringStartsWithNoCase(s, kGamepadKeyNames[i]);
    if (r) {
      *value = r;
      return kGamepadKeyIds[i];
    }
  }
  return kGamepadBtn_Invalid;
}

static const uint8 kDefaultGamepadCmds[] = {
  kGamepadBtn_DpadUp, kGamepadBtn_DpadDown, kGamepadBtn_DpadLeft, kGamepadBtn_DpadRight, kGamepadBtn_Back, kGamepadBtn_Start,
  kGamepadBtn_B, kGamepadBtn_A, kGamepadBtn_Y, kGamepadBtn_X, kGamepadBtn_L1, kGamepadBtn_R1,
};

static void ParseGamepadArray(int gamepad, char *value, int cmd, int size) {
  char *s;
  int i = 0;
  for (; i < size && (s = NextDelim(&value, ',')) != NULL; i++, cmd += (cmd != 0)) {
    if (*s == 0)
      continue;
    int gamepad_cur = gamepad;
    uint32 modifiers = 0;
    const char *ss = s;
    for (;;) {
      int button = ParseGamepadButtonName(&ss);
      if (button == kGamepadBtn_Invalid) BAD: {
        fprintf(stderr, "Unknown gamepad button: '%s'\n", s);
        break;
      }
      while (*ss == ' ' || *ss == '\t') ss++;
      if (*ss == '+') {
        ss++;
        modifiers |= 1 << button;
      } else if (*ss == 0) {
        GamepadMap_Add(button + gamepad_cur * kGamepadBtn_Count, modifiers, cmd);
        break;
      } else
        goto BAD;
    }
  }
}

static void RegisterDefaultKeys(void) {
  for (int i = 1; i < countof(kKeyNameId); i++) {
    if (!has_keynameid[i]) {
      int size = kKeyNameId[i].size, k = kKeyNameId[i].id;
      for (int j = 0; j < size; j++, k++)
        KeyMapHash_Add(kDefaultKbdControls[k], k);
    }
  }
  if (!(has_assigned_joypad_controls & 1)) {
    for (int i = 0; i < countof(kDefaultGamepadCmds); i++)
      GamepadMap_Add(kDefaultGamepadCmds[i], 0, kKeys_Controls + i);
  }
  if (!(has_assigned_joypad_controls & 2)) {
    for (int i = 0; i < countof(kDefaultGamepadCmds); i++)
      GamepadMap_Add(kDefaultGamepadCmds[i] + kGamepadBtn_Count, 0, kKeys_ControlsP2 + i);
  }
}

static int GetIniSection(const char *s) {
  if (StringEqualsNoCase(s, "[KeyMap]"))
    return 0;
  if (StringEqualsNoCase(s, "[Graphics]"))
    return 1;
  if (StringEqualsNoCase(s, "[Sound]"))
    return 2;
  if (StringEqualsNoCase(s, "[General]"))
    return 3;
  if (StringEqualsNoCase(s, "[Features]"))
    return 4;
  if (StringEqualsNoCase(s, "[GamepadMap]"))
    return 5;
  return -1;
}

bool ParseBool(const char *value, bool *result) {
  bool rv = false;
  switch (*value++ | 32) {
  case '0': if (*value == 0) break; return false;
  case 'f': if (StringEqualsNoCase(value, "alse")) break; return false;
  case 'n': if (StringEqualsNoCase(value, "o")) break; return false;
  case 'o':
    rv = (*value | 32) == 'n';
    if (StringEqualsNoCase(value, rv ? "n" : "ff")) break;
    return false;
  case '1': rv = true; if (*value == 0) break; return false;
  case 'y': rv = true; if (StringEqualsNoCase(value, "es")) break; return false;
  case 't': rv = true; if (StringEqualsNoCase(value, "rue")) break; return false;
  default: return false;
  }
  if (result) {
    *result = rv;
    return true;
  }
  return rv;
}

static bool ParseBoolBit(const char *value, uint32 *data, uint32 mask) {
  bool tmp;
  if (!ParseBool(value, &tmp))
    return false;
  *data = *data & ~mask | (tmp ? mask : 0);
  return true;
}

static bool HandleIniConfig(int section, const char *key, char *value) {
  if (section == 0) {
    for (int i = 0; i < countof(kKeyNameId); i++) {
      if (StringEqualsNoCase(key, kKeyNameId[i].name)) {
        has_keynameid[i] = true;
        ParseKeyArray(value, kKeyNameId[i].id, kKeyNameId[i].size);
        return true;
      }
    }
  } else if (section == 5) {
    if (StringEqualsNoCase(key, "EnableGamepad1")) {
      return ParseBool(value, &g_config.enable_gamepad[0]);
    } else if (StringEqualsNoCase(key, "EnableGamepad2")) {
      return ParseBool(value, &g_config.enable_gamepad[1]);
    } else {
      for (int i = 0; i < countof(kKeyNameId); i++) {
        if (StringEqualsNoCase(key, kKeyNameId[i].name)) {
          int id = kKeyNameId[i].id;
          has_assigned_joypad_controls |= (id == kKeys_Controls) ? 1 : (id == kKeys_ControlsP2) ? 2 : 0;
          ParseGamepadArray(id == kKeys_ControlsP2 ? 1 : 0, value, kKeyNameId[i].id, kKeyNameId[i].size);
          return true;
        }
      }
    }
  } else if (section == 1) {
    if (StringEqualsNoCase(key, "IntegerScaling")) {
      return ParseBool(value, &g_config.integer_scaling);
    } else if (StringEqualsNoCase(key, "VSync")) {
      return ParseBool(value, &g_config.vsync);
    } else if (StringEqualsNoCase(key, "ShowFPS")) {
      return ParseBool(value, &g_config.show_fps);
    } else if (StringEqualsNoCase(key, "WindowSize")) {
      char *s;
      if (StringEqualsNoCase(value, "Auto")){
        g_config.window_width  = 0;
        g_config.window_height = 0;
        return true;
      }
      while ((s = NextDelim(&value, 'x')) != NULL) {
        if(g_config.window_width == 0) {
          g_config.window_width = atoi(s);
        } else {
          g_config.window_height = atoi(s);
          return true;
        }
      }
    } else if (StringEqualsNoCase(key, "EnhancedMode7")) {
      return ParseBool(value, &g_config.enhanced_mode7);
    } else if (StringEqualsNoCase(key, "NewRenderer")) {
      return ParseBool(value, &g_config.new_renderer);
    } else if (StringEqualsNoCase(key, "IgnoreAspectRatio")) {
      return ParseBool(value, &g_config.ignore_aspect_ratio);
    } else if (StringEqualsNoCase(key, "Fullscreen")) {
      g_config.fullscreen = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "WindowScale")) {
      g_config.window_scale = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "OutputMethod")) {
      g_config.output_method = StringEqualsNoCase(value, "SDL-Software") ? kOutputMethod_SDLSoftware :
                               StringEqualsNoCase(value, "OpenGL") ? kOutputMethod_OpenGL : kOutputMethod_SDL;
      return true;
    } else if (StringEqualsNoCase(key, "LinearFiltering")) {
      return ParseBool(value, &g_config.linear_filtering);
    } else if (StringEqualsNoCase(key, "NoSpriteLimits")) {
      return ParseBool(value, &g_config.no_sprite_limits);
    } else if (StringEqualsNoCase(key, "LinkGraphics")) {
      g_config.link_graphics = value;
      return true;
    } else if (StringEqualsNoCase(key, "Shader")) {
      g_config.shader = *value ? value : NULL;
      return true;
    }
  } else if (section == 2) {
    if (StringEqualsNoCase(key, "Volume")) {
      int v = atoi(value);
      g_config.volume = (uint8)(v < 0 ? 0 : v > 100 ? 100 : v);
      return true;
    } else if (StringEqualsNoCase(key, "EnableAudio")) {
      return ParseBool(value, &g_config.enable_audio);
    } else if (StringEqualsNoCase(key, "AudioFreq")) {
      g_config.audio_freq = (uint16)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "AudioChannels")) {
      g_config.audio_channels = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "AudioSamples")) {
      g_config.audio_samples = (uint16)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "EnableMSU")) {
        if (StringEqualsNoCase(value, "opuz"))
        g_config.enable_msu = kMsuEnabled_Opuz;
      else if (StringEqualsNoCase(value, "deluxe"))
        g_config.enable_msu = kMsuEnabled_MsuDeluxe;
      else if (StringEqualsNoCase(value, "deluxe-opuz"))
        g_config.enable_msu = kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz;
      else 
        return ParseBool(value, (bool*)&g_config.enable_msu);
      return true;
    } else if (StringEqualsNoCase(key, "MSUPath")) {
      g_config.msu_path = value;
      return true;
    } else if (StringEqualsNoCase(key, "MSUVolume")) {
      g_config.msuvolume = atoi(value);
      return true;
    } else if (StringEqualsNoCase(key, "ResumeMSU")) {
      return ParseBool(value, &g_config.resume_msu);
    }
  } else if (section == 3) {
    if (StringEqualsNoCase(key, "Autosave")) {
      g_config.autosave = (bool)strtol(value, (char **)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "SavePlaythrough")) {
      return ParseBool(value, &g_config.save_playthrough);
    } else if (StringEqualsNoCase(key, "DisplayPerfInTitle")) {
      return ParseBool(value, &g_config.display_perf_title);
    } else if (StringEqualsNoCase(key, "DisableFrameDelay")) {
      return ParseBool(value, &g_config.disable_frame_delay);
    } else if (StringEqualsNoCase(key, "VerifyAgainstRom")) {
      return ParseBool(value, &g_config.verify_against_rom);
    } else if (StringEqualsNoCase(key, "PauseOnFocusLoss")) {
      return ParseBool(value, &g_config.pause_on_focus_loss);
    }
  } else if (section == 4) {
  }
  return false;
}

static bool ParseOneConfigFile(const char *filename, int depth) {
  char *filedata = (char*)ReadWholeFile(filename, NULL), *p;
  if (!filedata)
    return false;
  
  int section = -2;
  g_config.memory_buffer = filedata;

  for (int lineno = 1; (p = NextLineStripComments(&filedata)) != NULL; lineno++) {
    if (*p == 0)
      continue; // empty line
    if (*p == '[') {
      section = GetIniSection(p);
      if (section < 0)
        fprintf(stderr, "%s:%d: Invalid .ini section %s\n", filename, lineno, p);
    } else if (*p == '!' && SkipPrefix(p + 1, "include ")) {
      char *tt = p + 8;
      char *new_filename = ReplaceFilenameWithNewPath(filename, NextPossiblyQuotedString(&tt));
      if (depth > 10 || !ParseOneConfigFile(new_filename, depth + 1))
        fprintf(stderr, "Warning: Unable to read %s\n", new_filename);
      free(new_filename);
    } else if (section == -2) {
      fprintf(stderr, "%s:%d: Expecting [section]\n", filename, lineno);
    } else {
      char *v = SplitKeyValue(p);
      if (v == NULL) {
        fprintf(stderr, "%s:%d: Expecting 'key=value'\n", filename, lineno);
        continue;
      }
      if (section >= 0 && !HandleIniConfig(section, p, v))
        fprintf(stderr, "%s:%d: Can't parse '%s'\n", filename, lineno, p);
    }
  }
  return true;
}

static char g_config_filename[1024];

void ParseConfigFile(const char *filename) {
  g_config.msuvolume = 100;  // default msu volume, 100%
  g_config.save_playthrough = true;
  g_config.vsync = true;
  g_config.volume = 100;
  g_config.pause_on_focus_loss = true;

  if (filename != NULL || !ParseOneConfigFile("smw.user.ini", 0)) {
    if (filename == NULL)
      filename = "smw.ini";
    if (!ParseOneConfigFile(filename, 0))
      fprintf(stderr, "Warning: Unable to read config file %s\n", filename);
  } else {
    filename = "smw.user.ini";
  }
  snprintf(g_config_filename, sizeof(g_config_filename), "%s", filename);
  RegisterDefaultKeys();
}

// ---------------------------------------------------------------------------
// Keyboard rebinding for the 12 SNES buttons

uint16 Config_PackSdlKey(SDL_Keycode code) {
  if (code & ~(SDLK_SCANCODE_MASK | 0x1ff))
    return 0;
  return REMAP_SDL_KEYCODE(code);
}

static SDL_Keycode UnpackSdlKey(uint16 key) {
  return (key & kKeyMod_ScanCode ? SDLK_SCANCODE_MASK : 0) | (key & (kKeyMod_ScanCode - 1));
}

static KeyMapHashEnt *FindEntForCmd(int cmd) {
  for (int i = 0; i < keymap_hash_size; i++)
    if (keymap_hash[i].cmd == cmd)
      return &keymap_hash[i];
  return NULL;
}

static KeyMapHashEnt *FindEntForKey(uint16 key) {
  int i = keymap_hash_first[key % 255];
  while (i) {
    KeyMapHashEnt *ent = &keymap_hash[i - 1];
    if (ent->key == key)
      return ent;
    i = ent->next;
  }
  return NULL;
}

uint16 Config_GetControlKey(int button) {
  KeyMapHashEnt *ent = FindEntForCmd(kKeys_Controls + button);
  return ent ? ent->key : 0;
}

const char *Config_ControlKeyName(int button) {
  uint16 key = Config_GetControlKey(button);
  return key ? SDL_GetKeyName(UnpackSdlKey(key)) : "";
}

static bool g_keys_dirty, g_pad_dirty;

// A key is only usable if its name survives a round trip through the .ini
// (names containing ',' or '#' would be split or treated as a comment).
static bool KeyIsBindable(SDL_Keycode code) {
  if (code == SDLK_UNKNOWN || code == SDLK_ESCAPE || !Config_PackSdlKey(code))
    return false;
  const char *name = SDL_GetKeyName(code);
  if (!name || !*name || strchr(name, ',') || strchr(name, '#'))
    return false;
  return SDL_GetKeyFromName(name) == code;
}

static int SetControlKeyPacked(int button, uint16 key) {
  int cmd = kKeys_Controls + button;
  KeyMapHashEnt *mine = FindEntForCmd(cmd);
  KeyMapHashEnt *other = FindEntForKey(key);
  if (mine && mine == other)
    return 0;
  int stolen = 0;
  uint16 old_key = mine ? mine->key : 0;
  if (mine)
    mine->cmd = 0;
  if (other) {
    int other_cmd = other->cmd;
    other->cmd = cmd;
    if (other_cmd >= kKeys_Controls && other_cmd <= kKeys_Controls_Last) {
      // The key belonged to another SNES button: give that button our old key.
      if (old_key)
        FindEntForKey(old_key)->cmd = other_cmd;
    } else {
      stolen = other_cmd;
    }
  } else {
    KeyMapHash_Add(key, cmd);
  }
  g_keys_dirty = true;
  return stolen;
}

int Config_SetControlKey(int button, SDL_Keycode code) {
  if (button < 0 || button >= 12 || !KeyIsBindable(code))
    return -1;
  return SetControlKeyPacked(button, Config_PackSdlKey(code));
}

void Config_ResetControlKeys(void) {
  for (int i = 0; i < 12; i++)
    SetControlKeyPacked(i, kDefaultKbdControls[kKeys_Controls + i]);
}

// ---------------------------------------------------------------------------
// Writing settings back to the .ini, keeping comments and layout intact

typedef struct IniDoc {
  char **lines;
  int n, cap;
} IniDoc;

static void IniDoc_Insert(IniDoc *d, int at, char *line) {
  if (d->n == d->cap) {
    d->cap = d->cap ? d->cap * 2 : 256;
    d->lines = (char **)realloc(d->lines, sizeof(char *) * d->cap);
  }
  memmove(d->lines + at + 1, d->lines + at, sizeof(char *) * (d->n - at));
  d->lines[at] = line;
  d->n++;
}

static char *DupStr(const char *s) {
  size_t n = strlen(s) + 1;
  char *r = (char *)malloc(n);
  memcpy(r, s, n);
  return r;
}

static const char *SkipWs(const char *s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

// Returns true if the line is "Key = ..." (or "#Key = ..." when allow_comment)
static bool LineIsKey(const char *line, const char *key, bool commented) {
  const char *s = SkipWs(line);
  if (commented) {
    if (*s != '#') return false;
    s = SkipWs(s + 1);
  }
  size_t kl = strlen(key);
  if (!StringStartsWithNoCase(s, key)) return false;
  s = SkipWs(s + kl);
  return *s == '=';
}

static bool LineIsSection(const char *line) {
  return *SkipWs(line) == '[';
}

static void IniDoc_Set(IniDoc *d, const char *section, const char *key, const char *value) {
  char buf[512];
  snprintf(buf, sizeof(buf), "%s = %s", key, value);
  int sec = -1, sec_end;
  for (int i = 0; i < d->n; i++) {
    if (LineIsSection(d->lines[i]) && StringStartsWithNoCase(SkipWs(d->lines[i]), section)) {
      sec = i;
      break;
    }
  }
  if (sec < 0) {
    IniDoc_Insert(d, d->n, DupStr(""));
    IniDoc_Insert(d, d->n, DupStr(section));
    sec = d->n - 1;
  }
  for (sec_end = sec + 1; sec_end < d->n && !LineIsSection(d->lines[sec_end]); sec_end++) {}
  for (int pass = 0; pass < 2; pass++) {
    for (int i = sec + 1; i < sec_end; i++) {
      if (LineIsKey(d->lines[i], key, pass == 1)) {
        free(d->lines[i]);
        d->lines[i] = DupStr(buf);
        return;
      }
    }
  }
  // Insert after the last non-empty line of the section
  int at = sec_end;
  while (at > sec + 1 && *SkipWs(d->lines[at - 1]) == 0) at--;
  IniDoc_Insert(d, at, DupStr(buf));
}

// ---------------------------------------------------------------------------
// Gamepad rebinding for the 12 SNES buttons (same layout for both gamepads)

static const char *const kPadButtonIniNames[kGamepadBtn_Count] = {
  "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3",
  "Lb", "Rb", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "L2", "R2",
};

int Config_GetPadButton(int button) {
  int cmd = kKeys_Controls + button;
  for (int b = 0; b < kGamepadBtn_Count; b++) {
    for (int e = joymap_first[b]; e != 0; e = joymap_ents[e - 1].next) {
      GamepadMapEnt *ent = &joymap_ents[e - 1];
      if (ent->cmd == cmd && ent->modifiers == 0)
        return b;
    }
  }
  return -1;
}

static void UnlinkPadControls(void) {
  // Remove the plain (no modifier) SNES button entries of both gamepads.
  for (int b = 0; b < kGamepadBtn_Count * 2; b++) {
    uint16 *p = &joymap_first[b];
    while (*p) {
      GamepadMapEnt *ent = &joymap_ents[*p - 1];
      bool is_ctrl = ent->modifiers == 0 &&
          ((ent->cmd >= kKeys_Controls && ent->cmd <= kKeys_Controls_Last) ||
           (ent->cmd >= kKeys_ControlsP2 && ent->cmd <= kKeys_ControlsP2_Last));
      if (is_ctrl) {
        uint16 idx = *p;
        *p = ent->next;
        ent->cmd = 0;
        ent->next = joymap_free;
        joymap_free = idx;
      } else {
        p = &ent->next;
      }
    }
  }
}

static void ApplyPadLayout(const int layout[12]) {
  UnlinkPadControls();
  for (int i = 0; i < 12; i++) {
    if (layout[i] < 0)
      continue;
    GamepadMap_Add(layout[i], 0, kKeys_Controls + i);
    GamepadMap_Add(layout[i] + kGamepadBtn_Count, 0, kKeys_ControlsP2 + i);
  }
}

void Config_SetPadButton(int button, int pad_btn) {
  if (button < 0 || button >= 12 || pad_btn < 0 || pad_btn >= kGamepadBtn_Count || pad_btn == kGamepadBtn_Guide)
    return;
  int layout[12];
  for (int i = 0; i < 12; i++)
    layout[i] = Config_GetPadButton(i);
  if (layout[button] == pad_btn)
    return;
  for (int i = 0; i < 12; i++)
    if (layout[i] == pad_btn)
      layout[i] = layout[button];  // swap with the button that had it
  layout[button] = pad_btn;
  ApplyPadLayout(layout);
  g_pad_dirty = true;
}

void Config_ResetPadButtons(void) {
  int layout[12];
  for (int i = 0; i < 12; i++)
    layout[i] = kDefaultGamepadCmds[i];
  ApplyPadLayout(layout);
  g_pad_dirty = true;
}

#ifdef _WIN32
__declspec(dllimport) int __stdcall MoveFileExA(const char *existing, const char *new_name, unsigned long flags);
#endif

static bool ReplaceFile_(const char *src, const char *dst) {
#ifdef _WIN32
  return MoveFileExA(src, dst, 1 /* MOVEFILE_REPLACE_EXISTING */) != 0;
#else
  return rename(src, dst) == 0;
#endif
}

bool Config_Save(void) {
  size_t length;
  char *data = (char *)ReadWholeFile(g_config_filename, &length);
  IniDoc d = { 0 };
  bool crlf = false;
  if (data) {
    char *p = data;
    for (;;) {
      char *nl = strchr(p, '\n');
      if (nl) *nl = 0;
      size_t l = strlen(p);
      if (l && p[l - 1] == '\r') { p[l - 1] = 0; crlf = true; }
      if (nl || *p)
        IniDoc_Insert(&d, d.n, DupStr(p));
      if (!nl) break;
      p = nl + 1;
    }
    free(data);
  }

  char buf[256];
  static const char *const kOutputNames[] = { "SDL", "SDL-Software", "OpenGL" };
  snprintf(buf, sizeof(buf), "%d", g_config.fullscreen);
  IniDoc_Set(&d, "[Graphics]", "Fullscreen", buf);
  snprintf(buf, sizeof(buf), "%d", g_config.window_scale);
  IniDoc_Set(&d, "[Graphics]", "WindowScale", buf);
  IniDoc_Set(&d, "[Graphics]", "IgnoreAspectRatio", g_config.ignore_aspect_ratio ? "1" : "0");
  IniDoc_Set(&d, "[Graphics]", "IntegerScaling", g_config.integer_scaling ? "1" : "0");
  IniDoc_Set(&d, "[Graphics]", "NoSpriteLimits", g_config.no_sprite_limits ? "1" : "0");
  IniDoc_Set(&d, "[Graphics]", "OutputMethod", kOutputNames[g_config.output_method <= 2 ? g_config.output_method : 0]);
  IniDoc_Set(&d, "[Graphics]", "LinearFiltering", g_config.linear_filtering ? "1" : "0");
  IniDoc_Set(&d, "[Graphics]", "VSync", g_config.vsync ? "1" : "0");
  IniDoc_Set(&d, "[Graphics]", "ShowFPS", g_config.show_fps ? "1" : "0");

  snprintf(buf, sizeof(buf), "%d", g_config.volume);
  IniDoc_Set(&d, "[Sound]", "Volume", buf);
  snprintf(buf, sizeof(buf), "%d", g_config.audio_freq);
  IniDoc_Set(&d, "[Sound]", "AudioFreq", buf);
  snprintf(buf, sizeof(buf), "%d", g_config.audio_samples);
  IniDoc_Set(&d, "[Sound]", "AudioSamples", buf);

  IniDoc_Set(&d, "[General]", "Autosave", g_config.autosave ? "1" : "0");
  IniDoc_Set(&d, "[General]", "PauseOnFocusLoss", g_config.pause_on_focus_loss ? "1" : "0");

  // Control lines are only rewritten when changed, so hand-edited layouts
  // (e.g. a different player 2 gamepad layout) are left alone otherwise.
  if (g_keys_dirty) {
    buf[0] = 0;
    for (int i = 0; i < 12; i++) {
      size_t l = strlen(buf);
      snprintf(buf + l, sizeof(buf) - l, "%s%s", i ? ", " : "", Config_ControlKeyName(i));
    }
    IniDoc_Set(&d, "[KeyMap]", "Controls", buf);
  }
  if (g_pad_dirty) {
    buf[0] = 0;
    for (int i = 0; i < 12; i++) {
      int b = Config_GetPadButton(i);
      size_t l = strlen(buf);
      snprintf(buf + l, sizeof(buf) - l, "%s%s", i ? ", " : "", b >= 0 ? kPadButtonIniNames[b] : "");
    }
    IniDoc_Set(&d, "[GamepadMap]", "Controls", buf);
    IniDoc_Set(&d, "[GamepadMap]", "ControlsP2", buf);
  }

  // Write to a temporary file and swap it in, so a crash can't leave a half-written .ini.
  char tmp_name[sizeof(g_config_filename) + 8];
  snprintf(tmp_name, sizeof(tmp_name), "%s.tmp", g_config_filename);
  FILE *f = fopen(tmp_name, "wb");
  bool ok = f != NULL;
  for (int i = 0; i < d.n; i++) {
    if (f) {
      fputs(d.lines[i], f);
      fputs(crlf ? "\r\n" : "\n", f);
    }
    free(d.lines[i]);
  }
  free(d.lines);
  if (f && fclose(f) != 0)
    ok = false;
  if (ok)
    ok = ReplaceFile_(tmp_name, g_config_filename);
  if (!ok)
    remove(tmp_name);
  if (ok)
    g_keys_dirty = g_pad_dirty = false;
  return ok;
}
