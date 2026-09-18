// New-game "story" sequence -- see got_story.h for the overall design/scope. Real source: directly
// read _g1's/_g2's/_g3's own 1_init.c/2_init.c/3_init.c story() functions side by side (identical
// shape/constants in all three -- only the STORYn text resource name, the real Odin-face/hammer
// icon coordinates, and the chapter-title strings differ). Real story() draws onto real VGA mode-X
// hardware pages via literal page-flip scrolling (`display_page=add; xshowpage(add); add+=80;`),
// stacking its own real OPENP1-12 background strips into two 320x240 backdrops, deliberately
// overlapped with real level/actor loading for load-time reasons this port has no equivalent of --
// none of that raw mechanism is ported here. This port draws the real STORYPIC icons and the real
// STORYn text (exact real layout: x=8,y=2 start/10px line pitch/8px fixed char advance) directly onto
// a plain black backdrop, with this port's own ordinary immediate-mode per-frame drawing and wootbeer's
// own explicitly requested UX (a discrete per-line Up/Down scroll like the credits screen, Fire to
// advance, action3.mp3 under the story text, silence under the chapter title).
//
// Two rounds of on-device feedback from wootbeer shaped this file:
//   1) "the colors are all funky... instead of trying to recreate the background exactly, let's just
//      do the same wood/green frame and brown background every other menu uses" -- tried, but the
//      very next round: "the frame and brown background look horrible, get rid of the frame, and
//      make the background black." Both real OPENP1-12 and the rope-bordered-box treatment are gone;
//      story_draw_text() just fills GOT_PAGE0 black, matching the chapter-title card that follows it.
//   2) "the colors on odin and the hammer icon look messed up still, almost inverted... I don't think
//      the different colors of text from the example are represented, there should be a blue a
//      yellow and a red." The actual real cause, missed by round 1: real story() loads and applies a
//      THIRD, separate real palette resource, "STORYPAL", before drawing any of STORYPIC or the real
//      STORYn text -- `res_read("STORYPAL",pbuff); pbuff[0]=pbuff[1]=pbuff[2]=0; set_palette();`
//      (1_init.c's own story(), identical in g2/g3) -- and restores the normal game palette
//      (`load_palette();`) right before returning, i.e. before the chapter-title card ever draws.
//      STORYPIC and the real STORYn "/NNN/" color escapes were authored against STORYPAL's own
//      indices, not the normal in-game PALETTE this port already had loaded -- confirmed directly:
//      STORYPAL's own raw bytes at indices 72/208/209/210 (the only 4 values the 3 real STORYn
//      scripts' own escapes ever use) decode to a warm off-white body tone and real red/yellow/blue
//      highlights, while those same indices under the normal PALETTE are unrelated colors. See
//      got_main.c's own s_storypal comment for the load-time details (6-bit->8-bit scaling, index 0
//      forced black) and got_story_begin()/got_story_update() below for where this port now switches
//      to STORYPAL and back, mirroring real story()'s own two set_palette()-equivalent calls.
//
// Also the one real place in the whole DOS source with a fourth inline color-escape syntax, "/NNN/"
// (three ASCII digits between two slashes, a literal 0-255 real STORYPAL index) -- confirmed
// directly in this same story() function:
//   else if(*p=='/' && *(p+4)=='/'){ p++; s[0]=*p++; s[1]=*p++; s[2]=*p++; s[3]=0; color=atoi(s); }
// This is DIFFERENT from both got_font.c's real "~N" (single hex digit, xprint()'s own real escape,
// section 123) and the never-shipped level-editor "/NN/" got_font.c's own header comment once
// mistakenly attributed to xprint() -- multiple genuinely different real/almost-real escape
// conventions turned up in this codebase's own history, and this is the one time "/NNN/" really is
// real, just scoped to this one function rather than the general-purpose text renderer.

#include "got_story.h"
#include "got_font.h"
#include "modex.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

extern volatile char key_flag[100];
extern const unsigned char *got_get_storypic(void);
extern const unsigned char *got_get_story_text(int episode, long *out_len);
extern const unsigned char (*got_get_storypal(void))[3]; // real STORYPAL, only while the
														   // story-text screen is up -- see this
														   // file's own top comment and
														   // got_main.c's own s_storypal comment
extern const unsigned char (*got_get_game_palette(void))[3]; // the normal game palette, restored
															   // the moment the chapter-title card
															   // shows -- same two getters this
															   // file's own top comment describes
extern void got_play_music(int num, bool override);
extern void got_pause_music(void);

// Same real scancodes got_title.c/got_menu.c already hand-copy locally rather than sharing a header
// -- see got_title.c's own top-of-file comment on why (this project's established convention for
// this handful of values rather than a shared scancode header).
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_FIRE 56

// Matches got_main.c's/got_title.c's/got_menu.c's own GOT_PAGE0 exactly.
#define GOT_PAGE0 3840u

// got_main.c's own MUSIC_STORY enum value, hand-copied here same as GOT_PAGE0 above (that enum is
// local to got_main.c, not shared via a header -- see its own comment).
#define GOT_MUSIC_STORY 10

#define STORY_VIEW_H 192 // modexgl.c's own MODEX_HEIGHT -- the title/menu canvas this draws onto
#define STORY_CANVAS_W 320

// Real x=8,y=2 start / 10px line pitch / 8px fixed char advance -- 1_init.c's/2_init.c's/3_init.c's
// own story(), identical in all three, used here completely unmodified now that this screen draws
// straight onto the full 320x192 canvas rather than inside a padded box. STORY_TEXT_TOP is nudged a
// little past the real y=2 start to leave a thin header strip for the "^" scroll marker (and
// STORY_VISIBLE_ROWS trimmed to leave a matching footer strip for "v") -- see this file's own
// STORY_ARROW_X comment for why those markers moved out of the text columns entirely.
#define STORY_TEXT_X0 8
#define STORY_TEXT_TOP 12
#define STORY_LINE_PITCH 10
#define STORY_CHAR_ADVANCE 8
#define STORY_VISIBLE_ROWS 17 // (192 - 12 header - 10 footer) / 10, rounded down -- comfortably
							   // covers every real line width (confirmed: the longest of all 3
							   // episodes' own real lines is 38 characters, 304px, well inside 320)
							   // without the old box ever needing to narrow it
#define STORY_MAX_LINES 46 // real: `while(i<46)` -- the real loop's own bound, kept here too even
							// though this port also (unlike real code) bounds by the actual real
							// STORYn buffer length, since real code trusts the buffer to contain at
							// least 46 real lines without ever checking (confirmed true for all 3
							// episodes -- 46/48/49 real CR bytes respectively -- but bounding by
							// length too is free insurance against that ever not holding for some
							// future re-export of these assets)

#define STORY_DEFAULT_COLOR 72 // real: `color=72;` -- story()'s own default before any /NNN/ escape,
								// a warm off-white under real STORYPAL (see this file's own top
								// comment) -- NOT a valid-looking color at all under the normal game
								// palette, which is exactly what made this screen look "funky"/
								// "almost inverted" before STORYPAL was wired up
#define STORY_SHADOW_COLOR 0 // real STORYPAL index 0, forced true black at load time (see
							  // got_main.c's own s_storypal comment) -- same shadow color the real
							  // game's own drop-shadow text always uses

// "^"/"v" scroll markers -- wootbeer: "keep the arrow scrolling that seems to look and work well."
// Placed in their own thin strips above/below the real text rows (STORY_TEXT_TOP's own top comment)
// rather than the text columns' own top-right/bottom-right corner the way got_title.c's own
// got_title_draw_credits() places them -- that box had a narrow content width with real margin to
// spare; this screen's real lines run up to 38 characters (304px) on a 320px-wide plain backdrop,
// so a same-row corner marker would sit on top of real text on this screen's own longer lines.
#define STORY_ARROW_X (STORY_CANVAS_W - 20)
#define STORY_ARROW_UP_Y 2
#define STORY_ARROW_DOWN_Y 182
#define STORY_ARROW_COLOR 14 // got_title.c's own ITEM_COLOR value -- not shared via a header, same
							  // hand-copy convention as GOT_PAGE0 above -- deliberately NOT one of
							  // real STORYPAL's own 4 in-use indices (72/208/209/210), so the markers
							  // stay a consistent yellow regardless of which real color the story
							  // text nearby happens to be using

// Real Odin-face (STORYPIC frame 0) / hammer-icon (frame 1) placement -- 1_init.c's own
// `xput(146,64,0u,back[0])`/`xput(24,88,19200u,back[1])`, g2's/g3's own `xput(146,16,0u,back[0])`
// (no hammer at all -- see kIconLayout's own comment). X positions are real, used unmodified.
// *_LINE below is each icon's own real Y reverse-engineered into this file's own unified line index
// (no more real two-page split to translate across): row=(y-2)/10 on real page 1, or
// 23+row=(y-2)/10 on real page 2 (line 23 is where real code's own page split lands) -- e.g. real
// hammer at page-2 y=88 -> row (88-2)/10=8.6 -> real absolute line 23+9=32, which is exactly
// STORY1's own real "Mjolnir, the Enchanted Hammer" line (confirmed by reading STORY1's own decoded
// text directly) -- wootbeer: "it needs to be next to the text in the box 'Mjolnir' as shown in the
// example." Both icons are drawn as part of the normal per-frame text walk below, at their own
// LINE's row, so they scroll with the text exactly like every other line instead of staying pinned
// to the screen -- consistent with hammer's own real position being meaningful only relative to one
// specific real line of text, not a fixed screen coordinate.
#define STORY_ODIN_X 146
#define STORY_HAMMER_X 24

typedef struct {
	int odin_line;
	int hammer_line; // -1 -- no hammer at all this episode
} StoryIconLayout;

// Index 0 unused (episodes are 1-based, matching every other episode-numbered table in this port).
// Episode 1 real Y=64 -> line 6; episodes 2/3 real Y=16 -> line 1 (both still real page-1 icons,
// no page-split math needed for those two). Episodes 2/3 never draw a hammer at all -- confirmed by
// reading _g2/2_init.c's and _g3/3_init.c's own story() functions side by side with _g1/1_init.c's:
// both are missing the `xput(24,88,...,back[1])` call entirely (and neither STORY2 nor STORY3's own
// real text even mentions Mjolnir) -- wootbeer: "of course episode ii and iii do not get the hammer
// icon," confirmed right.
static const StoryIconLayout kIconLayout[4] = {
	{0, -1},
	{6, 32},
	{1, -1},
	{1, -1},
};

// Real xprintx(13*8,13*8,"God Of Thunder",PAGE1,14) / xprintx(.,15*8,"Part N: ...",PAGE1,32) --
// 1_init.c's/2_init.c's/3_init.c's own initialize(), identical position/colors in all three (only
// the subtitle string differs), drawn under the normal game palette (real story()'s own
// load_palette() call already restored it by the time these run -- see this file's own top
// comment), which is why wootbeer's report ("on the title screen it looks good") never needed a palette
// fix. Real Y values (104/120) were tuned for real code's own 240px-tall page, though -- this port's
// canvas is only STORY_VIEW_H (192) tall, so those same absolute Y values sit visibly below center
// here ("the text is not centered on the screen on the y-axis"). STORY_TITLE_Y/STORY_SUBTITLE_Y
// below instead center the same real 16px-apart two-line block within this port's own actual canvas
// height, keeping the real relative spacing and real per-episode subtitle strings, just recomputed
// for the real Y this canvas actually has.
#define STORY_TITLE_COLOR 14
#define STORY_SUBTITLE_COLOR 32
#define STORY_TITLE_LINE_GAP 16 // real 120-104
#define STORY_TITLE_GLYPH_H 9 // got_font.c's own real glyph height
#define STORY_TITLE_Y ((STORY_VIEW_H - (STORY_TITLE_LINE_GAP + STORY_TITLE_GLYPH_H)) / 2)
#define STORY_SUBTITLE_Y (STORY_TITLE_Y + STORY_TITLE_LINE_GAP)

typedef enum {
	STORY_SCREEN_TEXT = 0,
	STORY_SCREEN_TITLE,
} StoryScreen;

static int s_episode = 1;
static StoryScreen s_screen = STORY_SCREEN_TEXT;
static int s_scroll_line = 0; // real story-text line index (not a pixel offset) currently scrolled
							   // to the top of the visible window
static int s_story_line_count = 0; // this episode's own real line count, capped at STORY_MAX_LINES,
									// computed once in got_story_begin() and used to clamp
									// s_scroll_line in story_draw_text() (same "draw function owns
									// its own clamp" shape got_title_draw_credits() already uses)
static bool s_finished = false;
static char s_prev_up = 0;
static char s_prev_down = 0;
static char s_prev_fire = 0;

void got_story_init(void) {
	// Real STORYPIC/STORYPAL/STORY1-3 loading lives in got_main.c's own got_load_real_resources()
	// (see that function's own comment, right alongside ODINPIC/HAMPIC) -- exposed here through
	// got_get_storypic()/got_get_storypal()/got_get_story_text(), same one-getter-per-resource shape
	// as got_get_hampic(). Nothing else needs startup-time setup.
}

// Counts this episode's own real story-text lines (by real CR bytes, same as the draw loop below),
// capped at STORY_MAX_LINES -- used only to clamp how far s_scroll_line can go. A trailing line with
// real text but no final CR (none of the 3 real STORYn buffers actually end this way, but nothing
// guarantees it) simply isn't counted, the same "free insurance, not a correctness requirement" spirit
// STORY_MAX_LINES's own comment already gives -- worst case, scrolling stops one line early.
static int story_count_lines(const unsigned char *text, long len) {
	int lines = 0;
	long p = 0;
	if (!text) {
		return 0;
	}
	while (lines < STORY_MAX_LINES && p < len) {
		if (text[p] == 13) {
			++lines;
		} else if (text[p] == '/' && p + 4 < len && text[p + 4] == '/') {
			p += 4; // skip the /NNN/ escape's own 3 digits -- doesn't affect line counting, just
					 // keeps this loop's own p in step with the real draw loop below
		}
		++p;
	}
	return lines;
}

void got_story_begin(int episode) {
	long len = 0;
	const unsigned char *text;
	const unsigned char (*storypal)[3];

	if (episode < 1 || episode > 3) {
		episode = 1;
	}
	s_episode = episode;
	s_screen = STORY_SCREEN_TEXT;
	s_scroll_line = 0;
	s_finished = false;

	text = got_get_story_text(episode, &len);
	s_story_line_count = story_count_lines(text, len);

	// Real story()'s own `res_read("STORYPAL",pbuff);...;set_palette();`, right before it draws
	// anything -- see this file's own top comment. Left as whatever's already active (the normal
	// game palette, at every point this screen is ever reached from) if STORYPAL never loaded.
	storypal = got_get_storypal();
	if (storypal) {
		modex_set_palette(storypal);
	}

	// Prime the edge-detectors with whatever's CURRENTLY held, rather than hardcoding "nothing
	// pressed." wootbeer: "bug with episode's 2 and 3's screens, only the title screen is showing, the
	// other first two screens I think are showing, but are flashing rapidly off the screen, probably
	// stray button presses" -- exactly right. got_title.c's own GOT_APP_EPISODE_SELECT confirms an
	// episode with the very same physical Fire press that calls got_story_begin() and switches to
	// GOT_APP_STORY; got_title.c's own s_prev_up/down/fire are file-level statics that are never
	// reset across ITS OWN screen-to-screen transitions (so a still-held Fire never misreads as a
	// fresh press there), but this file's own s_prev_fire used to hardcode to 0 right here -- if the
	// physical Fire press was still being read as held on this screen's very first
	// got_story_update() call (more likely for episodes 2/3, which need extra Down presses first to
	// reach their own row, meaning a slower, more deliberate Fire tap that's more likely to still be
	// down a frame later), `fire && !s_prev_fire` read that held press as a brand new one and
	// instantly advanced past the text screen -- then correctly waited for a real new press on the
	// chapter-title screen after that, matching exactly what wootbeer saw. Priming from the real current
	// key_flag[] state here removes the race outright: only a genuine release-then-press-again ever
	// counts as a new edge, matching how got_title.c's own trackers already behave.
	s_prev_up = key_flag[KEY_UP] != 0;
	s_prev_down = key_flag[KEY_DOWN] != 0;
	s_prev_fire = key_flag[KEY_FIRE] != 0;

	// wootbeer: "use action3 as the background music." override=true so this always (re)starts from
	// the top even if action3.mp3 happened to already be s_music_current from a previous story
	// screen this same session (real story()'s own `music_current=0;` right before its own
	// music_play() call is this same "force a restart" intent).
	got_play_music(GOT_MUSIC_STORY, true);
}

bool got_story_finished(void) {
	return s_finished;
}

// Real xtext1()+xtext() two-pass drop-shadow draw -- got_dialogue.c's own draw_char_shadowed()
// (real display_speech()'s per-character reveal) already established this exact pattern in this
// codebase; reused here rather than real story()'s own heavier 8-direction xtextx() outline, both
// for consistency with that existing precedent and because story text runs to several times as
// many on-screen characters as a dialogue box ever does, page after page -- an 8x-the-shadow-calls
// outline redrawn every single frame was not worth it for a look this port already has a cheaper,
// established equivalent for.
static void draw_char_shadowed(int x, int y, int ch, int color) {
	const unsigned char *glyph = got_font_glyph((unsigned char) ch);
	if (!glyph) {
		return;
	}
	xtext(x + 1, y + 1, GOT_PAGE0, (char *) glyph, STORY_SHADOW_COLOR);
	xtext(x, y, GOT_PAGE0, (char *) glyph, color);
}

static void draw_line_shadowed(int x, int y, const char *s, int color) {
	while (*s) {
		draw_char_shadowed(x, y, (unsigned char) *s, color);
		x += STORY_CHAR_ADVANCE;
		++s;
	}
}

// Draws the current episode's real story text directly onto a plain black backdrop -- wootbeer: "get rid
// of the frame, and make the background black" -- matching the chapter-title card that follows it
// (story_draw_title() below), which he confirmed already "looks good" as a plain black screen. Only
// STORY_VISIBLE_ROWS real lines are drawn per frame, starting at s_scroll_line: this function walks
// the ENTIRE real text every frame (cheap -- at most a few thousand characters), tracking the real
// running color state the "/NNN/" escapes update, but only actually calls draw_char_shadowed() for
// characters whose own line falls inside the current scroll window. Odin's face and (episode 1 only)
// the hammer icon are drawn as part of this same per-line walk, at their own kIconLayout entry's row,
// so they scroll with the text exactly like any other line.
static void story_draw_text(void) {
	long len = 0;
	const unsigned char *text = got_get_story_text(s_episode, &len);
	const unsigned char *storypic = got_get_storypic();
	const StoryIconLayout *icons = &kIconLayout[s_episode];
	int max_scroll;

	max_scroll = s_story_line_count - STORY_VISIBLE_ROWS;
	if (max_scroll < 0) {
		max_scroll = 0;
	}
	if (s_scroll_line < 0) {
		s_scroll_line = 0;
	}
	if (s_scroll_line > max_scroll) {
		s_scroll_line = max_scroll;
	}

	xfillrectangle(0, 0, STORY_CANVAS_W, STORY_VIEW_H, GOT_PAGE0, 0);

	if (s_scroll_line > 0) {
		got_xprint(STORY_ARROW_X, STORY_ARROW_UP_Y, "^", GOT_PAGE0, STORY_ARROW_COLOR);
	}
	if (s_scroll_line < max_scroll) {
		got_xprint(STORY_ARROW_X, STORY_ARROW_DOWN_Y, "v", GOT_PAGE0, STORY_ARROW_COLOR);
	}

	// Odin's face -- always present (all 3 episodes draw it), at this episode's own kIconLayout row.
	if (storypic) {
		int row = icons->odin_line - s_scroll_line;
		if (row >= 0 && row < STORY_VISIBLE_ROWS) {
			xfput(STORY_ODIN_X, STORY_TEXT_TOP + row * STORY_LINE_PITCH, GOT_PAGE0, (char *) storypic);
		}
		// The hammer icon -- episode 1 only (kIconLayout's own comment: episodes 2/3's own real
		// story() never draws it at all).
		if (icons->hammer_line >= 0) {
			row = icons->hammer_line - s_scroll_line;
			if (row >= 0 && row < STORY_VISIBLE_ROWS) {
				xfput(STORY_HAMMER_X, STORY_TEXT_TOP + row * STORY_LINE_PITCH, GOT_PAGE0,
					  (char *) (storypic + 262));
			}
		}
	}

	if (!text) {
		return;
	}

	{
		int line_no = 0;
		int x = STORY_TEXT_X0;
		int color = STORY_DEFAULT_COLOR;
		long p = 0;
		while (line_no < STORY_MAX_LINES && p < len) {
			unsigned char ch = text[p];
			if (ch == 13) { // '\r' -- real newline
				++line_no;
				x = STORY_TEXT_X0;
			} else if (ch == '/' && p + 4 < len && text[p + 4] == '/') {
				char digits[4];
				digits[0] = (char) text[p + 1];
				digits[1] = (char) text[p + 2];
				digits[2] = (char) text[p + 3];
				digits[3] = '\0';
				color = atoi(digits);
				p += 4; // the loop's own ++p below consumes the closing '/'
			} else if (ch != 10) { // '\n' -- real DOS \r\n pairing, silently skipped, same as real code
				int row = line_no - s_scroll_line;
				if (row >= 0 && row < STORY_VISIBLE_ROWS) {
					draw_char_shadowed(x, STORY_TEXT_TOP + row * STORY_LINE_PITCH, ch, color);
				}
				x += STORY_CHAR_ADVANCE;
			}
			++p;
		}
	}
}

static const char *story_subtitle(int episode) {
	switch (episode) {
		case 1:
			return "Part I: Serpent Surprise";
		case 2:
			return "Part II: Non-Stick Nognir";
		case 3:
			return "Part III: Lookin' for Loki";
		default:
			return "";
	}
}

// Real: plain black (no backdrop image at all -- story() only ever fade_out()s before drawing
// these two lines, never fills or re-blits anything else first) with "God Of Thunder"/"Part N:
// ..." straight on top, drawn under the normal game palette (already restored by this point -- see
// got_story_update()'s own screen-transition handling below) -- real color values, this port's own
// vertically-recentered Y (see STORY_TITLE_Y's own comment).
static void story_draw_title(void) {
	const char *title = "God Of Thunder";
	const char *subtitle = story_subtitle(s_episode);
	int title_x = (STORY_CANVAS_W - (int) strlen(title) * STORY_CHAR_ADVANCE) / 2;
	int subtitle_x = (STORY_CANVAS_W - (int) strlen(subtitle) * STORY_CHAR_ADVANCE) / 2;

	xfillrectangle(0, 0, STORY_CANVAS_W, STORY_VIEW_H, GOT_PAGE0, 0);
	draw_line_shadowed(title_x, STORY_TITLE_Y, title, STORY_TITLE_COLOR);
	draw_line_shadowed(subtitle_x, STORY_SUBTITLE_Y, subtitle, STORY_SUBTITLE_COLOR);
}

void got_story_draw(void) {
	if (s_screen == STORY_SCREEN_TEXT) {
		story_draw_text();
	} else {
		story_draw_title();
	}
}

void got_story_update(void) {
	bool up = key_flag[KEY_UP] != 0;
	bool down = key_flag[KEY_DOWN] != 0;
	bool fire = key_flag[KEY_FIRE] != 0;

	if (s_screen == STORY_SCREEN_TEXT) {
		// wootbeer: "if all the text doesn't fit on one screen we can use a scrolling method like we did
		// on the credits" -- the same edge-detected one-line-per-press scroll got_title_update()'s
		// own GOT_APP_CREDITS case uses (clamped in story_draw_text() itself, same "draw function
		// owns its own clamp" shape got_title_draw_credits() uses). wootbeer confirmed this part already
		// "seems to look and work well," so it's untouched by this file's other changes.
		if (up && !s_prev_up) {
			--s_scroll_line;
		}
		if (down && !s_prev_down) {
			++s_scroll_line;
		}
		if (fire && !s_prev_fire) {
			// wootbeer: "after pressing a button to exit that screen, then show the part 3 ... this
			// screen plays no music." Real story()'s own `load_palette();` call, right before it
			// returns -- restoring the normal game palette here, before the chapter-title card ever
			// draws, is what keeps that screen "looking good" (wootbeer's own words) exactly as it did
			// before STORYPAL existed.
			const unsigned char (*game_palette)[3] = got_get_game_palette();
			if (game_palette) {
				modex_set_palette(game_palette);
			}
			s_screen = STORY_SCREEN_TITLE;
			got_pause_music();
		}
	} else { // STORY_SCREEN_TITLE
		if (fire && !s_prev_fire) {
			// wootbeer: "after pressing a, this screen exits and the game begins." got_title.c's own
			// GOT_APP_STORY case checks got_story_finished() right after calling this and takes it
			// from here (got_start_new_game() + GOT_APP_PLAYING).
			s_finished = true;
		}
	}
	s_prev_up = up;
	s_prev_down = down;
	s_prev_fire = fire;
}
