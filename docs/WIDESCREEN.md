# Widescreen status

`[Graphics] AspectRatio` (or Options > Video > Aspect) adds `E` extra pixels on
each side of the 256 px view: 16:10 = 52, 16:9 = 72, 21:9 = 96. Game code reads
the margin from `g_ws_margin`. With the original aspect (`E = 0`), every change
reduces to the original code. Check this with `VerifyAgainstRom = 1` while
running the attract demo; it should report no "Verify failure".

## Done

- Renderer: side margins, sprite X past 256, full-width windows, mosaic, BG3
  status bar kept at 256 px (`snes/ppu.c`).
- Per-frame margins: only in scene modes, and clipped at level/map edges and in
  Mode 7 rooms (`SmwUpdateWidescreenMargins`, `smw_rtl.c`).
- Normal sprites: spawn columns, level-load sweep, draw range (GetDrawInfo in
  banks 01, 02 and 03), despawn (SubOffscreen in banks 01, 02 and 03).
- Mario fireballs, generic extended sprites (hammers, bones, enemy fireballs,
  smoke trails), score popups, bounce blocks, brick pieces.

## Remaining (from the code audit; file:function, SNES address)

Visible issues first:

- Smoke sprites have no X high byte (`smoke_spr_xpos_lo` only). They need a
  new high-byte array, then the 8-bit culls in SmokeSpr01/02/03 can be widened
  (029927, 0296e3, 029797).
- Spinning coins from blocks (0299f1): the cull uses only the X low byte.
- Minor extended sprites 02/03/06/07/0A/0B (028ed2, 028e7e, 028ddb, 028d4f,
  028cc4, 028c0f) and ExtSpr01 smoke puff (02a34f) use 8-bit culls.
- Cluster sprites: ceiling boos and bats wrap every 256 px (02fd48, 02fbc7);
  the boo ring removal range (02fa98); candle flames (02fa16); reappearing
  boos (02f83d).
- Wing tiles on sprites (019e37) and Yoshi's wings and throat (02bb23,
  01ef18) are culled at x < 256 and never set the X high bit.
- Lakitu cloud face and fishing line (01e8ca, 02e67a).
- Generators and shooters use 256 px spawn positions (02b036, 02b07c, 02b1bc,
  02b207, 02b26c, 02b2d6, 02b115, 02b3b6, 02b466).
- Overworld sprites, players and clouds (04862e, 04fb0a, 04f290, 04fb37,
  04f708).
- Lunar Magic hack paths in `lm.c`: tile streaming covers only 20 columns
  (needs 20 + 2*ceil(E/16)), and the LM sprite spawn window (`LmHook_BE8A`).
  Vanilla levels are unaffected.

Kept the same on purpose (gameplay, not visuals): the player screen clamp,
which sprites count as "awake" (`spr_xoffscreen_flag`), autoscroll edges,
shell and Chuck block hits, and the Yoshi tongue range.

Possible upstream bug noted in the audit: `PlayerGFXRt_00E45D` (00e45d)
culls player tiles on Y where the original tests X. It never culls today.
