#include "smw_rtl.h"
#include "variables.h"
#include <time.h>
#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "src/funcs.h"

const uint8 *ptr_layer1_data;
const uint8 *ptr_layer2_data;
uint8 ptr_layer2_is_bg;

uint8 *ptr_lo_map16_data;
uint8 *ptr_lo_map16_data_bak;

bool g_lunar_magic;


void AddSprXPos(uint8 k, uint16 x) {
  AddHiLo(&SPR(spr_xpos_hi, k), &SPR(spr_xpos_lo, k), x);
}

void AddSprYPos(uint8 k, uint16 y) {
  AddHiLo(&SPR(spr_ypos_hi, k), &SPR(spr_ypos_lo, k), y);
}

void AddSprXYPos(uint8 k, uint16 x, uint16 y) {
  AddHiLo(&SPR(spr_xpos_hi, k), &SPR(spr_xpos_lo, k), x);
  AddHiLo(&SPR(spr_ypos_hi, k), &SPR(spr_ypos_lo, k), y);
}

uint16 GetSprXPos(uint8 k) {
  return PAIR16(SPR(spr_xpos_hi, k), SPR(spr_xpos_lo, k));
}

uint16 GetSprYPos(uint8 k) {
  return PAIR16(SPR(spr_ypos_hi, k), SPR(spr_ypos_lo, k));
}

void SetSprXPos(uint8 k, uint16 x) {
  SPR(spr_xpos_hi, k) = x >> 8;
  SPR(spr_xpos_lo, k) = x;
}

void SetSprYPos(uint8 k, uint16 y) {
  SPR(spr_ypos_hi, k) = y >> 8;
  SPR(spr_ypos_lo, k) = y;
}

void SetSprXYPos(uint8 k, uint16 x, uint16 y) {
  SetHiLo(&SPR(spr_xpos_hi, k), &SPR(spr_xpos_lo, k), x);
  SetHiLo(&SPR(spr_ypos_hi, k), &SPR(spr_ypos_lo, k), y);
}

void SmwSavePlaythroughSnapshot() {
  char buf[128];
  snprintf(buf, sizeof(buf), "playthrough/%d_%d_%d.sav", ow_level_number_lo, misc_exit_level_action, (int)time(NULL));
  RtlSaveSnapshot(buf, false);
}

void UploadOAMBuffer() {  // 008449
  memcpy(g_ppu->oam, g_ram + 0x200, 0x220);
  // OAM page 1 (sprite slots 12+) only when it was filled this frame
  g_ppu->extOam = g_oam_page1_used ? (const uint16 *)g_oam_page1 : NULL;
  g_ppu->extHighOam = g_oam_page1 + 0x200;
  g_oam_page1_used = false;
  RtlPpuWrite(OAMADDH, 0x80);
  RtlPpuWrite(OAMADDL, mirror_oamaddress_lo);
}


int g_ws_margin;

// Widescreen margins for this frame: shown only in modes that display a
// scrolling scene, and never past the edges of the level or map, where the
// tilemap holds stale data.
static void SmwUpdateWidescreenMargins(void) {
  int e = g_ws_margin, left = 0, right = 0;
  uint8 mode = misc_game_mode;
  bool scene = (mode >= 0x05 && mode <= 0x0f) || (mode >= 0x13 && mode <= 0x15);
  if (e && scene && PPU_mode(g_ppu) != 7) {
    int cam = (int16)mirror_current_layer1_xpos;
    int width;
    if (mode >= 0x0b && mode <= 0x0f)
      width = 512;  // overworld map
    else if (misc_level_layout_flags & 1)
      width = 512;  // vertical level
    else
      width = camera_last_screen_horiz <= 1 ? 256 : camera_last_screen_horiz * 256;
    left = IntMax(0, IntMin(e, cam));
    right = IntMax(0, IntMin(e, width - 256 - cam));
  }
  PpuSetExtraSideSpace(g_ppu, left, right, e);
}

void SmwDrawPpuFrame(void) {
  SimpleHdma hdma_chans[3];

  Dma *dma = g_dma;

  dma_startDma(dma, mirror_hdmaenable, true);

  SimpleHdma_Init(&hdma_chans[0], &dma->channel[5]);
  SimpleHdma_Init(&hdma_chans[1], &dma->channel[6]);
  SimpleHdma_Init(&hdma_chans[2], &dma->channel[7]);

  int trigger = g_snes->vIrqEnabled ? g_snes->vTimer + 1 : -1;
  // Widescreen: when a mid-frame IRQ is used, the lines above it are the
  // status bar on BG3, which stays a centered 256 px. Below it BG3 is part of
  // the scene and may extend into the margins.
  g_ppu->bg3Margins = (trigger < 0);
  SmwUpdateWidescreenMargins();

  for (int i = 0; i <= 224; i++) {
    ppu_runLine(g_ppu, i);
    SimpleHdma_DoLine(&hdma_chans[0]);
    SimpleHdma_DoLine(&hdma_chans[1]);
    SimpleHdma_DoLine(&hdma_chans[2]);
    //    dma_doHdma(snes->dma);
    if (i == trigger) {
      g_ppu->bg3Margins = true;
      SmwVectorIRQ();
      trigger = g_snes->vIrqEnabled ? g_snes->vTimer + 1 : -1;
    }
  }
}

void SmwRunOneFrameOfGame(void) {
  if (*(uint16 *)reset_sprites_y_function_in_ram == 0)
    SmwVectorReset();
  SmwRunOneFrameOfGame_Internal();
  SmwVectorNMI();
}


void LoadStripeImage_UploadToVRAM(const uint8 *pp) {  // 00871e
  while (1) {
    if ((*pp & 0x80) != 0)
      break;
    uint16 vram_addr = pp[0] << 8 | pp[1];
    if (g_lunar_magic)
      vram_addr = LmHook_LoadStripeImage(vram_addr);

    uint8 vmain = __CFSHL__(pp[2], 1);
    uint8 fixed_addr = (uint8)(pp[2] & 0x40) >> 3;
    uint16 num = (swap16(WORD(pp[2])) & 0x3FFF) + 1;
    pp += 4;

    if (fixed_addr) {
      if (vram_addr != 0xffff) {
        uint16 *dst = g_ppu->vram + vram_addr;
        uint16 src_data = WORD(*pp);
        int ctr = (num + 1) >> 1;
        if (vmain) {
          for (int i = 0; i < ctr; i++)
            dst[i * 32] = src_data;
        } else {
          // uhm...?
          uint8 *dst_b = (uint8 *)dst;
          for (int i = 0; i < num; i++)
            dst_b[i + ((i & 1) << 1)] = src_data;
          for (int i = 0; i < num; i += 2)
            dst_b[i + 1] = src_data >> 8;
        }
      }
      pp += 2;
    } else {
      if (vram_addr != 0xffff) {
        uint16 *dst = g_ppu->vram + vram_addr;
        uint16 *src = (uint16 *)pp;
        if (vmain) {
          for (int i = 0; i < (num >> 1); i++)
            dst[i * 32] = src[i];
        } else {
          for (int i = 0; i < (num >> 1); i++)
            dst[i] = src[i];
        }
      }
      pp += num;
    }
  }
}

