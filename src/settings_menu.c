// In-game options menu for the PC port.
//
// Text is drawn with the game's own status-bar / message font, decoded at
// startup from graphics file 0x28 in smw_assets.dat (which is built from the
// user's ROM). Nothing from the game is embedded here; only the tile numbers
// of the characters are listed.

#include "settings_menu.h"
#include "config.h"
#include "assets/smw_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int DecompressTo(const uint8 *src, uint8 *pdst);

// ---------------------------------------------------------------------------
// Font

enum { kFontGfxFile = 0x28, kFontTiles = 128 };

static uint8 g_font[kFontTiles][64];  // palette index (0..3) per pixel
static bool g_font_ok;

// A few extra symbols the game font lacks, drawn in the same style
// (2px strokes, outlined automatically). Bit 7 = leftmost pixel.
typedef struct ExtraGlyph { char ch; uint8 rows[8]; } ExtraGlyph;
static const ExtraGlyph kExtraGlyphs[] = {
  { '>', { 0x00, 0x20, 0x30, 0x38, 0x30, 0x20, 0x00, 0x00 } },
  { '<', { 0x00, 0x10, 0x30, 0x70, 0x30, 0x10, 0x00, 0x00 } },
  { '@', { 0x40, 0x60, 0x70, 0x78, 0x70, 0x60, 0x40, 0x00 } },  // menu cursor
  { '/', { 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x00, 0x00 } },
  { ':', { 0x00, 0x30, 0x30, 0x00, 0x30, 0x30, 0x00, 0x00 } },
  { '?', { 0x78, 0xcc, 0x0c, 0x18, 0x30, 0x00, 0x30, 0x00 } },
  { '+', { 0x00, 0x30, 0x30, 0xfc, 0x30, 0x30, 0x00, 0x00 } },
  { '\'', { 0x30, 0x30, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 } },
  { '=', { 0x00, 0x00, 0x7c, 0x00, 0x7c, 0x00, 0x00, 0x00 } },
};
static uint8 g_extra[countof(kExtraGlyphs)][64];

static void BuildExtraGlyphs(void) {
  for (size_t g = 0; g < countof(kExtraGlyphs); g++) {
    uint8 *dst = g_extra[g];
    const uint8 *rows = kExtraGlyphs[g].rows;
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        int set = (rows[y] >> (7 - x)) & 1;
        int near = 0;
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            int yy = y + dy, xx = x + dx;
            if (yy >= 0 && yy < 8 && xx >= 0 && xx < 8 && ((rows[yy] >> (7 - xx)) & 1))
              near = 1;
          }
        }
        dst[y * 8 + x] = set ? 3 : near ? 1 : 0;
      }
    }
  }
}

void SettingsMenu_Init(void) {
  BuildExtraGlyphs();
  MemBlk blk = kGraphicsPtrs(kFontGfxFile);
  if (!blk.ptr || !blk.size)
    return;
  uint8 *buf = (uint8 *)calloc(0x10000, 1);
  if (!buf)
    return;
  DecompressTo(blk.ptr, buf);
  for (int t = 0; t < kFontTiles; t++) {
    for (int y = 0; y < 8; y++) {
      uint8 b0 = buf[t * 16 + y * 2], b1 = buf[t * 16 + y * 2 + 1];
      for (int x = 0; x < 8; x++)
        g_font[t][y * 8 + x] = ((b0 >> (7 - x)) & 1) | ((b1 >> (7 - x)) & 1) << 1;
    }
  }
  free(buf);
  g_font_ok = true;
}

static const uint8 *GlyphFor(char c) {
  if (c >= 'a' && c <= 'z')
    c -= 32;
  int tile = -1;
  if (c >= '0' && c <= '9') tile = c - '0';
  else if (c >= 'A' && c <= 'Z') tile = 0x0A + c - 'A';
  else if (c == '.') tile = 0x24;
  else if (c == ',') tile = 0x25;
  else if (c == '*') tile = 0x26;  // the game's "x" multiplication sign
  else if (c == '-') tile = 0x27;
  else if (c == '!') tile = 0x28;
  if (tile >= 0 && g_font_ok)
    return g_font[tile];
  if (c == ' ')
    return NULL;
  for (size_t i = 0; i < countof(kExtraGlyphs); i++)
    if (kExtraGlyphs[i].ch == c)
      return g_extra[i];
  for (size_t i = 0; i < countof(kExtraGlyphs); i++)
    if (kExtraGlyphs[i].ch == '?')
      return g_extra[i];
  return NULL;
}

// ---------------------------------------------------------------------------
// Drawing primitives (ARGB8888)

typedef struct Canvas { uint8 *pixels; int pitch, width, height; } Canvas;

static inline void PutPixel(Canvas *c, int x, int y, uint32 color) {
  if ((unsigned)x < (unsigned)c->width && (unsigned)y < (unsigned)c->height)
    *(uint32 *)(c->pixels + y * c->pitch + x * 4) = color;
}

static void FillRoundRect(Canvas *c, int x, int y, int w, int h, int r, uint32 color) {
  for (int j = 0; j < h; j++) {
    int edge = j < r ? r - 1 - j : (j >= h - r ? j - (h - r) : -1);
    int inset = edge >= 0 ? edge : 0;
    for (int i = inset; i < w - inset; i++)
      PutPixel(c, x + i, y + j, color);
  }
}

static uint32 Shade(uint32 color) {
  return ((color >> 1) & 0x7f7f7f) + ((color >> 2) & 0x3f3f3f);
}

static void DrawGlyph(Canvas *c, int x, int y, const uint8 *g, uint32 color) {
  if (!g)
    return;
  uint32 pal[4] = { 0, 0x000000, Shade(color), color };
  for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++)
      if (g[j * 8 + i])
        PutPixel(c, x + i, y + j, pal[g[j * 8 + i]]);
}

static int TextWidth(const char *s) {
  return (int)strlen(s) * 8;
}

static void DrawText(Canvas *c, int x, int y, const char *s, uint32 color) {
  for (; *s; s++, x += 8)
    DrawGlyph(c, x, y, GlyphFor(*s), color);
}

static void DrawTextCentered(Canvas *c, int cx, int y, const char *s, uint32 color) {
  DrawText(c, cx - TextWidth(s) / 2, y, s, color);
}

// ---------------------------------------------------------------------------
// Menu model

enum {
  kColor_Text = 0xf8f8f8,
  kColor_Selected = 0xf8d840,
  kColor_Title = 0xf8d840,
  kColor_Disabled = 0x707070,
  kColor_Help = 0x98b8f8,
  kColor_Rule = 0x484848,
  kColor_Frame = 0xf8f8f8,
  kColor_Fill = 0x000000,
};

enum {
  kPage_Main,
  kPage_Video,
  kPage_Audio,
  kPage_Controls,  // submenu: keyboard / gamepad
  kPage_Keyboard,
  kPage_Gamepad,
  kPage_Gameplay,
  kPage_ConfirmReset,
  kPage_ConfirmQuit,
  kPage_Count,
};

enum {
  kItem_Resume,
  kItem_Video,
  kItem_Audio,
  kItem_Controls,
  kItem_Gameplay,
  kItem_Reset,
  kItem_Quit,

  kItem_DisplayMode,
  kItem_Aspect,
  kItem_WindowSize,
  kItem_Scaling,
  kItem_Filter,
  kItem_VSync,
  kItem_Renderer,
  kItem_ShowFps,

  kItem_Volume,
  kItem_Latency,
  kItem_SampleRate,

  kItem_KeyboardPage,
  kItem_GamepadPage,

  kItem_Key0,
  kItem_Key11 = kItem_Key0 + 11,
  kItem_KeysDefault,

  kItem_Pad0,
  kItem_Pad11 = kItem_Pad0 + 11,
  kItem_PadDefault,

  kItem_SpriteLimit,
  kItem_Autosave,
  kItem_FocusPause,

  kItem_Back,
  kItem_No,
  kItem_Yes,
  kItem_Count,
};

static const char *const kItemLabels[kItem_Count] = {
  [kItem_Resume] = "CONTINUE",
  [kItem_Video] = "VIDEO",
  [kItem_Audio] = "SOUND",
  [kItem_Controls] = "CONTROLS",
  [kItem_Gameplay] = "GAMEPLAY",
  [kItem_Reset] = "RESET GAME",
  [kItem_Quit] = "QUIT GAME",
  [kItem_DisplayMode] = "DISPLAY",
  [kItem_Aspect] = "ASPECT",
  [kItem_WindowSize] = "WINDOW SIZE",
  [kItem_Scaling] = "SCALING",
  [kItem_Filter] = "FILTER",
  [kItem_VSync] = "VSYNC",
  [kItem_Renderer] = "RENDERER",
  [kItem_ShowFps] = "SHOW FPS",
  [kItem_Volume] = "VOLUME",
  [kItem_Latency] = "LATENCY",
  [kItem_SampleRate] = "SAMPLE RATE",
  [kItem_Key0 + 0] = "UP",
  [kItem_Key0 + 1] = "DOWN",
  [kItem_Key0 + 2] = "LEFT",
  [kItem_Key0 + 3] = "RIGHT",
  [kItem_Key0 + 4] = "SELECT",
  [kItem_Key0 + 5] = "START",
  [kItem_Key0 + 6] = "A BUTTON",
  [kItem_Key0 + 7] = "B BUTTON",
  [kItem_Key0 + 8] = "X BUTTON",
  [kItem_Key0 + 9] = "Y BUTTON",
  [kItem_Key0 + 10] = "L BUTTON",
  [kItem_Key0 + 11] = "R BUTTON",
  [kItem_KeysDefault] = "RESET KEYS",
  [kItem_KeyboardPage] = "KEYBOARD",
  [kItem_GamepadPage] = "GAMEPAD",
  [kItem_Pad0 + 0] = "UP",
  [kItem_Pad0 + 1] = "DOWN",
  [kItem_Pad0 + 2] = "LEFT",
  [kItem_Pad0 + 3] = "RIGHT",
  [kItem_Pad0 + 4] = "SELECT",
  [kItem_Pad0 + 5] = "START",
  [kItem_Pad0 + 6] = "A BUTTON",
  [kItem_Pad0 + 7] = "B BUTTON",
  [kItem_Pad0 + 8] = "X BUTTON",
  [kItem_Pad0 + 9] = "Y BUTTON",
  [kItem_Pad0 + 10] = "L BUTTON",
  [kItem_Pad0 + 11] = "R BUTTON",
  [kItem_PadDefault] = "RESET BUTTONS",
  [kItem_SpriteLimit] = "SPRITE LIMIT",
  [kItem_Autosave] = "AUTOSAVE",
  [kItem_FocusPause] = "AUTO PAUSE",
  [kItem_Back] = "BACK",
  [kItem_No] = "NO",
  [kItem_Yes] = "YES",
};

static const char *const kItemHelp[kItem_Count] = {
  [kItem_Resume] = "BACK TO THE GAME",
  [kItem_Video] = "WINDOW, SCALING, VSYNC",
  [kItem_Audio] = "VOLUME AND AUDIO OUTPUT",
  [kItem_Controls] = "KEYBOARD AND GAMEPAD",
  [kItem_KeyboardPage] = "REMAP KEYBOARD KEYS",
  [kItem_GamepadPage] = "REMAP CONTROLLER BUTTONS",
  [kItem_PadDefault] = "RESTORE DEFAULT BUTTONS",
  [kItem_Gameplay] = "SPRITES, AUTOSAVE, PAUSE",
  [kItem_Reset] = "RESTART FROM THE TITLE",
  [kItem_Quit] = "CLOSE THE GAME",
  [kItem_DisplayMode] = "ALT+ENTER ALSO TOGGLES",
  [kItem_Aspect] = "WIDER VIEW OF THE LEVEL",
  [kItem_WindowSize] = "CTRL+UP/DOWN ALSO WORKS",
  [kItem_Scaling] = "INTEGER = EVEN PIXELS",
  [kItem_Filter] = "SMOOTH BLURS THE PIXELS",
  [kItem_VSync] = "OFF MAY CAUSE TEARING",
  [kItem_Renderer] = "RESTART GAME TO APPLY",
  [kItem_ShowFps] = "FRAME RATE IN THE CORNER",
  [kItem_Volume] = "MUSIC AND SOUND EFFECTS",
  [kItem_Latency] = "RAISE IF SOUND CRACKLES",
  [kItem_SampleRate] = "MATCH YOUR SOUND DEVICE",
  [kItem_KeysDefault] = "RESTORE DEFAULT KEYS",
  [kItem_SpriteLimit] = "REMOVED = LESS FLICKER",
  [kItem_Autosave] = "RESUME WHERE YOU QUIT",
  [kItem_FocusPause] = "PAUSE WHEN UNFOCUSED",
  [kItem_Back] = "ESC OR B TO GO BACK",
};

static const uint8 kItemsMain[] = { kItem_Resume, kItem_Video, kItem_Audio, kItem_Controls, kItem_Gameplay, kItem_Reset, kItem_Quit };
static const uint8 kItemsVideo[] = { kItem_DisplayMode, kItem_Aspect, kItem_WindowSize, kItem_Scaling, kItem_Filter, kItem_VSync, kItem_Renderer, kItem_ShowFps, kItem_Back };
static const uint8 kItemsAudio[] = { kItem_Volume, kItem_Latency, kItem_SampleRate, kItem_Back };
static const uint8 kItemsControls[] = { kItem_KeyboardPage, kItem_GamepadPage, kItem_Back };
static const uint8 kItemsGamepad[] = {
  kItem_Pad0 + 0, kItem_Pad0 + 1, kItem_Pad0 + 2, kItem_Pad0 + 3, kItem_Pad0 + 4, kItem_Pad0 + 5,
  kItem_Pad0 + 6, kItem_Pad0 + 7, kItem_Pad0 + 8, kItem_Pad0 + 9, kItem_Pad0 + 10, kItem_Pad0 + 11,
  kItem_PadDefault, kItem_Back };
static const uint8 kItemsKeyboard[] = {
  kItem_Key0 + 0, kItem_Key0 + 1, kItem_Key0 + 2, kItem_Key0 + 3, kItem_Key0 + 4, kItem_Key0 + 5,
  kItem_Key0 + 6, kItem_Key0 + 7, kItem_Key0 + 8, kItem_Key0 + 9, kItem_Key0 + 10, kItem_Key0 + 11,
  kItem_KeysDefault, kItem_Back };
static const uint8 kItemsGameplay[] = { kItem_SpriteLimit, kItem_Autosave, kItem_FocusPause, kItem_Back };
static const uint8 kItemsConfirm[] = { kItem_No, kItem_Yes };

typedef struct PageDef {
  const char *title;
  const uint8 *items;
  uint8 count;
  bool centered;
} PageDef;

static const PageDef kPages[kPage_Count] = {
  [kPage_Main] = { "OPTIONS", kItemsMain, countof(kItemsMain), true },
  [kPage_Video] = { "VIDEO", kItemsVideo, countof(kItemsVideo), false },
  [kPage_Audio] = { "SOUND", kItemsAudio, countof(kItemsAudio), false },
  [kPage_Controls] = { "CONTROLS", kItemsControls, countof(kItemsControls), true },
  [kPage_Keyboard] = { "KEYBOARD", kItemsKeyboard, countof(kItemsKeyboard), false },
  [kPage_Gamepad] = { "GAMEPAD", kItemsGamepad, countof(kItemsGamepad), false },
  [kPage_Gameplay] = { "GAMEPLAY", kItemsGameplay, countof(kItemsGameplay), false },
  [kPage_ConfirmReset] = { "RESET GAME", kItemsConfirm, countof(kItemsConfirm), true },
  [kPage_ConfirmQuit] = { "QUIT GAME", kItemsConfirm, countof(kItemsConfirm), true },
};

enum {
  kWait_None,
  kWait_Key,     // waiting for a keyboard key
  kWait_Button,  // waiting for a controller button
};

enum {
  kRepeatDelayMs = 350,
  kRepeatRateMs = 110,
  kCaptureTimeoutMs = 5000,
  kMessageMs = 2200,
};

static struct {
  bool open;
  bool dirty;
  uint8 waiting;
  uint32 wait_start;
  uint8 page;
  uint8 cursor[kPage_Count];
  uint32 frame;  // animation clock, ~60 per second regardless of refresh rate
  uint32 held_prev;
  uint32 repeat_at;
  const char *message;
  uint32 message_until;
  uint8 startup_output_method;
  bool startup_output_method_set;
  uint8 *snapshot;
  int snap_w, snap_h;
} g_menu;

static const uint16 kLatencySamples[] = { 256, 512, 1024, 2048 };
static const char *const kLatencyNames[] = { "LOW", "NORMAL", "HIGH", "HIGHEST" };
static const uint16 kSampleRates[] = { 32000, 44100, 48000 };
static const char *const kSampleRateNames[] = { "32 KHZ", "44.1 KHZ", "48 KHZ" };
static const uint8 kRendererOrder[] = { kOutputMethod_SDL, kOutputMethod_OpenGL, kOutputMethod_SDLSoftware };
static const char *const kRendererNames[] = { [kOutputMethod_SDL] = "SDL", [kOutputMethod_SDLSoftware] = "SOFTWARE", [kOutputMethod_OpenGL] = "OPENGL" };

static bool IsKeyItem(int id) { return id >= kItem_Key0 && id <= kItem_Key11; }
static bool IsPadItem(int id) { return id >= kItem_Pad0 && id <= kItem_Pad11; }

static void ShowMessage(const char *msg) {
  g_menu.message = msg;
  g_menu.message_until = SDL_GetTicks() + kMessageMs;
}

// Button names as printed on each controller family, indexed by kGamepadBtn_*.
static const char *PadButtonName(int family, int b) {
  static const char *const kDpad[4] = { "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT" };
  if (b < 0 || b >= kGamepadBtn_Count)
    return "NONE";
  if (b >= kGamepadBtn_DpadUp && b <= kGamepadBtn_DpadRight)
    return kDpad[b - kGamepadBtn_DpadUp];
  static const char *const kXboxOne[kGamepadBtn_Count] = {
    "A", "B", "X", "Y", "VIEW", "XBOX", "MENU", "LS", "RS", "LB", "RB", 0, 0, 0, 0, "LT", "RT" };
  static const char *const kXbox360[kGamepadBtn_Count] = {
    "A", "B", "X", "Y", "BACK", "GUIDE", "START", "LS", "RS", "LB", "RB", 0, 0, 0, 0, "LT", "RT" };
  static const char *const kPS[kGamepadBtn_Count] = {
    "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "SELECT", "PS", "START", "L3", "R3", "L1", "R1", 0, 0, 0, 0, "L2", "R2" };
  static const char *const kNintendo[kGamepadBtn_Count] = {
    "A", "B", "X", "Y", "MINUS", "HOME", "PLUS", "LS", "RS", "L", "R", 0, 0, 0, 0, "ZL", "ZR" };
  switch (family) {
  case kPadFamily_XboxOne: return kXboxOne[b];
  case kPadFamily_PS3: case kPadFamily_PS4: case kPadFamily_PS5:
    if (b == kGamepadBtn_Back)
      return family == kPadFamily_PS5 ? "CREATE" : family == kPadFamily_PS4 ? "SHARE" : "SELECT";
    if (b == kGamepadBtn_Start)
      return family == kPadFamily_PS3 ? "START" : "OPTIONS";
    return kPS[b];
  case kPadFamily_Nintendo: return kNintendo[b];
  default: return kXbox360[b];
  }
}

static int IndexOfU16(const uint16 *arr, int n, int v) {
  for (int i = 0; i < n; i++)
    if (arr[i] >= v)
      return i;
  return n - 1;
}

static bool ItemHasValue(int id) {
  return (id >= kItem_DisplayMode && id <= kItem_SampleRate) || IsKeyItem(id) || IsPadItem(id) ||
         (id >= kItem_SpriteLimit && id <= kItem_FocusPause);
}

static bool ItemIsEnabled(int id) {
  if (id == kItem_WindowSize)
    return Platform_GetDisplayMode() == 0;
  return true;
}

static void UpperCopy(char *dst, size_t size, const char *src, size_t max_chars) {
  size_t i = 0;
  for (; src[i] && i + 1 < size && i < max_chars; i++)
    dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
  dst[i] = 0;
}

static void GetValueText(int id, char *buf, size_t size) {
  buf[0] = 0;
  switch (id) {
  case kItem_DisplayMode: {
    static const char *const kNames[] = { "WINDOW", "FULLSCREEN", "EXCLUSIVE" };
    snprintf(buf, size, "%s", kNames[Platform_GetDisplayMode()]);
    break;
  }
  case kItem_Aspect: {
    int e = Platform_GetWidescreen();
    snprintf(buf, size, "%s", e >= 96 ? "ULTRAWIDE" : e >= 72 ? "16:9" : e >= 52 ? "16:10" : "ORIGINAL");
    break;
  }
  case kItem_WindowSize: snprintf(buf, size, "%d*", Platform_GetWindowScale()); break;
  case kItem_Scaling:
    snprintf(buf, size, "%s", g_config.ignore_aspect_ratio ? "STRETCH" : g_config.integer_scaling ? "INTEGER" : "FIT");
    break;
  case kItem_Filter: snprintf(buf, size, "%s", g_config.linear_filtering ? "SMOOTH" : "SHARP"); break;
  case kItem_VSync: snprintf(buf, size, "%s", g_config.vsync ? "ON" : "OFF"); break;
  case kItem_Renderer: snprintf(buf, size, "%s", kRendererNames[g_config.output_method <= 2 ? g_config.output_method : 0]); break;
  case kItem_ShowFps: snprintf(buf, size, "%s", g_config.show_fps ? "ON" : "OFF"); break;
  case kItem_Volume:
    if (g_config.volume == 0)
      snprintf(buf, size, "MUTE");
    else
      snprintf(buf, size, "%d", g_config.volume);
    break;
  case kItem_Latency: snprintf(buf, size, "%s", kLatencyNames[IndexOfU16(kLatencySamples, 4, g_config.audio_samples)]); break;
  case kItem_SampleRate: snprintf(buf, size, "%s", kSampleRateNames[IndexOfU16(kSampleRates, 3, g_config.audio_freq)]); break;
  case kItem_SpriteLimit: snprintf(buf, size, "%s", g_config.no_sprite_limits ? "REMOVED" : "ORIGINAL"); break;
  case kItem_Autosave: snprintf(buf, size, "%s", g_config.autosave ? "ON" : "OFF"); break;
  case kItem_FocusPause: snprintf(buf, size, "%s", g_config.pause_on_focus_loss ? "ON" : "OFF"); break;
  default:
    if (IsKeyItem(id)) {
      const char *name = Config_ControlKeyName(id - kItem_Key0);
      UpperCopy(buf, size, *name ? name : "NONE", 11);
    } else if (IsPadItem(id)) {
      int family = Platform_GetPadFamily();
      UpperCopy(buf, size, PadButtonName(family, Config_GetPadButton(id - kItem_Pad0)), 11);
    }
    break;
  }
}

static int Wrap(int v, int n) {
  return ((v % n) + n) % n;
}

static void ChangeValue(int id, int delta) {
  if (!ItemIsEnabled(id))
    return;
  switch (id) {
  case kItem_DisplayMode: {
    int m = Wrap(Platform_GetDisplayMode() + delta, 3);
    Platform_SetDisplayMode(m);
    break;
  }
  case kItem_Aspect: {
    static const uint8 kSteps[] = { 0, 52, 72, 96 };
    int cur = 0, e = Platform_GetWidescreen();
    for (int i = 0; i < 4; i++)
      if (e >= kSteps[i])
        cur = i;
    Platform_SetWidescreen(kSteps[Wrap(cur + delta, 4)]);
    break;
  }
  case kItem_WindowSize: {
    int s = Platform_GetWindowScale() + delta, max = Platform_GetMaxWindowScale();
    s = s < 1 ? 1 : s > max ? max : s;
    Platform_SetWindowScale(s);
    break;
  }
  case kItem_Scaling: {
    int state = g_config.ignore_aspect_ratio ? 2 : g_config.integer_scaling ? 1 : 0;
    state = Wrap(state + delta, 3);
    g_config.ignore_aspect_ratio = (state == 2);
    g_config.integer_scaling = (state == 1);
    Platform_ApplyVideoSettings();
    break;
  }
  case kItem_Filter:
    g_config.linear_filtering ^= 1;
    Platform_ApplyVideoSettings();
    break;
  case kItem_VSync:
    g_config.vsync ^= 1;
    Platform_ApplyVideoSettings();
    break;
  case kItem_Renderer: {
    int cur = 0;
    for (int i = 0; i < 3; i++)
      if (kRendererOrder[i] == g_config.output_method)
        cur = i;
    g_config.output_method = kRendererOrder[Wrap(cur + delta, 3)];
    break;
  }
  case kItem_ShowFps: g_config.show_fps ^= 1; break;
  case kItem_Volume: {
    int v = g_config.volume + delta * 10;
    g_config.volume = v < 0 ? 0 : v > 100 ? 100 : v;
    Platform_ApplyVolume();
    break;
  }
  case kItem_Latency: {
    int i = IndexOfU16(kLatencySamples, 4, g_config.audio_samples) + delta;
    i = i < 0 ? 0 : i > 3 ? 3 : i;
    if (g_config.audio_samples == kLatencySamples[i])
      return;
    g_config.audio_samples = kLatencySamples[i];
    Platform_ReopenAudio();
    break;
  }
  case kItem_SampleRate: {
    int i = IndexOfU16(kSampleRates, 3, g_config.audio_freq) + delta;
    i = i < 0 ? 0 : i > 2 ? 2 : i;
    if (g_config.audio_freq == kSampleRates[i])
      return;
    g_config.audio_freq = kSampleRates[i];
    Platform_ReopenAudio();
    break;
  }
  case kItem_SpriteLimit:
    g_config.no_sprite_limits ^= 1;
    Platform_ApplySpriteLimit();
    break;
  case kItem_Autosave: g_config.autosave ^= 1; break;
  case kItem_FocusPause: g_config.pause_on_focus_loss ^= 1; break;
  default:
    return;
  }
  g_menu.dirty = true;
}

static void GoToPage(int page, int cursor) {
  g_menu.page = page;
  g_menu.cursor[page] = cursor;
  g_menu.waiting = kWait_None;
}

static void GoBack(void) {
  if (g_menu.waiting) {
    g_menu.waiting = kWait_None;
    return;
  }
  g_menu.message = NULL;
  switch (g_menu.page) {
  case kPage_Main: SettingsMenu_Close(); break;
  case kPage_Video: GoToPage(kPage_Main, 1); break;
  case kPage_Audio: GoToPage(kPage_Main, 2); break;
  case kPage_Controls: GoToPage(kPage_Main, 3); break;
  case kPage_Keyboard: GoToPage(kPage_Controls, 0); break;
  case kPage_Gamepad: GoToPage(kPage_Controls, 1); break;
  case kPage_Gameplay: GoToPage(kPage_Main, 4); break;
  case kPage_ConfirmReset: GoToPage(kPage_Main, 5); break;
  case kPage_ConfirmQuit: GoToPage(kPage_Main, 6); break;
  }
}

static int CurrentItem(void) {
  const PageDef *p = &kPages[g_menu.page];
  return p->items[g_menu.cursor[g_menu.page]];
}

static void Confirm(void) {
  int id = CurrentItem();
  switch (id) {
  case kItem_Resume: SettingsMenu_Close(); break;
  case kItem_Video: GoToPage(kPage_Video, 0); break;
  case kItem_Audio: GoToPage(kPage_Audio, 0); break;
  case kItem_Controls: GoToPage(kPage_Controls, 0); break;
  case kItem_Gameplay: GoToPage(kPage_Gameplay, 0); break;
  case kItem_Reset: GoToPage(kPage_ConfirmReset, 0); break;
  case kItem_Quit: GoToPage(kPage_ConfirmQuit, 0); break;
  case kItem_Back: case kItem_No: GoBack(); break;
  case kItem_Yes:
    if (g_menu.page == kPage_ConfirmQuit) {
      SettingsMenu_Close();
      Platform_Quit();
    } else {
      SettingsMenu_Close();
      Platform_ResetGame();
    }
    break;
  case kItem_KeyboardPage: GoToPage(kPage_Keyboard, 0); break;
  case kItem_GamepadPage: GoToPage(kPage_Gamepad, 0); break;
  case kItem_KeysDefault:
    Config_ResetControlKeys();
    g_menu.dirty = true;
    ShowMessage("DEFAULT KEYS RESTORED");
    break;
  case kItem_PadDefault:
    Config_ResetPadButtons();
    g_menu.dirty = true;
    ShowMessage("DEFAULT BUTTONS RESTORED");
    break;
  default:
    if (IsKeyItem(id)) {
      g_menu.waiting = kWait_Key;
      g_menu.wait_start = SDL_GetTicks();
    } else if (IsPadItem(id)) {
      if (Platform_GetPadFamily() == kPadFamily_None) {
        ShowMessage("CONNECT A CONTROLLER");
      } else {
        g_menu.waiting = kWait_Button;
        g_menu.wait_start = SDL_GetTicks();
      }
    } else if (ItemHasValue(id)) {
      ChangeValue(id, 1);
    }
    break;
  }
}

static void Navigate(int dir) {
  if (g_menu.waiting)
    return;
  const PageDef *p = &kPages[g_menu.page];
  uint8 *cur = &g_menu.cursor[g_menu.page];
  if (dir & kMenuNav_Up)
    *cur = Wrap(*cur - 1, p->count);
  if (dir & kMenuNav_Down)
    *cur = Wrap(*cur + 1, p->count);
  int id = p->items[*cur];
  if (dir & (kMenuNav_Up | kMenuNav_Down))
    g_menu.message = NULL;
  if (ItemHasValue(id) && !IsKeyItem(id) && !IsPadItem(id)) {
    if (dir & kMenuNav_Left)
      ChangeValue(id, -1);
    if (dir & kMenuNav_Right)
      ChangeValue(id, 1);
  }
}

// ---------------------------------------------------------------------------
// Public API

bool SettingsMenu_IsOpen(void) {
  return g_menu.open;
}

void SettingsMenu_Open(const uint8 *frame, int width, int height) {
  if (!g_menu.startup_output_method_set) {
    g_menu.startup_output_method = g_config.output_method;
    g_menu.startup_output_method_set = true;
  }
  size_t size = (size_t)width * height * 4;
  if (width != g_menu.snap_w || height != g_menu.snap_h) {
    free(g_menu.snapshot);
    g_menu.snapshot = (uint8 *)malloc(size);
    g_menu.snap_w = width;
    g_menu.snap_h = height;
  }
  if (g_menu.snapshot) {
    for (int y = 0; y < height; y++) {
      // Dim the frozen game picture to ~40% so the menu stands out.
      const uint32 *src = (const uint32 *)(frame + y * width * 4);
      uint32 *dst = (uint32 *)(g_menu.snapshot + y * width * 4);
      for (int x = 0; x < width; x++) {
        uint32 p = src[x];
        dst[x] = 0xff000000 | (((p >> 2) & 0x3f3f3f) + ((p >> 3) & 0x1f1f1f));
      }
    }
  }
  g_menu.open = true;
  g_menu.page = kPage_Main;
  g_menu.cursor[kPage_Main] = 0;
  g_menu.waiting = kWait_None;
  g_menu.held_prev = ~0u;  // ignore directions already held when the menu opens
  g_menu.message = NULL;
  g_menu.frame = SDL_GetTicks() / 16;
}

void SettingsMenu_Close(void) {
  if (!g_menu.open)
    return;
  g_menu.open = false;
  g_menu.waiting = kWait_None;
  if (g_menu.dirty) {
    g_menu.dirty = false;
    if (!Config_Save())
      fprintf(stderr, "Warning: unable to save settings\n");
  }
}

void SettingsMenu_OnKey(SDL_Keycode key, bool pressed, bool is_repeat, int game_cmd) {
  if (!pressed)
    return;
  if (g_menu.waiting) {
    if (is_repeat)
      return;
    if (key == SDLK_ESCAPE) {
      g_menu.waiting = kWait_None;
      ShowMessage("CANCELLED");
      return;
    }
    if (g_menu.waiting != kWait_Key)
      return;  // waiting for a controller button; only Esc does anything
    int id = CurrentItem();
    g_menu.waiting = kWait_None;
    if (!IsKeyItem(id))
      return;
    int r = Config_SetControlKey(id - kItem_Key0, key);
    if (r < 0) {
      ShowMessage("THAT KEY CAN'T BE USED");
    } else {
      g_menu.dirty = true;
      if (r > 0)
        ShowMessage("OLD SHORTCUT UNBOUND");
    }
    return;
  }
  switch (key) {
  case SDLK_ESCAPE: case SDLK_BACKSPACE: if (!is_repeat) GoBack(); return;
  case SDLK_UP: Navigate(kMenuNav_Up); return;
  case SDLK_DOWN: Navigate(kMenuNav_Down); return;
  case SDLK_LEFT: Navigate(kMenuNav_Left); return;
  case SDLK_RIGHT: Navigate(kMenuNav_Right); return;
  case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: if (!is_repeat) Confirm(); return;
  }
  // Also accept whatever keys the player mapped to the SNES buttons.
  if (game_cmd >= kKeys_Controls && game_cmd <= kKeys_Controls_Last) {
    switch (game_cmd - kKeys_Controls) {
    case 0: Navigate(kMenuNav_Up); break;
    case 1: Navigate(kMenuNav_Down); break;
    case 2: Navigate(kMenuNav_Left); break;
    case 3: Navigate(kMenuNav_Right); break;
    case 5: case 6: if (!is_repeat) Confirm(); break;  // Start, A
    case 7: if (!is_repeat) GoBack(); break;  // B
    }
  }
}

void SettingsMenu_OnGamepadButton(int button, bool pressed) {
  if (!pressed || button < 0 || button >= kGamepadBtn_Count)
    return;
  if (g_menu.waiting == kWait_Button) {
    int id = CurrentItem();
    g_menu.waiting = kWait_None;
    if (IsPadItem(id) && button != kGamepadBtn_Guide) {
      Config_SetPadButton(id - kItem_Pad0, button);
      g_menu.dirty = true;
    }
    return;
  }
  if (g_menu.waiting == kWait_Key) {
    if (button == kGamepadBtn_B) {
      g_menu.waiting = kWait_None;
      ShowMessage("CANCELLED");
    }
    return;
  }
  switch (button) {
  case kGamepadBtn_A: Confirm(); break;
  case kGamepadBtn_B: GoBack(); break;
  case kGamepadBtn_Start: SettingsMenu_Close(); break;
  case kGamepadBtn_L1: if (g_menu.page != kPage_Main) Navigate(kMenuNav_Left); break;
  case kGamepadBtn_R1: if (g_menu.page != kPage_Main) Navigate(kMenuNav_Right); break;
  }
}

void SettingsMenu_Tick(uint32 held) {
  uint32 now = SDL_GetTicks();
  g_menu.frame = now / 16;
  if (g_menu.waiting) {
    // Capturing: directions are bindable buttons, not navigation.
    if (now - g_menu.wait_start >= kCaptureTimeoutMs) {
      g_menu.waiting = kWait_None;
      ShowMessage("NOTHING CHANGED");
    }
    g_menu.held_prev = held;
    return;
  }
  uint32 pressed = held & ~g_menu.held_prev;
  if (pressed) {
    Navigate(pressed);
    g_menu.repeat_at = now + kRepeatDelayMs;
  } else if (held && held == g_menu.held_prev) {
    if ((int32)(now - g_menu.repeat_at) >= 0) {
      Navigate(held);
      g_menu.repeat_at = now + kRepeatRateMs;
    }
  }
  g_menu.held_prev = held;
}

void SettingsMenu_Draw(uint8 *pixels, int pitch, int width, int height) {
  Canvas cv = { pixels, pitch, width, height };
  Canvas *c = &cv;

  // Frozen, dimmed game picture behind the menu
  if (g_menu.snapshot && g_menu.snap_w == width && g_menu.snap_h == height) {
    for (int y = 0; y < height; y++)
      memcpy(pixels + y * pitch, g_menu.snapshot + y * width * 4, width * 4);
  } else {
    for (int y = 0; y < height; y++)
      memset(pixels + y * pitch, 0, width * 4);
  }

  const PageDef *p = &kPages[g_menu.page];
  int row_h = p->count > 12 ? 11 : 13;
  int win_w = 232;
  int header_h = 8 + 8 + 6;
  int footer_h = 6 + 8 + 8;
  int win_h = header_h + p->count * row_h + footer_h;
  int win_x = (width - win_w) / 2;
  int win_y = (height - win_h) / 2;
  if (win_y < 2) win_y = 2;

  // Window: black edge, white frame, black inside (like the game's message boxes)
  FillRoundRect(c, win_x, win_y, win_w, win_h, 3, 0x000000);
  FillRoundRect(c, win_x + 1, win_y + 1, win_w - 2, win_h - 2, 3, kColor_Frame);
  FillRoundRect(c, win_x + 3, win_y + 3, win_w - 6, win_h - 6, 2, kColor_Fill);

  int cx = win_x + win_w / 2;
  DrawTextCentered(c, cx, win_y + 8, p->title, kColor_Title);
  for (int x = win_x + 10; x < win_x + win_w - 10; x++)
    PutPixel(c, x, win_y + header_h - 2, kColor_Rule);

  int bob = (g_menu.frame >> 3) & 1;
  int sel = g_menu.cursor[g_menu.page];
  int y = win_y + header_h + (row_h - 8) / 2 + 1;
  char value[64];

  for (int i = 0; i < p->count; i++, y += row_h) {
    int id = p->items[i];
    bool selected = (i == sel);
    bool enabled = ItemIsEnabled(id);
    uint32 color = !enabled ? kColor_Disabled : selected ? kColor_Selected : kColor_Text;
    const char *label = kItemLabels[id];

    int label_x = p->centered ? cx - TextWidth(label) / 2 : win_x + 22;
    if (selected)
      DrawGlyph(c, label_x - 12 + bob, y, GlyphFor('@'), kColor_Text);
    DrawText(c, label_x, y, label, color);

    if (ItemHasValue(id)) {
      int right = win_x + win_w - 12;
      bool is_key = IsKeyItem(id) || IsPadItem(id);
      if (selected && is_key && g_menu.waiting) {
        uint32 elapsed = SDL_GetTicks() - g_menu.wait_start;
        int secs_left = (int)((kCaptureTimeoutMs - IntMin(elapsed, kCaptureTimeoutMs) + 999) / 1000);
        if (g_menu.waiting == kWait_Key)
          snprintf(value, sizeof(value), "%s", (g_menu.frame & 16) ? "PRESS A KEY" : "");
        else
          snprintf(value, sizeof(value), "PRESS %d", secs_left);
        DrawText(c, right - 8 - TextWidth(value), y, value, kColor_Selected);
        continue;
      }
      GetValueText(id, value, sizeof(value));
      int vx = right - 8 - TextWidth(value);
      DrawText(c, vx, y, value, color);
      if (selected && enabled && !is_key) {
        DrawGlyph(c, vx - 9, y, GlyphFor('<'), kColor_Text);
        DrawGlyph(c, right - 7, y, GlyphFor('>'), kColor_Text);
      }
    }
  }

  // Help line
  const char *help = NULL;
  int cur = CurrentItem();
  bool show_message = g_menu.message && (int32)(g_menu.message_until - SDL_GetTicks()) > 0;
  if (show_message)
    help = g_menu.message;
  else if (g_menu.waiting == kWait_Key)
    help = "ESC OR CIRCLE/B TO CANCEL";
  else if (g_menu.waiting == kWait_Button)
    help = "PRESS A CONTROLLER BUTTON";
  else if (IsKeyItem(cur))
    help = "ENTER TO CHANGE KEY";
  else if (g_menu.page == kPage_Gamepad && Platform_GetPadFamily() == kPadFamily_None)
    help = "NO CONTROLLER CONNECTED";
  else if (IsPadItem(cur))
    help = "CROSS/A TO CHANGE BUTTON";
  else if (cur == kItem_Renderer && g_menu.startup_output_method_set && g_config.output_method != g_menu.startup_output_method)
    help = (g_menu.frame & 32) ? "RESTART GAME TO APPLY" : "";
  else if (g_menu.page == kPage_ConfirmReset || g_menu.page == kPage_ConfirmQuit)
    help = "UNSAVED PROGRESS IS LOST";
  else
    help = kItemHelp[cur];
  if (help)
    DrawTextCentered(c, cx, win_y + win_h - 8 - 8, help, kColor_Help);
}
