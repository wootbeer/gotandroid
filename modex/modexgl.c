// GL-backed implementation of modex.h. See that header for the overall rationale.
//
// Model: 5 independent 320x192, 8-bit palette-indexed page buffers -- 4 matching the original's
// PAGE0-PAGE3 constants from l_define.h (all any real caller ever passes as `pagebase`; no attempt
// is made to replicate the DOS original's single-VGA-bank planar memory layout, since nothing
// outside this file needs to know the difference), plus a 5th this port's own MODEX_PANEL_PAGE
// (see modex.h) reserves for the real status panel's background/overlays (got_panel.c) -- sized
// identically to the other four purely so every existing primitive keeps working unchanged, even
// though only its own top 48 rows are ever actually used. xshowpage() just records which page is
// "the display page" for the PLAYFIELD; modex_present_frame() (called once per frame by
// got_main.c, not part of the original API) converts that page's indices through the current
// palette into RGB, optionally appends MODEX_PANEL_PAGE's own top 48 rows below it (Display Modes
// A/B -- see modex_set_display_mode()), uploads the result as one GL texture, then draws it as a
// single quad -- full-bleed (Modes B/C, and always before this feature existed) or pillarboxed to
// the real 4:3 aspect (Mode A) -- exactly the "one textured fullscreen quad + glTexSubImage2D per
// frame" plan from got-android-port-notes_1.md section 6a, now with an optional second source
// region and a non-full-bleed quad shape.
//
// What's real here: xsetmode, xshowpage, xcopys2d, xcopyd2d, xline, xpset, xget/xput (a
// self-consistent pair -- see the comment above them), xfillrectangle, xpoint, xfput (background
// tile blit -- byte format verified against real GOTRES.DAT BPICS1 data, see the comment above
// its definition and notes section 2a), xfarput (arbitrary-size background image blit -- xfput's
// own formula generalized past its hardcoded 4/16, byte format verified against real GOTRES.DAT
// "STATUS" data, see the comment above its definition and got_panel.c), xtext (real bitmap-glyph
// blit -- byte format verified against the real GOTRES.DAT "TEXT" resource, see the comment above
// its definition; got_font.c owns loading that resource and is the only real caller),
// modex_draw_sprite_frame() (the port's sprite-drawing replacement, see below and modex.h), and
// the palette + GL presentation pipeline.
//
// What's deliberately stubbed, and why:
// xcopyd2dmasked/xcopyd2dmasked2/xcreatmaskimage/xcreatemaskimage/xcreatmaskimage2 (the
// original's masked-sprite-compositing API). Verifying real ACTOR sprite data (notes section 2a)
// turned up something that changes the plan here: the shipped game's actual sprite rendering
// (xdisplay_actors/xerase_actors in g_asm.asm) never calls these modex.h masked-image primitives
// at all -- it reads ACTOR.pic[dir][frame] (a MASK_IMAGE of 4 precomputed alignments) directly
// and blits via a VGA-latch/bit-mask hardware trick with no GL equivalent, entirely bypassing
// this API. On real hardware that hack exists only to avoid a per-pixel shift when a sprite's X
// isn't 4-pixel-aligned; nothing here has that constraint, so these five functions are very
// likely dead weight for this port -- they stay stubbed (log a warning once, return harmlessly)
// rather than being implemented. The actual replacement is modex_draw_sprite_frame() (declared
// in modex.h, implemented below): decodes straight from a real ACTOR pic/shot frame (plain
// chunky pixels, one palette-index byte per pixel, confirmed empirically -- see notes section
// 2a) and writes the opaque pixels (index 0 is transparent) directly into the target page's
// indexed buffer, the same one xpset/xfput write. That, not a separate RGBA-texture path, is
// what makes palette effects (flashes, fades, color cycling) apply to sprites automatically,
// with zero extra code -- modex_present_frame() already re-derives RGB from indices every frame.

#include "modex.h"

#include <GLES/gl.h>
#include <android/log.h>
#include <stdbool.h>
#include <string.h>

#define LOG_TAG "ModexGL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define MODEX_WIDTH 320
#define MODEX_HEIGHT 192
#define MODEX_NUM_PAGES 5
// Real STATUS panel height (1_panel.c's own 320x48 image) -- see modex.h's own MODEX_PANEL_PAGE
// comment. MODEX_HEIGHT + PANEL_HEIGHT (240) is the real combined 4:3 320x240 frame Display Modes
// A/B composite and present; MODEX_HEIGHT alone (192) is what Mode C (and the title/player-select
// screens, which force Mode C -- see modex_set_display_mode()'s own comment) always used, both
// before and after this feature existed.
#define PANEL_HEIGHT 48
#define FULL_HEIGHT_WITH_PANEL (MODEX_HEIGHT + PANEL_HEIGHT)

// GLES1 texture allocation should stay power-of-two-safe regardless of whether a given device
// supports the NPOT extension -- 320x192 isn't POT, so the texture itself is allocated at the
// next POT size up and only the real 320x192 region is ever uploaded to or sampled from (via
// texture-coordinate clamping in modex_present_frame() below). Standard, well-understood
// technique; costs a bit of unused texture memory, nothing else.
#define MODEX_TEX_W 512
#define MODEX_TEX_H 256

static unsigned char page_buffers[MODEX_NUM_PAGES][MODEX_WIDTH * MODEX_HEIGHT];
static unsigned char palette[256][3];
static bool palette_initialized = false;
static unsigned int display_page_index = 0;
static GLuint modex_texture = 0;
// Sized for the larger of the two composited frame heights (Modes A/B's combined playfield+panel
// 320x240, vs Mode C's playfield-only 320x192) -- see modex_present_frame()'s own comment.
static unsigned char rgb_scratch[MODEX_WIDTH * FULL_HEIGHT_WITH_PANEL * 3];

static ModexDisplayMode s_display_mode = MODEX_DISPLAY_MODE_C_MINIMAL; // see modex.h's own comment
static int s_screen_w = 0, s_screen_h = 0;
static bool s_menu_pillarbox = false; // modex_set_menu_pillarbox()'s own comment (modex.h)

// Mirror Mode (wootbeer: "an option in our Enhancement sub-menu, called 'Mirror Mode'... everything is
// flipped on the Y-axis [he means the vertical axis -- a left/right mirror], left is right, right is
// left... would have to make sure to keep all text, dialogue, menus, pop-ups, icons, all versions of
// the GUI, all still un-flipped"). Deliberately a PRESENTATION-time flag only -- see
// modex_set_mirror_mode()'s own modex.h comment for why got_main.c's render loop only turns this on
// during actual live gameplay frames (not while the pause menu/item picker/a dialogue box is drawn),
// and modex_present_frame()'s own comment below for exactly what it flips.
static bool s_mirror_mode = false;

// Mirror Mode's own GUI exemption -- see modex.h's own modex_set_mirror_exclude_rect() comment for
// the full design. `s_mirror_exclude_active` false means "no exclusion, flip the whole canvas" (the
// state before this feature existed, and every ordinary-gameplay frame after it). Coordinates are
// plain screen pixels in the un-mirrored 320x192 playfield space -- the same space every existing
// draw call already addresses.
static bool s_mirror_exclude_active = false;
static int s_mirror_exclude_x1 = 0, s_mirror_exclude_y1 = 0, s_mirror_exclude_x2 = 0,
		   s_mirror_exclude_y2 = 0;

// Screen-shake -- see modex_set_screen_shake_offset()'s own modex.h comment for the full design (a
// wootbeer-requested addition, not a real GoT effect). Playfield pixels, converted to a clip-space quad
// translation inside modex_present_frame() itself, right below.
static int s_shake_offset_x = 0;
static int s_shake_offset_y = 0;

// TEMPORARY dev/test aid -- wootbeer: "add a temporary way for me to mock view it on a 4:3 screen [so
// I can test how the new menu pillarboxing above, and gameplay's existing Mode A pillarboxing, would
// look on a real 4:3 device like the Retroid Pocket Nova] ... then after I test / look at it we will
// go back to normal." While true, modex_present_frame()'s own pillarbox/letterbox math (both Mode A
// and modex_set_menu_pillarbox()'s new menu case) treats the REAL device screen as if it were a true
// 4:3 shape, regardless of its actual real s_screen_w/s_screen_h -- shows exactly how this device
// would present both the menu/title/story screens and gameplay's own Mode A on an actual 4:3 screen,
// without needing one to test on, by adding the simulated 4:3 boundary's own black bars within
// whatever real screen this is built and run on.
//
// Tested by wootbeer with this on, with two results he flagged as surprising, both actually correct:
//   - The menu/title/story screens (5:3) showed bars on TOP/BOTTOM, not the sides. Right -- 5:3 is a
//     WIDER aspect than 4:3 (1.667 vs. 1.333), so fitting it into a proportionally boxier/taller 4:3
//     frame without cropping means filling the frame's own width and leaving the shorter dimension
//     (height) under-filled -- exactly the classic "widescreen content on an old 4:3 TV" letterbox
//     bars, not pillarbox ones. Pillarboxing only happens the other way around (narrower content on a
//     wider screen, e.g. gameplay's real 4:3 Mode A image on the actual RP6's own wider-than-4:3
//     screen).
//   - Gameplay Mode A showed no bars AT ALL with this flag on. Also right, and the actual point of
//     Mode A's own design: its real content genuinely IS 4:3, and this flag simulates an exactly-4:3
//     screen, so they match perfectly -- zero wasted space, exactly what a real Retroid Pocket Nova
//     should also get from Mode A (this flag isn't lying about that one -- a true 4:3 device really
//     doesn't need any pillarboxing for 4:3 gameplay content in the first place).
// wootbeer confirmed gameplay Modes A/B/C all already look right as shipped and only wanted the menu
// behavior checked, so testing is done -- flipped back to `false` here (real device aspect again).
static const bool s_debug_force_4_3_screen = false;

// The aspect ratio modex_present_frame()'s own pillarbox/letterbox math compares the pillaboxed
// content's own real aspect against -- normally the real device screen's own actual
// s_screen_w/s_screen_h, or (see s_debug_force_4_3_screen's own comment) a simulated 4:3 shape while
// that temporary flag is on.
static float modex_effective_screen_aspect(void) {
	if (s_debug_force_4_3_screen) {
		return 4.0f / 3.0f;
	}
	return (float) s_screen_w / (float) s_screen_h;
}

// --- Page addressing -------------------------------------------------------------------------
// Matches l_define.h's PAGE0-PAGE3 constants exactly (3840u/19280u/34720u/50160u) -- the only
// values any real call site ever passes as pagebase -- plus this port's own MODEX_PANEL_PAGE
// (90210u, see modex.h), which can never collide with any of those four real values. Anything
// else is a caller bug; logged and treated as page 0 rather than indexing out of bounds.

static unsigned int page_index(unsigned int pagebase) {
	switch (pagebase) {
		case 3840u: return 0;  // PAGE0
		case 19280u: return 1; // PAGE1
		case 34720u: return 2; // PAGE2
		case 50160u: return 3; // PAGE3
		case MODEX_PANEL_PAGE: return 4;
		default:
			LOGW("page_index: unrecognized pagebase %u, treating as page 0", pagebase);
			return 0;
	}
}

static unsigned char *page_ptr(unsigned int pagebase) {
	return page_buffers[page_index(pagebase)];
}

static void clip_rect(int *x1, int *y1, int *x2, int *y2) {
	if (*x1 < 0) *x1 = 0;
	if (*y1 < 0) *y1 = 0;
	if (*x2 > MODEX_WIDTH) *x2 = MODEX_WIDTH;
	if (*y2 > MODEX_HEIGHT) *y2 = MODEX_HEIGHT;
}

// --- Implemented primitives ------------------------------------------------------------------

void xsetmode(void) {
	memset(page_buffers, 0, sizeof(page_buffers));
	display_page_index = 0;
	// A default grayscale palette so anything drawn before modex_set_palette() is ever called
	// (e.g. this port's own on-screen self-tests) is still visible, rather than solid black.
	// Real ported game code calling xsetmode() again later doesn't reset the palette back to
	// this -- only the page buffers -- matching the original's separation between "set video
	// mode" and "load a palette" (1_grp.c's load_palette(), not yet ported) as two different
	// operations.
	if (!palette_initialized) {
		int i;
		for (i = 0; i < 256; ++i) {
			palette[i][0] = palette[i][1] = palette[i][2] = (unsigned char) i;
		}
		palette_initialized = true;
	}
}

void xshowpage(unsigned int page) {
	display_page_index = page_index(page);
}

void xpset(int X, int Y, unsigned int PageBase, int Color) {
	if (X < 0 || X >= MODEX_WIDTH || Y < 0 || Y >= MODEX_HEIGHT) {
		return;
	}
	page_ptr(PageBase)[Y * MODEX_WIDTH + X] = (unsigned char) Color;
}

unsigned int xpoint(int X, int Y, unsigned int PageBase) {
	if (X < 0 || X >= MODEX_WIDTH || Y < 0 || Y >= MODEX_HEIGHT) {
		return 0;
	}
	return page_ptr(PageBase)[Y * MODEX_WIDTH + X];
}

void xfillrectangle(int StartX, int StartY, int EndX, int EndY, unsigned int PageBase,
					 int Color) {
	int x, y;
	clip_rect(&StartX, &StartY, &EndX, &EndY);
	unsigned char *buf = page_ptr(PageBase);
	for (y = StartY; y < EndY; ++y) {
		for (x = StartX; x < EndX; ++x) {
			buf[y * MODEX_WIDTH + x] = (unsigned char) Color;
		}
	}
}

void xline(int x0, int y0, int x1, int y1, int color, int page) {
	// Standard integer Bresenham -- behavior (which pixels light up), not any original
	// assembly internals, is all that needs to match per the notes.
	unsigned char *buf = page_ptr((unsigned int) page);
	int dx = x1 - x0 < 0 ? x0 - x1 : x1 - x0;
	int sx = x0 < x1 ? 1 : -1;
	int dy = y1 - y0 < 0 ? y0 - y1 : y1 - y0;
	dy = -dy;
	int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	int e2;

	while (1) {
		if (x0 >= 0 && x0 < MODEX_WIDTH && y0 >= 0 && y0 < MODEX_HEIGHT) {
			buf[y0 * MODEX_WIDTH + x0] = (unsigned char) color;
		}
		if (x0 == x1 && y0 == y1) {
			break;
		}
		e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

void xcopys2d(int SourceStartX, int SourceStartY, int SourceEndX, int SourceEndY,
			  int DestStartX, int DestStartY, char *SourcePtr, unsigned int DestPageBase,
			  int SourceBitmapWidth, int DestBitmapWidth) {
	(void) DestBitmapWidth; // our page buffers are always MODEX_WIDTH-strided by construction
	unsigned char *dst = page_ptr(DestPageBase);
	int w = SourceEndX - SourceStartX;
	int h = SourceEndY - SourceStartY;
	int row;
	for (row = 0; row < h; ++row) {
		int dy = DestStartY + row;
		if (dy < 0 || dy >= MODEX_HEIGHT) {
			continue;
		}
		int sy = SourceStartY + row;
		int col;
		for (col = 0; col < w; ++col) {
			int dx = DestStartX + col;
			if (dx < 0 || dx >= MODEX_WIDTH) {
				continue;
			}
			int sx = SourceStartX + col;
			dst[dy * MODEX_WIDTH + dx] =
					(unsigned char) SourcePtr[sy * SourceBitmapWidth + sx];
		}
	}
}

void xcopyd2d(int SourceStartX, int SourceStartY, int SourceEndX, int SourceEndY,
			  int DestStartX, int DestStartY, unsigned int SourcePageBase,
			  unsigned int DestPageBase, int SourceBitmapWidth, int DestBitmapWidth) {
	(void) SourceBitmapWidth;
	(void) DestBitmapWidth; // both our page buffers are always MODEX_WIDTH-strided
	unsigned char *src = page_ptr(SourcePageBase);
	unsigned char *dst = page_ptr(DestPageBase);
	int w = SourceEndX - SourceStartX;
	int h = SourceEndY - SourceStartY;
	int row;
	for (row = 0; row < h; ++row) {
		int dy = DestStartY + row;
		int sy = SourceStartY + row;
		if (dy < 0 || dy >= MODEX_HEIGHT || sy < 0 || sy >= MODEX_HEIGHT) {
			continue;
		}
		int col;
		for (col = 0; col < w; ++col) {
			int dx = DestStartX + col;
			int sx = SourceStartX + col;
			if (dx < 0 || dx >= MODEX_WIDTH || sx < 0 || sx >= MODEX_WIDTH) {
				continue;
			}
			dst[dy * MODEX_WIDTH + dx] = src[sy * MODEX_WIDTH + sx];
		}
	}
}

// xget/xput are a self-consistent pair, not tied to any external file format -- unlike xfput,
// nothing outside this port ever reads or writes this buffer, so the exact layout is entirely
// this port's own choice (a small width/height header followed by raw indices) rather than
// something that needs verifying against real data first.
void xget(int x1, int y1, int x2, int y2, unsigned int pagebase, char *buff, int invis) {
	(void) invis; // no real call site exists yet to clarify its intended meaning
	int w = x2 - x1;
	int h = y2 - y1;
	unsigned char *src = page_ptr(pagebase);
	unsigned char *out = (unsigned char *) buff;
	int row, col;
	out[0] = (unsigned char) (w & 0xFF);
	out[1] = (unsigned char) (h & 0xFF);
	out += 2;
	for (row = 0; row < h; ++row) {
		int sy = y1 + row;
		for (col = 0; col < w; ++col) {
			int sx = x1 + col;
			*out++ = (sx >= 0 && sx < MODEX_WIDTH && sy >= 0 && sy < MODEX_HEIGHT)
					? src[sy * MODEX_WIDTH + sx] : 0;
		}
	}
}

void xput(int x, int y, unsigned int pagebase, char *buff) {
	unsigned char *in = (unsigned char *) buff;
	int w = in[0];
	int h = in[1];
	unsigned char *dst = page_ptr(pagebase);
	int row, col;
	in += 2;
	for (row = 0; row < h; ++row) {
		int dy = y + row;
		for (col = 0; col < w; ++col) {
			int dx = x + col;
			if (dx >= 0 && dx < MODEX_WIDTH && dy >= 0 && dy < MODEX_HEIGHT) {
				dst[dy * MODEX_WIDTH + dx] = *in;
			}
			++in;
		}
	}
}

// xfput() -- BACKGROUND TILE BLIT, format verified against real GOTRES.DAT data ---------------
//
// Buffer layout confirmed two ways: (1) reading the real DOS assembly implementation in the
// original source release's utility/g_asm.asm (xfput/xfput_plane, and xget/xput which write
// this same buffer shape), and (2) cross-checked against actual extracted BPICS1 resource bytes
// from a real GOTRES.DAT -- decoded with this exact layout and rendered to a contact sheet,
// producing clean, coherent 16x16 tile art (circles, diagonal edges, dollar-sign icons, dithered
// terrain, etc.), not noise. See got-android-port-notes_1.md section 2a for the full verification
// writeup.
//
// Layout: a 6-byte header (three little-endian 16-bit words -- width in mode-X "addresses",
// height, and an invis-color field the real xfput() never actually reads, always skipped
// unconditionally) followed by always-exactly-16x16 pixels stored as four sequential 64-byte
// VGA mode-X bitplanes (NOT row-major chunky data -- the real xfput hard-codes 4 addresses x 16
// rows regardless of what the header says). For source pixel (px,py) with px,py in 0..15:
//   plane = px % 4          (which of the 4 sequential 64-byte blocks)
//   col   = px / 4          (address within that plane's 4-wide, 16-tall block)
//   byte  = body[plane*64 + py*4 + col]
// Pixel values 0 and 15 are transparent (skipped) -- matches the real asm's hard-coded checks
// in xput_plane/xfput_plane. This is the BPICS/background-tile format specifically; ACTOR sprite
// frame data (pic[16][256]/shot[4][256]) uses a different, plain row-major chunky layout with no
// header -- confirmed separately by decompressing a real ACTOR resource and rendering it (came
// out as a recognizable enemy sprite, not this plane-interleaved shape). See notes.
void xfput(int x, int y, unsigned int pagebase, char *buff) {
	const unsigned char *body = (const unsigned char *) buff + 6;
	int py, px;
	for (py = 0; py < 16; ++py) {
		for (px = 0; px < 16; ++px) {
			int plane = px & 3;
			int col = px >> 2;
			unsigned char v = body[plane * 64 + py * 4 + col];
			if (v == 0 || v == 15) {
				continue; // transparent, matches the real xfput_plane's hard-coded skip
			}
			xpset(x + px, y + py, pagebase, v);
		}
	}
}

// xfarput() -- REAL BACKGROUND IMAGE BLIT (arbitrary width/height), format verified against real
// GOTRES.DAT "STATUS" data -- see modex.h's own comment for the full byte-layout writeup (a 6-byte
// header -- width-in-addresses, height, 2 unused bytes, the exact same 3-little-endian-16-bit-word
// shape xfput()'s own header above already uses -- then 4 sequential (width_addr*height)-byte
// bitplanes, the same addressing scheme xfput() above uses generalized past its own hardcoded 4/16).
// Deliberately NO transparency skip (unlike xfput()/xput(), where 0/15 mean "see-through") -- real
// xfarput_plane's own assembly is a plain unconditional byte copy, appropriate for a full opaque
// background image meant to completely cover its target. `buff` bytes beyond the header+4-plane
// body (this port's own real STATUS resource has some trailing slack past that point -- see
// got_panel.c's own comment) are simply never read, matching real xfarput() itself.
//
// The header really is 6 bytes, not 8 -- confirmed by re-reading real xfarput's own assembly
// (utility/g_asm.asm) byte-by-byte after wootbeer reported the real panel's background art rendering a
// few pixels off (see got-android-port-notes_1.md's own bugfix writeup): `mov ax,es:[bx]; mov
// wid,ax; inc bx; inc bx; mov ax,es:[bx]; mov height,ax; add bx,4;` -- bx sits at offset 2 (only
// the two `inc bx`s after reading width ever move it) when height is read from offset 2-3, and the
// following `add bx,4` lands it at offset 6, not 8. An 8-byte header read the real pixel body two
// bytes late, which -- since each plane row is 80 bytes (one per 4-pixel-wide "address") -- shows
// up as the entire image shifted 2 addresses (8 screen pixels) into itself, with the last couple of
// columns of each plane bleeding in the next plane's/next row's leading bytes instead of wrapping
// cleanly. Verified against a real GOTRES.DAT: with a 6-byte header, the background art's own drawn
// bar troughs line up exactly with display_health()'s/display_magic()'s real (59,8)-(209,12)/
// (59,20)-(209,24) rectangles; with 8, they were visibly offset, exactly matching what wootbeer saw
// on-device.
void xfarput(int x, int y, unsigned int pagebase, char *buff) {
	const unsigned char *hdr = (const unsigned char *) buff;
	int width_addr = hdr[0] | (hdr[1] << 8);
	int height = hdr[2] | (hdr[3] << 8);
	const unsigned char *body = hdr + 6;
	long plane_size = (long) width_addr * height;
	int width = width_addr * 4;
	int px, py;
	for (py = 0; py < height; ++py) {
		for (px = 0; px < width; ++px) {
			int plane = px & 3;
			int col = px >> 2;
			unsigned char v = body[plane * plane_size + (long) py * width_addr + col];
			xpset(x + px, y + py, pagebase, v);
		}
	}
}

// xtext() -- REAL BITMAP GLYPH BLIT, format verified against real GOTRES.DAT "TEXT" data ---------
//
// `buff` is one already-selected glyph's raw 72-byte block from the real TEXT resource (got_font.c
// owns loading that resource and indexing into it -- this function just blits whichever 72 bytes
// it's handed, exactly mirroring the real xtext(x,y,pagebase,buff,color) signature and call
// contract, where real xprint() passes `text[ch-32]` as buff). Byte layout: 4 sequential 18-byte
// VGA mode-X bitplanes, each row-major 2 (local columns) x 9 (rows) -- glyph column c reads from
// plane (c%4), local column (c/4). See got_font.c's own comment for the full verification writeup
// (several other candidate geometries tried first, all produced visual noise; this one produced
// clean, correct letterforms for all 94 real glyphs). Nonzero glyph bytes are opaque (drawn in
// `color`); zero bytes are transparent (skipped) -- same convention xfput()/modex_draw_sprite_frame()
// already use for their own transparency, just a 0/1 mask here instead of a palette index.
void xtext(int x, int y, unsigned int pagebase, char *buff, int color) {
	const unsigned char *g = (const unsigned char *) buff;
	int row, col;
	if (!g) {
		return;
	}
	for (row = 0; row < 9; ++row) {
		for (col = 0; col < 8; ++col) {
			int idx = (col & 3) * 18 + row * 2 + (col >> 2);
			if (g[idx]) {
				xpset(x + col, y + row, pagebase, color);
			}
		}
	}
}

void xcopyd2dmasked(int SourceStartX, int SourceStartY, int SourceEndX, int SourceEndY,
					 int DestStartX, int DestStartY, MaskedImage *Source,
					 unsigned int DestPageBase, int DestBitmapWidth) {
	(void) SourceStartX; (void) SourceStartY; (void) SourceEndX; (void) SourceEndY;
	(void) DestStartX; (void) DestStartY; (void) Source; (void) DestPageBase;
	(void) DestBitmapWidth;
	static bool warned = false;
	if (!warned) {
		LOGW("xcopyd2dmasked() not implemented yet -- sprite masking representation not "
			 "designed yet, see modexgl.c");
		warned = true;
	}
}

void xcopyd2dmasked2(int SourceEndX, int SourceEndY, int DestStartX, int DestStartY,
					  MaskedImage *Source, unsigned int DestPageBase) {
	(void) SourceEndX; (void) SourceEndY; (void) DestStartX; (void) DestStartY;
	(void) Source; (void) DestPageBase;
	static bool warned = false;
	if (!warned) {
		LOGW("xcopyd2dmasked2() not implemented yet -- see modexgl.c");
		warned = true;
	}
}

unsigned int xcreatmaskimage(MaskedImage *ImageToSet, unsigned int DispMemStart, char *Image,
							  int ImageWidth, int ImageHeight, char *Mask) {
	(void) ImageToSet; (void) DispMemStart; (void) Image; (void) ImageWidth; (void) ImageHeight;
	(void) Mask;
	static bool warned = false;
	if (!warned) {
		LOGW("xcreatmaskimage() not implemented yet -- see modexgl.c");
		warned = true;
	}
	return 0;
}

unsigned int xcreatemaskimage(MaskedImage *ImageToSet, unsigned int DispMemStart, char *Image,
							   int ImageWidth, int ImageHeight, char *Mask) {
	return xcreatmaskimage(ImageToSet, DispMemStart, Image, ImageWidth, ImageHeight, Mask);
}

unsigned int xcreatmaskimage2(MaskedImage *ImageToSet, unsigned int DispMemStart, char *Image,
							   int ImageWidth, int ImageHeight, char *Mask) {
	return xcreatmaskimage(ImageToSet, DispMemStart, Image, ImageWidth, ImageHeight, Mask);
}

// --- Palette + GL presentation ----------------------------------------------------------------

void modex_set_palette(const unsigned char rgb[256][3]) {
	memcpy(palette, rgb, sizeof(palette));
	palette_initialized = true;
}

// --- Sprite drawing (port-specific -- see modex.h) ---------------------------------------------

void modex_draw_sprite_frame(int x, int y, unsigned int pagebase, const unsigned char *chunky,
							  int width, int height) {
	int row, col;
	for (row = 0; row < height; ++row) {
		for (col = 0; col < width; ++col) {
			unsigned char v = chunky[row * width + col];
			if (v == 0 || v == 15) {
				// Transparent -- see modex.h's comment on this function. Originally just v==0,
				// confirmed against WORMY (ACTOR1); wootbeer then reported a white box around SKUNK
				// (ACTOR6) once she started moving, which turned out to be real, not a bug in the
				// erase/redraw logic -- SKUNK's own frame data uses index 15 (palette color
				// (243,243,243), near-white) as ITS background, not 0. Same dual convention
				// already found for BPICS background tiles (notes section 2a: "Transparent =
				// value 0 or 15, hardcoded in the real asm, not header-driven") -- turns out that
				// convention is shared by ACTOR sprite art too, not BPICS-specific as originally
				// assumed. Verified directly against both actors' real decompressed pixel data:
				// WORMY's frames are dominated by 0 (140-154 of 256 px) with no 15 in her border
				// pixels, SKUNK's are dominated by 15 (145-165 of 256 px) with barely any 0 at
				// all -- so both indices have to be treated as transparent unconditionally, not
				// per-actor, matching how xfput already treats BPICS tiles.
				continue;
			}
			xpset(x + col, y + row, pagebase, v);
		}
	}
}

// wootbeer: "when minimizing then restoring the game window, or locking the device then restoring, the
// view of the game will go solid white, but music will continue playing." Root cause: got_main.c's
// got_show_render_buffer() destroys and recreates the whole EGL context whenever Android tears down
// and rebuilds the render Surface (backgrounding/locking does exactly that) -- correct, and the only
// option, since a destroyed Surface's EGLSurface can't be reused -- but modex_texture below (this
// file's one GL texture object, created once by ensure_texture() the very first time
// modex_present_frame() ever runs) kept its old numeric name across that reset. That name belonged
// to the OLD, now-destroyed context; the new context has never heard of it, so ensure_texture()'s
// own `if (modex_texture != 0) return;` guard skipped recreating it, and modex_present_frame()'s
// glBindTexture()/glTexSubImage2D() calls ran against a texture object the new context silently
// fabricates on first bind, with no storage ever allocated for it (ensure_texture()'s own
// glTexImage2D() call, which allocates that storage, never ran again) -- an incomplete texture with
// no image data, which renders as flat white on this hardware. The music kept playing throughout
// because MediaPlayer (GotView.java) is a completely separate system from this GL state and was
// never touched by any of this.
void modex_reset_gl_state(void) {
	modex_texture = 0;
}

static void ensure_texture(void) {
	if (modex_texture != 0) {
		return;
	}
	glGenTextures(1, &modex_texture);
	glBindTexture(GL_TEXTURE_2D, modex_texture);
	// GL_NEAREST, not GL_LINEAR -- this is pixel art at a native 320x192, keep it crisp rather
	// than letting the fixed-function pipeline smear it when scaled up to the real screen size.
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// Allocate at the padded POT size once; only the real 320x192 region is ever written to or
	// sampled from (see MODEX_TEX_W/H comment up top).
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, MODEX_TEX_W, MODEX_TEX_H, 0, GL_RGB, GL_UNSIGNED_BYTE,
				 NULL);
}

void modex_set_display_mode(ModexDisplayMode mode) {
	s_display_mode = mode;
}

void modex_set_menu_pillarbox(bool enabled) {
	s_menu_pillarbox = enabled;
}

void modex_set_mirror_mode(bool enabled) {
	s_mirror_mode = enabled;
}

void modex_set_mirror_exclude_rect(bool active, int x1, int y1, int x2, int y2) {
	s_mirror_exclude_active = active;
	s_mirror_exclude_x1 = x1;
	s_mirror_exclude_y1 = y1;
	s_mirror_exclude_x2 = x2;
	s_mirror_exclude_y2 = y2;
}

void modex_set_screen_shake_offset(int dx, int dy) {
	s_shake_offset_x = dx;
	s_shake_offset_y = dy;
}

void modex_set_screen_size(int width, int height) {
	s_screen_w = width;
	s_screen_h = height;
}

// Jormangund boss-fight overhaul follow-up (wootbeer, after playtesting the bridge-crossing fix: "the
// boss' health bar is 'off' screen when using the force 4:3 pillarboxed mode we added"). Root cause:
// got_hud.c's screen-space GL overlays (the boss health bar chief among them) anchor themselves
// against the FULL physical screen (s_screen_w/s_screen_h, as passed to got_hud_init()) -- correct
// for Modes B/C, which really do stretch the game to fill the whole screen edge to edge, but wrong
// for Mode A: this function's own sibling modex_present_frame() shrinks the actual playfield+panel
// quad to a smaller, centered, true-4:3 sub-rect there instead, leaving black pillarbox/letterbox
// bars in the margin -- so a screen-space overlay anchored to the full screen's own right edge (the
// boss health bar's own real position, see got_hud_draw_boss_health()'s own comment) ends up sitting
// out in that black margin, well to the right of where the game itself is actually being drawn,
// rather than at the game's own visible right edge like it's supposed to.
//
// Returns the actual on-screen pixel rect (physical screen pixels, origin top-left, same convention
// got_hud_init()'s own screen_w/screen_h already use) the game is really being drawn into for the
// CURRENTLY selected display mode: the full screen for Modes B/C (matching their own always-fill-the-
// viewport behavior), or Mode A's own smaller centered sub-rect -- deliberately computed with the
// exact same half_x/half_y clip-space math modex_present_frame() itself uses just below (converted to
// pixels rather than clip-space coordinates), so this can never drift out of sync with where that
// function is actually drawing the quad. Callers with a screen-space overlay of their own that needs
// to line up with the visible game area (rather than the physical screen edges, which is the RIGHT
// thing for e.g. got_controls.c's own touch D-pad/buttons -- those stay reachable at the physical
// screen's own corners regardless of pillarboxing, deliberately unaffected by this function) should
// use this instead of assuming the full screen.
void modex_get_playfield_rect(int *out_x, int *out_y, int *out_w, int *out_h) {
	int x = 0, y = 0, w = s_screen_w, h = s_screen_h;
	if (s_display_mode == MODEX_DISPLAY_MODE_A_PILLARBOX && s_screen_w > 0 && s_screen_h > 0) {
		float target_aspect = (float) MODEX_WIDTH / (float) FULL_HEIGHT_WITH_PANEL; // 320/240 = 4/3,
																					  // see modex_present_frame()
		// modex_effective_screen_aspect(), not a raw s_screen_w/s_screen_h divide, so this stays in
		// sync with modex_present_frame()'s own quad even while s_debug_force_4_3_screen is on --
		// otherwise the boss health bar (this function's own main caller, got_hud.c) would anchor to
		// the real device's own aspect while the quad it's supposed to line up with is actually being
		// drawn pillarboxed to the simulated one, and visibly drift apart.
		float screen_aspect = modex_effective_screen_aspect();
		float half_x = 1.0f, half_y = 1.0f;
		if (screen_aspect > target_aspect) {
			half_x = target_aspect / screen_aspect; // wider screen than 4:3 -- pillarbox (L/R bars)
		} else if (screen_aspect < target_aspect) {
			half_y = screen_aspect / target_aspect; // taller screen than 4:3 -- letterbox (T/B bars)
		}
		w = (int) (half_x * (float) s_screen_w);
		h = (int) (half_y * (float) s_screen_h);
		x = (s_screen_w - w) / 2;
		y = (s_screen_h - h) / 2;
	}
	if (out_x) *out_x = x;
	if (out_y) *out_y = y;
	if (out_w) *out_w = w;
	if (out_h) *out_h = h;
}

void modex_present_frame(void) {
	if (!palette_initialized) {
		xsetmode(); // seeds the default grayscale palette as a fallback -- see its comment
	}
	ensure_texture();

	// Modes A/B show the real authentic panel (got_panel.c draws it into MODEX_PANEL_PAGE every
	// frame, BEFORE this function runs -- see got_main.c's own render-loop call site) stacked below
	// the playfield, composited into one combined 320x240 image; Mode C is playfield-only, exactly
	// as this function always behaved before this feature existed.
	bool panel_included = (s_display_mode != MODEX_DISPLAY_MODE_C_MINIMAL);
	int tex_height = panel_included ? FULL_HEIGHT_WITH_PANEL : MODEX_HEIGHT;

	const unsigned char *page = page_buffers[display_page_index];
	int i;
	// Mirror Mode: a horizontal flip of the PLAYFIELD rows only, done here at upload time rather than
	// by writing flipped pixels back into page_buffers itself. That matters for two reasons: (1) every
	// other draw/erase call site (tile blits, sprite erase-then-redraw, collision math) keeps reading
	// and writing this same buffer in normal, un-mirrored coordinates -- nothing about how the world is
	// drawn or tracked has to change, only how this one already-finished frame is read out to the
	// screen; (2) the pause menu/item picker/dialogue box/ASK prompt all paint their own box directly
	// into this exact same GOT_PAGE0 buffer with no separate compositing step (see got_main.c's own
	// comment on that), so flipping indiscriminately on a frame where one of those is baked in would
	// flip that UI right along with the world, which wootbeer was explicit must stay un-flipped.
	// s_mirror_exclude_active/s_mirror_exclude_x1..y2 (modex_set_mirror_exclude_rect(), modex.h) is
	// exactly how that's avoided WITHOUT switching mirroring off while one of those is open (the
	// original round-1 approach wootbeer found "pretty noticeable"): within that rect, this loop reads the
	// SAME column it's writing (a direct, un-mirrored passthrough) instead of the mirrored one, so the
	// box stays pinned to its normal on-screen position and orientation while every column outside it
	// still mirrors normally -- the world visible around the box's edges stays flipped right up to
	// where the box begins. The status panel (MODEX_PANEL_PAGE, copied separately just below) and the
	// minimal-HUD/D-pad screen-space overlays (drawn by got_main.c well after this function returns)
	// are never touched by this loop at all regardless, so they're un-flipped for free either way.
	if (s_mirror_mode) {
		for (i = 0; i < MODEX_WIDTH * MODEX_HEIGHT; ++i) {
			int row = i / MODEX_WIDTH;
			int col = i % MODEX_WIDTH;
			bool in_exclude_rect = s_mirror_exclude_active && col >= s_mirror_exclude_x1 &&
									col < s_mirror_exclude_x2 && row >= s_mirror_exclude_y1 &&
									row < s_mirror_exclude_y2;
			unsigned char idx = in_exclude_rect
									 ? page[i]
									 : page[row * MODEX_WIDTH + (MODEX_WIDTH - 1 - col)];
			rgb_scratch[i * 3 + 0] = palette[idx][0];
			rgb_scratch[i * 3 + 1] = palette[idx][1];
			rgb_scratch[i * 3 + 2] = palette[idx][2];
		}
	} else {
		for (i = 0; i < MODEX_WIDTH * MODEX_HEIGHT; ++i) {
			unsigned char idx = page[i];
			rgb_scratch[i * 3 + 0] = palette[idx][0];
			rgb_scratch[i * 3 + 1] = palette[idx][1];
			rgb_scratch[i * 3 + 2] = palette[idx][2];
		}
	}
	if (panel_included) {
		// Appended directly below the playfield rows in rgb_scratch -- real hardware shows this
		// same real 320x48 image glued to the bottom of whatever the playfield page currently is,
		// independent of that page's own flip state (see modex.h's own MODEX_PANEL_PAGE comment).
		const unsigned char *panel = page_buffers[page_index(MODEX_PANEL_PAGE)];
		int base = MODEX_WIDTH * MODEX_HEIGHT;
		for (i = 0; i < MODEX_WIDTH * PANEL_HEIGHT; ++i) {
			unsigned char idx = panel[i];
			rgb_scratch[(base + i) * 3 + 0] = palette[idx][0];
			rgb_scratch[(base + i) * 3 + 1] = palette[idx][1];
			rgb_scratch[(base + i) * 3 + 2] = palette[idx][2];
		}
	}

	glBindTexture(GL_TEXTURE_2D, modex_texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, MODEX_WIDTH, tex_height, GL_RGB, GL_UNSIGNED_BYTE,
					 rgb_scratch);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glEnable(GL_TEXTURE_2D);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	// One quad in clip space (-1..1 by default -- Modes B/C, full-bleed, exactly like every frame
	// before this feature existed), texcoords clamped to the real 320x(192 or 240) region within
	// the padded POT texture. v=0 at the top vertex, matching row 0 of page_buffers/rgb_scratch
	// being drawn as the top row of the image (glTexSubImage2D uploads row 0 of the source data as
	// texel row 0, i.e. v=0) -- if this ever renders upside down on-device, the fix is swapping the
	// v values below, not the buffer layout.
	float u_max = (float) MODEX_WIDTH / (float) MODEX_TEX_W;
	float v_max = (float) tex_height / (float) MODEX_TEX_H;

	float half_x = 1.0f, half_y = 1.0f;
	// Mode A (gameplay's own real 320x240/4:3 panel+playfield image) and modex_set_menu_pillarbox()
	// (the title/player-select/episode-select/credits/BBS-info/high-scores/story screens' own plain
	// 320x192/5:3 canvas, see that function's own modex.h comment) both want the same treatment: keep
	// this frame's own real aspect instead of stretching it to match the device's own (typically
	// wider) screen, by shrinking whichever clip-space axis would otherwise over-stretch the image and
	// leaving black bars in the margin instead. They're mutually exclusive in practice (got_main.c's
	// own render loop only ever sets Mode A during actual gameplay, and only ever enables the menu
	// pillarbox outside it), so at most one of these two conditions is ever true on a given frame.
	bool want_pillarbox = false;
	float target_aspect = 1.0f;
	if (s_display_mode == MODEX_DISPLAY_MODE_A_PILLARBOX) {
		target_aspect = (float) MODEX_WIDTH / (float) tex_height; // 320/240 = 4/3
		want_pillarbox = true;
	} else if (s_menu_pillarbox) {
		target_aspect = (float) MODEX_WIDTH / (float) MODEX_HEIGHT; // 320/192 = 5/3 -- this screen's
																	  // own real aspect (tex_height is
																	  // already MODEX_HEIGHT here too,
																	  // Mode C never includes the panel)
		want_pillarbox = true;
	}
	if (want_pillarbox && s_screen_w > 0 && s_screen_h > 0) {
		float screen_aspect = modex_effective_screen_aspect();
		if (screen_aspect > target_aspect) {
			half_x = target_aspect / screen_aspect; // wider than target -- pillarbox (L/R bars)
		} else if (screen_aspect < target_aspect) {
			half_y = screen_aspect / target_aspect; // taller than target -- letterbox (T/B bars)
		}
		// The quad no longer necessarily covers the full clip-space square, and
		// EGL_BUFFER_PRESERVED (got_main.c's own gotMain() setup) means whatever was drawn in the
		// margin last frame would otherwise just sit there -- clear to black first every frame either
		// of these is active instead of tracking "did the mode/aspect just change" across frames.
		glClear(GL_COLOR_BUFFER_BIT);
	}

	// Screen-shake -- see modex_set_screen_shake_offset()'s own modex.h comment. A straight clip-space
	// translation of the whole quad: MODEX_WIDTH playfield pixels span the full 2.0 clip-space units
	// (-1..1) at half_x==1.0, so `shake_x` scales down proportionally whenever pillarboxing has
	// already shrunk half_x below that (keeping the shake visually the same NUMBER of playfield pixels
	// regardless of pillarbox/letterbox state, not a fixed clip-space amount that would look larger or
	// smaller depending on the device's own aspect). tex_height (not MODEX_HEIGHT) for the Y axis,
	// matching every other Y conversion in this function -- the quad's own clip-space height always
	// corresponds to tex_height playfield-pixel rows (240 with the panel, 192 without), never a fixed
	// MODEX_HEIGHT. Y is negated: this quad's own top-left vertex is (-half_x, +half_y) (clip-space Y
	// increases upward), while `dy` arrives in ordinary screen/page-buffer pixel convention (Y
	// increases downward, same as every sprite x/y in this port) -- so a positive `dy` (shake "down")
	// has to SUBTRACT from clip Y to move the image down on screen, not add.
	float shake_x = (float) s_shake_offset_x / (float) MODEX_WIDTH * 2.0f * half_x;
	float shake_y = (float) s_shake_offset_y / (float) tex_height * 2.0f * half_y;

	GLfloat vertices[] = {
			-half_x + shake_x,  half_y - shake_y, // top-left
			 half_x + shake_x,  half_y - shake_y, // top-right
			-half_x + shake_x, -half_y - shake_y, // bottom-left
			 half_x + shake_x, -half_y - shake_y, // bottom-right
	};
	GLfloat texcoords[] = {
			0.0f,    0.0f,
			u_max,   0.0f,
			0.0f,    v_max,
			u_max,   v_max,
	};
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}
