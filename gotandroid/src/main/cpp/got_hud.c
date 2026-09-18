// Minimal/alternate HUD overlay for GoT ("mode C" in the status-panel design notes) -- health and
// magic bars plus jewels/keys/score counts, drawn as a slim translucent strip near the top of the
// screen. Reuses got_controls.c's own screen-space GL overlay technique verbatim (orthographic
// projection in real screen pixels, flat-color GL_TRIANGLE_STRIP quads, no textures) -- see that
// file's opening comment for why touch/HUD overlays have to be drawn this way rather than into
// GoT's low-res 320x192 page buffer.
//
// This is deliberately NOT an attempt at the real game's own fixed status panel (Modes A/B, now
// built -- see got_panel.c: real background art plus five ported display_*() functions from
// 1_panel.c, selectable from the options menu's own "Display Mode" row, got_menu.h's
// got_menu_display_mode()). Mode C (this file) is a smaller, intentionally different look Claude
// proposed and wootbeer accepted earlier in the project, kept as its own separate option rather than
// replaced now that the authentic panel exists -- it reclaims the fixed 48px panel reserve Modes
// A/B spend on the real art, trading authenticity for more playfield on a small handheld screen.
//
// One real gap this file used to work around no longer applies to Modes A/B (got_panel.c uses the
// real font system, built since): there was no ported font/text-rendering system yet when this
// file was first written (real 1_panel.c's own digit display goes through xprint(), which needed
// the real FONTEX-style bitmap font and xtext() this port hadn't built). Rather than pull that
// whole system in just to draw four small numbers, this file draws digits with a small custom
// 7-segment-style renderer built from the exact same flat-color quads everything else here already
// uses -- no new texture, no new resource, and a look that reads as "minimal/alternate" on its own
// terms rather than a rough approximation of the authentic panel. Kept as-is even though the real
// font exists now: it's still a smaller, self-contained renderer with no resource dependency, and
// switching it over would just make Mode C look like a worse copy of Mode B instead of its own thing.
//
// All of the underlying numbers (Thor's health, magic, jewels, keys, score) have been tracked
// correctly since the combat and pickups milestones; this file is a pure rendering layer called
// once per frame with those values, with no game-state logic of its own.

#include "got_hud.h"
#include "modex.h"

#include <GLES/gl.h>
#include <android/log.h>
#include <stdbool.h>

#define LOG_TAG "GotHud"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static int screen_w = 0, screen_h = 0;
static bool hud_initialized = false;

void got_hud_init(int w, int h) {
	screen_w = w;
	screen_h = h;
	hud_initialized = true;
	LOGI("got_hud_init: %dx%d screen", w, h);
}

// Draws one flat-color quad in screen-pixel space -- same technique got_controls.c's touch
// buttons use; the caller is responsible for having set glColor4f already.
static void draw_quad(float x, float y, float w, float h) {
	GLfloat vertices[] = {
			x,     y,
			x + w, y,
			x,     y + h,
			x + w, y + h,
	};
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// Standard 7-segment bitmasks (the same encoding digital-clock displays have used for decades, not
// something invented for this port): bit0=a(top) bit1=b(top-right) bit2=c(bottom-right)
// bit3=d(bottom) bit4=e(bottom-left) bit5=f(top-left) bit6=g(middle).
static const unsigned char SEGMENTS[10] = {
		0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};

// Draws one digit 0-9 in the cell (x,y)-(x+w,y+h) using up to 7 thin quads; `t` is segment
// thickness in the same screen-pixel units as x/y/w/h.
static void draw_digit(float x, float y, float w, float h, float t, int digit) {
	unsigned char seg;
	float half = h * 0.5f;
	float side_h = half - t * 1.5f; // shared height of every vertical (b/c/e/f) segment

	if (digit < 0 || digit > 9 || side_h <= 0.0f) {
		return;
	}
	seg = SEGMENTS[digit];
	if (seg & 0x01) draw_quad(x + t, y, w - 2.0f * t, t);                    // a: top
	if (seg & 0x02) draw_quad(x + w - t, y + t, t, side_h);                  // b: top-right
	if (seg & 0x04) draw_quad(x + w - t, y + half + t * 0.5f, t, side_h);    // c: bottom-right
	if (seg & 0x08) draw_quad(x + t, y + h - t, w - 2.0f * t, t);            // d: bottom
	if (seg & 0x10) draw_quad(x, y + half + t * 0.5f, t, side_h);            // e: bottom-left
	if (seg & 0x20) draw_quad(x, y + t, t, side_h);                         // f: top-left
	if (seg & 0x40) draw_quad(x + t, y + half - t * 0.5f, w - 2.0f * t, t);  // g: middle
}

// wootbeer: "instead of 1-6, can we shorthand the corresponding item names" (the active-item swatch
// below, see its own comment) -- a small self-contained 5x7 dot-matrix font, same "no resource
// dependency" reasoning as SEGMENTS/draw_digit() just above (this file's own top comment already
// explains why: no ported font/texture system is wired into this screen-space overlay path, and
// pulling one in just for six short labels would be a bigger change for a smaller win than just
// drawing them the same flat-quad way everything else here already is). Scoped to exactly the 16
// letters ITEM_LABELS (below) actually needs -- A/B/D/E/G/H/I/L/N/O/P/R/S/T/U/W -- not a full
// alphabet no caller here would ever use. Each letter is 7 rows of a 5-bit column mask (bit4 =
// leftmost column ... bit0 = rightmost), the same "hand-picked bitmap, not a real font resource"
// spirit as the SEGMENTS table above.
static bool font_rows(char c, unsigned char rows[7]) {
	switch (c) {
		case 'A': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x1F; rows[4]=0x11; rows[5]=0x11; rows[6]=0x11; return true;
		case 'B': rows[0]=0x1E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x1E; rows[4]=0x11; rows[5]=0x11; rows[6]=0x1E; return true;
		case 'D': rows[0]=0x1E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x11; rows[4]=0x11; rows[5]=0x11; rows[6]=0x1E; return true;
		case 'E': rows[0]=0x1F; rows[1]=0x10; rows[2]=0x10; rows[3]=0x1E; rows[4]=0x10; rows[5]=0x10; rows[6]=0x1F; return true;
		case 'G': rows[0]=0x0F; rows[1]=0x10; rows[2]=0x10; rows[3]=0x17; rows[4]=0x11; rows[5]=0x11; rows[6]=0x0F; return true;
		case 'H': rows[0]=0x11; rows[1]=0x11; rows[2]=0x11; rows[3]=0x1F; rows[4]=0x11; rows[5]=0x11; rows[6]=0x11; return true;
		case 'I': rows[0]=0x1F; rows[1]=0x04; rows[2]=0x04; rows[3]=0x04; rows[4]=0x04; rows[5]=0x04; rows[6]=0x1F; return true;
		case 'L': rows[0]=0x10; rows[1]=0x10; rows[2]=0x10; rows[3]=0x10; rows[4]=0x10; rows[5]=0x10; rows[6]=0x1F; return true;
		case 'N': rows[0]=0x11; rows[1]=0x19; rows[2]=0x15; rows[3]=0x15; rows[4]=0x13; rows[5]=0x11; rows[6]=0x11; return true;
		case 'O': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x11; rows[4]=0x11; rows[5]=0x11; rows[6]=0x0E; return true;
		case 'P': rows[0]=0x1E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x1E; rows[4]=0x10; rows[5]=0x10; rows[6]=0x10; return true;
		case 'R': rows[0]=0x1E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x1E; rows[4]=0x14; rows[5]=0x12; rows[6]=0x11; return true;
		case 'S': rows[0]=0x0F; rows[1]=0x10; rows[2]=0x10; rows[3]=0x0E; rows[4]=0x01; rows[5]=0x01; rows[6]=0x1E; return true;
		case 'T': rows[0]=0x1F; rows[1]=0x04; rows[2]=0x04; rows[3]=0x04; rows[4]=0x04; rows[5]=0x04; rows[6]=0x04; return true;
		case 'U': rows[0]=0x11; rows[1]=0x11; rows[2]=0x11; rows[3]=0x11; rows[4]=0x11; rows[5]=0x11; rows[6]=0x0E; return true;
		case 'W': rows[0]=0x11; rows[1]=0x11; rows[2]=0x11; rows[3]=0x15; rows[4]=0x15; rows[5]=0x1B; rows[6]=0x11; return true;
		default: return false; // unsupported char (e.g. a space) -- draws nothing, reads as a blank cell
	}
}

// Draws one letter filling the cell (x,y)-(x+w,y+h) as a 5(wide)x7(tall) grid of solid quads, one
// per "on" bit in font_rows() above -- deliberately solid blocks rather than the thin strokes
// draw_digit() uses (a dot-matrix look, not a 7-segment one), since letters need real curves/
// diagonals a segment display can't approximate.
static void draw_letter(float x, float y, float w, float h, char c) {
	unsigned char rows[7];
	int row, col;
	float cell_w, cell_h;

	if (!font_rows(c, rows)) {
		return;
	}
	cell_w = w / 5.0f;
	cell_h = h / 7.0f;
	for (row = 0; row < 7; ++row) {
		for (col = 0; col < 5; ++col) {
			if ((rows[row] >> (4 - col)) & 1) {
				draw_quad(x + (float) col * cell_w, y + (float) row * cell_h, cell_w, cell_h);
			}
		}
	}
}

// Draws `text` (a plain NUL-terminated C string, this file's own ITEM_LABELS entries only -- no
// general UTF-8/wrapping/etc. concerns here) right-aligned so its rightmost letter's right edge
// sits at `right_x`, same shape as draw_number_right_aligned() just below including the
// shortest-width-not-padded behavior, just letters instead of digits.
static void draw_text_right_aligned(float right_x, float y, float cell_w, float cell_h, float gap,
									 const char *text) {
	int n = 0;
	float x;
	int i;
	const char *p;

	for (p = text; *p; ++p) {
		++n;
	}
	x = right_x - (float) n * cell_w - (float) (n - 1) * gap;
	for (i = 0; i < n; ++i) {
		draw_letter(x, y, cell_w, cell_h, text[i]);
		x += cell_w + gap;
	}
}

// Real magic items' own short names, index matching thor_info.item's real 1-6 ordering (apple/
// lightning/boots/wind/shield/thunder -- see the active-item swatch's own comment, below) -- exactly
// the 6 names wootbeer gave directly. Index 0 is never read (active_item>0 gates every caller); index 7
// (the quest item slot) isn't in here at all -- see that swatch's own comment for why it still shows
// a plain number instead of a name.
// Index 8 ("HOURGLASS") is item 8, the Hourglass Enhancement -- see got_menu.h's own "Hourglass:"
// comment. Not a real magic item slot at all (real thor_info.item only ever runs 0-7), so it has no
// real short name to port -- wootbeer: "don't forget to add a tag in the minimal ui for the hourglass as
// well, it can just be HOURGLASS", the one label here longer than this array's previous 5-letter max
// (APPLE/LIGHT/BOOTS/THUND) -- see the active-item block's own itemSlot comment, below, for how that
// width is now sized off the actual active label instead of a fixed constant.
static const char *ITEM_LABELS[9] = {
		NULL, "APPLE", "LIGHT", "BOOTS", "WIND", "PROT", "THUND", NULL, "HOURGLASS",
};

// Draws `value` (clamped to non-negative) right-aligned so its rightmost digit's right edge sits
// at `right_x`, each digit `cell_w` wide with `gap` between digits -- the natural (shortest) width
// for the number, not padded to a fixed digit count.
static void draw_number_right_aligned(float right_x, float y, float cell_w, float cell_h,
									   float gap, float thickness, int value) {
	char digits[12];
	int n = 0;
	float x;
	int i;

	if (value < 0) {
		value = 0;
	}
	if (value == 0) {
		digits[n++] = 0;
	} else {
		while (value > 0 && n < (int) sizeof(digits)) {
			digits[n++] = (char) (value % 10);
			value /= 10;
		}
	}
	x = right_x - (float) n * cell_w - (float) (n - 1) * gap;
	for (i = n - 1; i >= 0; --i) {
		draw_digit(x, y, cell_w, cell_h, thickness, digits[i]);
		x += cell_w + gap;
	}
}

void got_draw_hud(int health, int magic, int jewels, int keys, int score, int active_item,
				   int active_object) {
	float margin, barH, barW, digitW, digitH, digitGap, digitT, iconSize, y, x, backingH;
	float jewelSlot, keySlot;

	// No longer read here -- the quest item slot (active_item==7) used to fall back to showing this
	// as a plain number, but wootbeer asked for that case to show nothing at all instead (see the active-
	// item block's own comment, below). Kept as a parameter anyway rather than touching every call
	// site's own signature for a display-only trim like this one.
	(void) active_object;

	if (!hud_initialized) {
		return;
	}

	// Everything below is sized as a fraction of the real screen dimensions, same approach
	// got_controls.c's own button layout uses -- no display-density lookup needed, and it scales
	// sanely across phone/tablet/handheld aspect ratios. ~3% of screen height for the strip itself
	// matches the "minimal HUD" design notes' own target (a slim overlay, not the real panel's 20%).
	margin = (float) screen_h * 0.015f;
	barH = (float) screen_h * 0.028f;
	barW = (float) screen_w * 0.14f;
	digitH = barH;
	digitW = digitH * 0.55f;
	digitGap = digitW * 0.3f;
	digitT = digitW * 0.16f;
	iconSize = barH * 0.75f;
	y = margin;
	backingH = barH + margin * 1.4f;

	// Fixed-width slots (reserving each field's real maximum digit count -- jewels 0-999, keys
	// 0-99) so the row doesn't visibly jump around as counts change; score is drawn right-aligned
	// to the screen edge instead, so it doesn't need a reserved slot of its own.
	jewelSlot = digitW * 3.0f + digitGap * 2.0f;
	keySlot = digitW * 2.0f + digitGap * 1.0f;

	if (health < 0) health = 0; else if (health > 150) health = 150;
	if (magic < 0) magic = 0; else if (magic > 150) magic = 150;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	// Orthographic projection in real screen pixels, origin top-left -- same convention
	// got_controls.c's own overlay uses, so this strip lines up with the rest of the screen.
	glOrthof(0.0f, (float) screen_w, (float) screen_h, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnableClientState(GL_VERTEX_ARRAY);

	// Backing strip -- a wide, low, translucent dark bar behind everything, so the HUD reads
	// clearly over any background art (grass, dungeon stone, water, etc.) instead of only some.
	glColor4f(0.0f, 0.0f, 0.0f, 0.4f);
	draw_quad(0.0f, 0.0f, (float) screen_w, backingH);

	x = margin;

	// Health bar -- dark track, then a proportional fill (real range 0-150).
	glColor4f(0.35f, 0.1f, 0.1f, 0.8f);
	draw_quad(x, y, barW, barH);
	glColor4f(0.85f, 0.25f, 0.2f, 0.95f);
	draw_quad(x, y, barW * ((float) health / 150.0f), barH);
	x += barW + margin;

	// Magic bar -- same idea, blue/cyan.
	glColor4f(0.1f, 0.15f, 0.35f, 0.8f);
	draw_quad(x, y, barW, barH);
	glColor4f(0.25f, 0.55f, 0.95f, 0.95f);
	draw_quad(x, y, barW * ((float) magic / 150.0f), barH);
	x += barW + margin * 2.5f;

	// Jewel count -- a small pink/magenta swatch standing in for a jewel icon (no icon resource is
	// wired into this overlay path), then the number in its reserved 3-digit slot.
	glColor4f(0.9f, 0.25f, 0.75f, 0.95f);
	draw_quad(x, y + (barH - iconSize) * 0.5f, iconSize, iconSize);
	x += iconSize + digitGap * 2.0f;
	glColor4f(0.95f, 0.95f, 0.95f, 0.95f);
	draw_number_right_aligned(x + jewelSlot, y, digitW, digitH, digitGap, digitT, jewels);
	x += jewelSlot + margin * 1.5f;

	// Key count -- swatch, then the number in its reserved 2-digit slot. wootbeer: "can we change the
	// color of the yellow square next to the number of keys to a gray since keys themselves are gray"
	// -- was yellow (a leftover from before the jewel/key swatches had distinct colors of their own);
	// gray now matches the real key sprite's own color instead of an arbitrary accent.
	glColor4f(0.7f, 0.7f, 0.7f, 0.95f);
	draw_quad(x, y + (barH - iconSize) * 0.5f, iconSize, iconSize);
	x += iconSize + digitGap * 2.0f;
	glColor4f(0.95f, 0.95f, 0.95f, 0.95f);
	draw_number_right_aligned(x + keySlot, y, digitW, digitH, digitGap, digitT, keys);
	x += keySlot + margin * 1.5f;

	// Active item -- shows the real magic item's own short name (thor_info.item 1-6, ITEM_LABELS at
	// this file's own top) whenever Thor has one active. Used to also draw a gold color swatch here
	// standing in for a missing icon, and to fall back to a plain number (the quest object's own real
	// 1-15 id, thor_info.object) for item==7, the quest item slot. wootbeer, both dropped in the same
	// round: "can we remove the yellow square next to the item names on the minimal hud" (the swatch
	// -- redundant now that the name itself is legible text, not a bare number needing a color cue to
	// tell it apart from jewels/keys) and "also don't show a number or anything when it's the quest
	// items on the minimal hud" (item==7 -- this HUD is deliberately minimal, and a number with no
	// label to explain what it's counting was worse than showing nothing). So: nothing at all for
	// item==7 or no active item, and just the plain text name -- no icon, no swatch -- for 1-6.
	// Item 8 (Hourglass Enhancement) follows the same "just the name" treatment as 1-6, not item 7's
	// "show nothing" -- it's a real user-selected active item from the picker's point of view, it just
	// isn't one of the six real inventory-bit items.
	if ((active_item >= 1 && active_item <= 6) || active_item == 8) {
		// itemSlot reserves enough width for the active label's own right edge to land in a stable
		// spot regardless of that label's actual length (same "right-aligned within a fixed-width
		// slot" reasoning the jewel/key digit groups above already use) -- 5 characters for items 1-6
		// (their real shared max width, APPLE/LIGHT/BOOTS/THUND), or the full "HOURGLASS" length for
		// item 8, which is longer than that shared width and would otherwise run into the key group
		// to its left if drawn into the same fixed 5-char slot.
		int labelChars = (active_item == 8) ? 9 : 5;
		float itemSlot = digitW * (float) labelChars + digitGap * (float) (labelChars - 1);
		glColor4f(0.95f, 0.95f, 0.95f, 0.95f);
		draw_text_right_aligned(x + itemSlot, y, digitW, digitH, digitGap, ITEM_LABELS[active_item]);
	}

	// Score -- right-aligned to the far right edge of the screen (real max 999999, 6 digits) so it
	// never collides with the jewel/key group even at its longest.
	glColor4f(0.95f, 0.9f, 0.6f, 0.95f);
	draw_number_right_aligned((float) screen_w - margin, y, digitW, digitH, digitGap, digitT,
							   score);

	glDisableClientState(GL_VERTEX_ARRAY);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // restore the default color state modex_present_frame() expects
	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
}

// Episode 1 boss round -- originally a wide horizontal bar centered at the top; moved to a vertical,
// right-edge-anchored bar for the Jormangund boss-fight overhaul (wootbeer: "the boss health bar, it
// looks ok but it should be drawn near the right edge of the screen, how the actual game does it.
// this is important because the second boss is covered by it a bit, so it should be on the right
// side"). Real boss_status() (1_panel.c) draws a fixed-pixel vertical 10-segment bar at screen x=304-
// 317/y=2-81 (a 320x192 real screen) -- i.e. flush against the right edge, filling roughly the top
// 41% of screen height and about 4% of its width; this port has no fixed low-res coordinate space to
// match literally (see this file's own top comment on why Mode C is screen-space, not page-buffer-
// space), so those same proportions are reused as fractions of the real device screen instead. Ten
// segments, same as before and as the real bar, each lit solid once the boss's remaining health drops
// to or below that segment's own threshold -- e.g. at 55/100 health, segments 10 down through 6
// (health<=100,90,...,60) are UNLIT (undamaged) and segments 5 down through 1 (health<=50,...,10 --
// real code's own `if(i*10>health) c=0; else c=32;`, i.e. LIT means "already past this much damage")
// are lit red, matching the real bar's own "lit == damage taken" sense exactly. Real code's own
// "filling from the bottom (segment i=1, lowest threshold) up, draining from the top (segment i=10,
// highest threshold) down as damage accrues" is preserved by stacking segment index 0 at the BOTTOM
// of the vertical bar and index 9 at the TOP, same threshold ordering as before -- just read downward
// on-screen instead of left-to-right.
void got_hud_draw_boss_health(int health, int max) {
	float margin, barW, barH, segGap, segH, x, y, topClearance, outlineT;
	int i;
	int px, py, pw, ph;

	if (!hud_initialized || max <= 0) {
		return;
	}
	if (health < 0) health = 0; else if (health > max) health = max;

	// wootbeer, after playtesting the bridge-crossing fix: "the boss' health bar is 'off' screen when
	// using the force 4:3 pillarboxed mode we added." This function used to size/position itself
	// against screen_w/screen_h directly -- the FULL physical screen -- which is correct for Display
	// Modes B/C (both really do stretch the game to fill the whole screen) but wrong for Mode A: the
	// game itself only occupies a smaller, centered, true-4:3 sub-rect there (modexgl.c's own
	// modex_present_frame()), with black pillarbox/letterbox bars in the margin -- so anchoring to
	// the full screen's own right edge put this bar out in that black margin, well to the right of
	// where the game was actually being drawn. modex_get_playfield_rect() (modex.h) returns that
	// same sub-rect (or the full screen, unchanged, for Modes B/C) -- see that function's own
	// comment. Every screen_w/screen_h below is now pw/ph (that rect's own width/height), and every
	// x/y origin is offset by px/py (its own top-left) -- same proportions as before, just measured
	// against the actual visible game area instead of the physical screen.
	modex_get_playfield_rect(&px, &py, &pw, &ph);

	margin = (float) ph * 0.015f;
	// Same top-strip sizing got_draw_hud() above uses (barH=screen_h*0.028f, backingH=that+margin*
	// 1.4f) -- recomputed here rather than shared as file-scope state, so this function stays
	// self-contained the same way it already was. Cleared by one more margin's worth of gap so the
	// boss bar's own top segment never touches the ordinary health/magic/jewels/score strip above it.
	topClearance = (float) ph * 0.028f + margin * 1.4f + margin;

	barW = (float) pw * 0.04f;   // real: 14px of 320 (317-304+1) =~ 4.4%
	barH = (float) ph * 0.40f;   // real: 80px of 192 (81-2+1) =~ 41.7%
	segGap = barH * 0.008f;
	segH = (barH - segGap * 9.0f) / 10.0f;
	x = (float) px + (float) pw - margin - barW; // right-edge-anchored (of the playfield, not
												  // necessarily the physical screen), matching real's
												  // own flush-right x=304-317
	y = (float) py + topClearance;
	outlineT = barW * 0.15f;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrthof(0.0f, (float) screen_w, (float) screen_h, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnableClientState(GL_VERTEX_ARRAY);

	// Outer frame -- a dark backing rect a shade bigger than the segment column, standing in for real
	// boss_status()'s own nested 4-color rectangle frame.
	glColor4f(0.05f, 0.05f, 0.05f, 0.75f);
	draw_quad(x - outlineT, y - outlineT, barW + outlineT * 2.0f, barH + outlineT * 2.0f);

	for (i = 0; i < 10; ++i) {
		// Real: `if(i*10 > health) c=0; else c=32;` for i=10..1 -- segment i is LIT (c=32) once
		// remaining health covers its own threshold. Generalized from the real fixed 0-100 scale to
		// an arbitrary `max` so a future boss with a different real max health still lights the
		// correct proportion of segments. Index i stacks bottom-up (i=0 lowest threshold at the
		// bottom of the column, i=9 highest threshold at the top) -- see this function's own header
		// comment.
		int seg_threshold = (i + 1) * max / 10;
		bool lit = health >= seg_threshold;
		float segY = y + barH - (float) (i + 1) * (segH + segGap) + segGap;

		glColor4f(0.15f, 0.03f, 0.03f, 0.85f); // unlit segment track
		draw_quad(x, segY, barW, segH);
		if (lit) {
			glColor4f(0.85f, 0.15f, 0.1f, 0.95f); // lit -- remaining health still covers this segment
			draw_quad(x, segY, barW, segH);
		}
	}

	glDisableClientState(GL_VERTEX_ARRAY);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
}
