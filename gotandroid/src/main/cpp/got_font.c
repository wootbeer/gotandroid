// Real bitmap-font text rendering -- see got_font.h for the overall design (a direct port of real
// xprint(), drawing into the page buffer via modex.h's xtext()).
//
// The font DATA is real, recovered byte-for-byte from the user's own GOTRES.DAT "TEXT" resource --
// not invented. Its resource name and per-glyph size (94 entries, 72 bytes each) come straight from
// reading utility/xsprite.c's own font-loading code (`char far text[94][72]; ...
// res_read("TEXT",(char far *) text);`), the one place in the available DOS C source that shows how
// this resource is used. What that source does NOT show is the internal byte layout *within* each
// 72-byte glyph -- the actual per-pixel blitter, xtext(), only ever existed as compiled assembly
// (utility/g_asm.asm's xtext_plane and friends), never as C, in every source release checked. That
// layout was reverse-engineered here instead, empirically, against the real decompressed resource
// bytes: dumped every glyph through several candidate geometries (row-major 8x9, column-major 8x9,
// 9x8 both ways, 6x12, 4x18, ...) and rendered each as ASCII art. All of those produced visual
// noise. The one that didn't: treating each 72-byte glyph as 4 sequential 18-byte blocks (matching
// this exact same VGA mode-X "4 sequential planes" convention this project's own xfput() tile
// blitter already uses for BPICS1 background tiles, see modexgl.c's comment on that function -- not
// a coincidence, real mode X hardware addresses everything in 4-pixel-wide interleaved groups),
// each block row-major 2 (local columns) x 9 (rows), with glyph column c mapping to plane (c%4) and
// local column (c/4) -- i.e. byte index = (c%4)*18 + row*2 + c/4 for row 0-8, col 0-7. That
// geometry produced clean, immediately recognizable letterforms for every one of the 94 real
// glyphs -- checked by eye against the full run (space through the '0'-'9'/'A'-'Z'/'a'-'z' range
// plus punctuation): 'O' a clean oval, '8' a clean figure-eight, ']' a clean bracket, '.' a single
// dot at the baseline, and so on -- not just "a" self-consistent decoding but visibly *the*
// correct one. See /tmp/dump_text5.c (this session's verification harness, reusing this project's
// own res_man.c/lzss.c against the real GOTRES.DAT) for the exact dump this was confirmed against.
//
// One more real thing this confirms: the glyph range is ASCII 32-125 (94 glyphs, index = ch-32),
// NOT the narrower 32-93 that utility/xsprite.c's own `if(ch>31 && ch<94)` guard checks -- that
// guard is checking the *character value*, not the glyph *count* (94), so as written it would wrongly
// exclude every lowercase letter and the {|} punctuation this resource actually has real, correct
// glyphs for. Since xsprite.c is a level-editor dev tool (not the shipped game), and its own strings
// are all-uppercase so the bug never shows up there, this port trusts the resource's own actual
// content (verified by rendering it) over that one utility file's copy of the range check.

#include "got_font.h"
#include "res_man.h"
#include "modex.h"

#include <android/log.h>
#include <stdlib.h>

#define LOG_TAG "GotFont"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define GLYPH_BYTES 72
#define GLYPH_ADVANCE 8 // real xprint()'s fixed per-character advance, regardless of glyph width

static unsigned char *s_font_data = NULL; // never freed -- see got_font.h
static long s_font_glyph_count = 0;

void got_font_init(void) {
	unsigned char *buf = NULL;
	long len = res_read("TEXT", &buf);
	if (len < GLYPH_BYTES || buf == NULL) {
		LOGI("got_font_init: TEXT read failed (%ld) -- got_xprint() will draw nothing", len);
		if (buf) {
			free(buf);
		}
		return;
	}
	s_font_data = buf;
	s_font_glyph_count = len / GLYPH_BYTES;
	LOGI("got_font_init: real TEXT font loaded (%ld glyphs, ASCII 32-%ld)", s_font_glyph_count,
		 31 + s_font_glyph_count);
}

bool got_font_ready(void) {
	return s_font_data != NULL;
}

// Real dialog_color[]={14,54,120,138,15,0,0,...} (1_main.c) -- the same 16-entry palette
// got_dialogue.c's own per-character typewriter reveal already uses (see that file's own
// s_dialog_color and its header comment for the full real citation) -- duplicated here, not shared
// via a header, since the two are separate translation units and this is a fixed, never-changing
// literal table ported once from the real source, not state that could ever drift.
static const int s_dialog_color[16] = {14, 54, 120, 138, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// Shared by got_xprint() and got_text_width() -- real xprint()'s own escape-parsing loop (1_grp.c/
// 2_grp.c):
//   while(*string){
//     ch=*string++;
//     if(ch=='~' && isxdigit(*string)){
//       ch=*string++;
//       if(isdigit(ch)) ch-=48; else ch=toupper(ch)-55;
//       color=dialog_color[ch];
//       continue;
//     }
//     if(ch>31 && ch<127) { ...draw...; }
//     x+=8;
//   }
// Bugfix (wootbeer: "there's an npc you talk to with the dialog, 'do you have a reservation' part of it
// is showing up as '1dr. thor 0'. instead of 'dr. thor' in red" -- the real SPEAK2 ASK option text
// is `"I'm sorry, I meant ~1Dr. Thor~0"`, real select_option() (1_panel.c/2_panel.c) draws every ASK
// option with a single plain xprint() call, so this is real xprint()'s own escape syntax, not the
// separate per-character typewriter reveal got_dialogue.c's SAY/TEXT boxes use). This function used
// to look for a completely different, NEVER-real `"/NN/"` two-digit-slash escape instead of the real
// single-hex-digit `"~N"` one -- traced to a source mix-up: got_font.h's own header comment cites
// "the level-editor's own xprint()" (utility/xsprite.c, a separate, never-shipped tool, not the
// shipped game) as this escape's origin, which does use "/NN/" for its own unrelated purpose, but the
// game's own REAL xprint() (1_grp.c/2_grp.c, the one select_option()/every other real caller in the
// shipped game actually links against) has never had that syntax at all -- only the "~N" one shown
// above. Since this function only ever recognized "/NN/", every literal "~" character in any real
// script string this port passes to got_xprint() (ASK options, ITEMSAY item names, any other script
// text drawn as a whole line rather than typewritten) fell through as an ordinary character instead
// of an escape -- and since the real TEXT font resource's own glyph range is ASCII 32-125 (see this
// file's own top comment), "~" (126) is one past its last real glyph, so it silently drew nothing
// while still advancing the cursor 8px, leaving an invisible gap where the tilde was and printing the
// hex-digit color-selector character right after it as ordinary visible text -- exactly wootbeer's
// "1dr. thor 0" (tildes invisible, "1"/"0" visible, no color change ever applied). Now parses the
// real "~N" syntax instead, sharing the exact same `s_dialog_color[]` table and hex-digit decoding
// got_dialogue.c's own reveal_one_char() already uses correctly for typewritten text -- both real
// escape consumers now agree. When `draw` is true, also blits each real character via xtext(); either
// way returns the total advanced width in pixels.
static int scan_text(int x, int y, const char *text, unsigned int page, int color, bool draw) {
	int start_x = x;
	unsigned char ch;

	if (!text) {
		return 0;
	}
	while (*text) {
		ch = (unsigned char) *text++;
		if (ch == '~' && ((text[0] >= '0' && text[0] <= '9') || (text[0] >= 'A' && text[0] <= 'F')
						   || (text[0] >= 'a' && text[0] <= 'f'))) {
			char h = *text++;
			int idx = (h >= '0' && h <= '9') ? (h - '0') : ((h | 0x20) - 'a' + 10);
			color = s_dialog_color[idx];
			continue;
		}
		if (draw && ch > 31 && ch < 32 + s_font_glyph_count) {
			xtext(x, y, page, (char *) (s_font_data + (long) (ch - 32) * GLYPH_BYTES), color);
		}
		x += GLYPH_ADVANCE;
	}
	return x - start_x;
}

void got_xprint(int x, int y, const char *text, unsigned int page, int color) {
	if (!s_font_data) {
		return;
	}
	x &= ~3; // real: `x &= 0xfffc;` -- mode-X 4-pixel address alignment
	scan_text(x, y, text, page, color, true);
}

int got_text_width(const char *text) {
	return scan_text(0, 0, text, 0, 0, false);
}

const unsigned char *got_font_glyph(unsigned char ch) {
	if (!s_font_data || ch <= 31 || ch >= 32 + s_font_glyph_count) {
		return NULL;
	}
	return s_font_data + (long) (ch - 32) * GLYPH_BYTES;
}
