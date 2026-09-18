#ifndef GOT_FONT_H_
#define GOT_FONT_H_

#include <stdbool.h>

// Real bitmap-font text rendering for GoT -- a direct port of real utility/xsprite.c's xprint(),
// built on top of modex.h's xtext() (now implemented for real in modex/modexgl.c, see that file's
// own comment on the byte-layout verification). Draws into the low-res 320x192 page buffer, same
// space as tiles/sprites/doors -- NOT a GL screen-space overlay like got_hud.c/got_controls.c,
// because the real options menu itself is part of GoT's own in-game rendering (select_option() in
// 1_panel.c draws its box and text with exactly these same xfillrectangle/xfput/xprint primitives),
// not a native-UI overlay on top of it. See got_menu.c/h for the menu built on top of this.

// Loads the real "TEXT" GOTRES.DAT resource (94 glyphs, ASCII 32-125, 72 bytes each) that
// got_xprint() draws with. Call once during startup resource loading, while the resource archive
// is open (got_main.c's got_draw_real_tiles(), same timing as its PALETTE/OBJECTS loads, bracketed
// by that function's own res_open()/res_close()). Persistent for the app's lifetime once loaded --
// never freed, same policy already established for s_bpics/s_sdat/s_objects. Not fatal if it
// fails: got_xprint() just draws nothing until a later successful call, the same tolerance
// got_draw_real_tiles() already gives OBJECTS/PALETTE.
void got_font_init(void);

// True once a real font has been loaded successfully.
bool got_font_ready(void);

// Direct port of real xprint(x,y,string,color,page) (1_grp.c/2_grp.c, the shipped game's own
// version -- see got_font.c's own comment on a previous mix-up with a different, never-shipped
// level-editor xprint()): draws `text` starting at pixel (x,y) into the given page buffer, one 8x9
// glyph per character via xtext(), advancing x by a fixed 8px per character regardless of the
// glyph's own (narrower) width -- matches the real fixed-advance behavior exactly. Supports the real
// inline color-escape syntax "~N" (a single hex digit 0-9/A-F right after a tilde) to switch `color`
// mid-string without ending the call, same as real xprint() and the same escape/table
// (`s_dialog_color[]`) got_dialogue.c's own per-character typewriter reveal already uses for SAY/
// TEXT/ITEMSAY boxes -- this is that same real per-script-string convention, just reaching a second
// real caller. Characters outside the real font's actual ASCII 32-125 range are skipped (x still
// advances) -- see got_font.c's own comment on why that range is 32-125, not the narrower 32-93 a
// similarly-named check in the one available xprint() source copy (utility/xsprite.c, a level-editor
// tool, not the shipped game) uses. No-ops entirely if got_font_ready() is false.
void got_xprint(int x, int y, const char *text, unsigned int page, int color);

// The real fixed-width advance a got_xprint() call of this string would use (escape sequences not
// counted, matching real xprint()'s own behavior) -- same math real select_option() does inline
// (`s=strlen(title)*8; i=(320-s)/2;`) to center a line, exposed here so callers don't have to
// duplicate the escape-parsing loop just to measure a string.
int got_text_width(const char *text);

// Returns a pointer to the raw 72-byte glyph data for `ch`, or NULL if the font isn't loaded or
// `ch` is outside the real font's actual range (see got_font.c's own comment on why that's
// 32-125). Exposed for got_dialogue.c's typewriter renderer, which needs to draw one character at
// a time at its own pace (real display_speech()'s per-character xtext1()+xtext() two-pass draw,
// for the drop-shadow effect -- see got_dialogue.c) rather than a whole string at once the way
// got_xprint() does.
const unsigned char *got_font_glyph(unsigned char ch);

#endif
