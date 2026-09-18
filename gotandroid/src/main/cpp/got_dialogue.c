// SAY/TEXT/ITEMSAY dialogue box and ASK prompt -- see got_dialogue.h for the overall design (real
// display_speech() and a second real select_option() port, both driven per-frame by got_script.c).

#include "got_dialogue.h"
#include "got_font.h"
#include "modex.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Real GoT scancodes/page indices -- redefined locally rather than shared via a header, matching
// how got_menu.c already redefines its own copies of these same constants.
#define KEY_ESC 1
#define KEY_FIRE 56
#define GOT_PAGE0 3840u
#define GOT_PAGE2 34720u
#define SOUND_WOOP 9 // matches got_main.c's own SOUND_WOOP -- see that enum's own comment

extern volatile char key_flag[100];
extern const unsigned char *got_get_bpics(void);
extern const unsigned char *got_get_objects(void); // got_main.c -- see that function's own comment
													// (added for the Jormangund-round dialogue-icon fix)
// Real animated HAMPIC hammer-cursor sheet -- see got_menu.c's own identical extern for what this is
// and why it's used here too. wootbeer: "all menu cursors should be the spinning hammer like the main
// menu uses... this should be consistent everywhere."
extern const unsigned char *got_get_hampic(void);
extern void got_play_sound(int sound_index); // got_main.c -- no longer static, see its own site
extern void got_draw_all_sprites(void); // got_main.c -- no longer static, see its own site and
										 // restore_page()'s own comment just below

// Real d_restore() (1_back.c) -- called by real cmd_say()/cmd_ask() right after
// display_speech()/select_option() returns, to wipe the dialogue/ask box back out of the live page
// and leave the room's own clean background in its place. Ported the same way got_menu_close()
// already restores GOT_PAGE0 from GOT_PAGE2 (the room's own continuously-maintained clean
// background copy, see that function's own comment) -- this port draws everything into one single
// live page rather than real's separate draw/display page pair, so real d_restore()'s own
// multi-page juggling collapses to this one copy. Missing this was the box-stays-onscreen-until-
// Thor-moves bug wootbeer found: got_dialogue_open()/got_dialogue_update() draw the box straight into
// GOT_PAGE0 same as everything else in this file, but nothing was ever putting the background back
// underneath it once the box closed -- gameplay resuming just keeps whatever was already in
// GOT_PAGE0 until something else (Thor walking over that same screen area) happens to redraw it.
//
// Also now redraws every sprite immediately after the background, matching real d_restore()'s own
// `xdisplay_actors(...);` calls (both the draw_page AND display_page ones -- this port's single
// live page collapses that pair into one got_draw_all_sprites() call, same collapse the background
// restore above already makes). Missing this was a second, separate bug from the one above: the
// background-only restore wiped every sprite (Thor, the hammer, every enemy/NPC) off the live page
// the instant a box closed -- invisible for an ordinary one-line SAY/ASK box, since
// got_advance_game() redraws everyone again on the very next frame regardless, but wootbeer caught it
// on the apple vendor's own real script (label |10099): picking "Fill 'er Up!" runs a real
// 21-iteration EATEM loop (`sound @gulp:pause 20:addjewels -7:addhealth 5`) with NO dialogue box
// open at all between iterations, and got_script_is_running() staying true for that whole several-
// second stretch keeps got_advance_game() (this port's only OTHER sprite-redraw path) from ever
// running until the script finally finishes -- so the gap between "ASK box closes" and "script
// ends" is exactly how long every sprite, vendor included, stayed missing. wootbeer: "while the filling
// of apples action is taking place, the npc disappears from the screen until it is done." Real code
// never has this gap at all -- d_restore() redraws every actor unconditionally, every single call,
// specifically because REAL execute_script() has no other sprite-redraw path either (it blocks the
// whole real game loop for its own duration, so d_restore() is the ONLY place real actors ever get
// redrawn while a script has control) -- this port's own script engine is non-blocking instead (see
// got_script.h's own comment), which is what let this gap open up in the first place: without this
// fix, this port had neither real code's always-redraw-on-restore behavior NOR its own per-frame
// got_advance_game() path while a script runs.
static void restore_page(void) {
	xcopyd2d(0, 0, 320, 192, 0, 0, GOT_PAGE2, GOT_PAGE0, 320, 320);
	got_draw_all_sprites();
}

// Real dialog_color[]={14,54,120,138,15,0,0,...} (1_main.c) -- indexed by the hex digit in a real
// "~N" escape (0-9, A-F -- only 0-4 are ever actually nonzero/used by any real script, the rest
// are real unused/reserved slots, ported verbatim anyway since they cost nothing).
static const int s_dialog_color[16] = {14, 54, 120, 138, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// Real display_speech() delays ~5 real timer_cnt ticks between characters when no advance button
// is held, playing one WOOP per character for that real typewriter cadence. timer_cnt is NOT
// driven by DOS's stock ~18.2Hz INT 8 timer, despite that being the usual assumption for a "timer
// tick" in a DOS game -- 1_sbfx.c's own sbfx_init() reprograms PIT channel 0 itself before
// installing its own INT 8 handler: `speed=(unsigned)(1192030L/120L)` (=9933) followed by
// `outportb(0x43,0x36); outportb(0x40,speed); outportb(0x40,speed>>8);`, i.e. the real game's
// timer_cnt ticks at ~120Hz (~8.33ms/tick), confirmed directly from that source rather than
// assumed. So 5 ticks is ~41.7ms, not ~275ms -- the 275ms figure baked into this constant
// previously came from the wrong 18.2Hz assumption and was never checked against 1_sbfx.c until
// wootbeer reported the fixed cadence as too slow. This port has no equivalent tick counter (see
// got_main.c's own real-time-tick-accumulator work for why), so character reveal is paced against
// wall-clock time instead, the same got_step_palette_cycle()/got_advance_game() pattern (see
// got_dialogue_update()'s own use of clock_gettime() below) -- NOT a frame count. A frame count
// was tried first and was the direct cause of an earlier, different bug: at
// REVEAL_DELAY_FRAMES=4, the interval between characters was only ~33ms on the Retroid Pocket 6's
// 120Hz screen (already known to bite this project once before, see the notes on movement speed),
// fast enough that SOUND_WOOP was retriggering faster than talkBeep.ogg's own playback length.
// Several overlapping instances of the same clip, staggered by a few milliseconds each, phase/
// comb-filter together into one sustained, differently-pitched tone instead of distinct blips --
// exactly wootbeer's first report ("sounds the same the entire length of his speech... until he
// finishes the sentence, then the sound plays normally": no more overlap once retriggering stops,
// so the last instance finally plays cleanly). Fixed on two sides: this real-time pacing (so the
// interval is correct and device-refresh-rate-independent to begin with) and GotView.java's own
// playSound() now stopping any still-playing instance of the same sound before starting a new one
// (matching real hardware's own single-channel playback constraint, see that method's comment).
#define REVEAL_STEP_MS (5000.0 / 120.0)

// ---------------------------------------------------------------------------------------------
// SAY / TEXT / ITEMSAY box (real display_speech())
// ---------------------------------------------------------------------------------------------

typedef enum {
	DLG_CLOSED = 0,
	DLG_REVEALING, // typewriter-revealing characters on the current page
	DLG_PAGE_WAIT, // "More..." shown, waiting for input to continue to the next page
	DLG_END_WAIT,  // text fully shown, waiting for input to close (real final wait_not_response())
} DialogueState;

static DialogueState s_state = DLG_CLOSED;
static const char *s_p = NULL;   // real `p` -- current read position in the script's tmp_buff
static const unsigned char *s_pic = NULL;
static bool s_sound_on_reveal = false;
// Real display_speech()'s own `item` parameter -- 0 = no icon (the common case: ordinary SAY/TEXT
// boxes, and ITEMSAY calls with a 0 item argument). See got_dialogue_open()'s own header comment
// (got_dialogue.h) for the real 0-based-OBJECTS-index convention this value already arrives in.
static int s_icon_obj = 0;
static bool s_cancelled = false;
static bool s_prev_fire = false;
static bool s_prev_esc = false;

// Real locals `x`,`lc`,`color`,`pn`,`pc` -- persist across got_dialogue_update() calls since a
// single call now only advances one character (or waits), not the whole real blocking loop.
static int s_x, s_lc, s_color, s_pn, s_pc;
// Real-time reveal pacing (see REVEAL_STEP_MS's own comment) -- same clock_gettime()-based
// accumulator shape as got_main.c's own got_step_palette_cycle().
static bool s_reveal_time_inited;
static struct timespec s_reveal_last_time;
static double s_reveal_phase_ms;

static void draw_box_frame(void);
static void draw_portrait(void);
static void draw_item_icon(void);
static bool reveal_one_char(void);

// Keeps calling reveal_one_char() for one "pacing step" -- color escapes and newlines process for
// free (reveal_one_char() returns false for those, see its own comment) and shouldn't stall the
// typewriter waiting on REVEAL_STEP_MS for something the player never sees appear, so this loops
// through them and stops at the first real glyph drawn (or the box ending, or a page break opening
// the "More..." prompt -- either one leaves DLG_REVEALING, which also stops the loop). Bounded
// defensively against a pathological run of escapes with no visible text between them.
static void reveal_until_step(void) {
	int guard = 64;
	while (s_state == DLG_REVEALING && guard-- > 0) {
		if (reveal_one_char()) {
			return;
		}
	}
}

bool got_dialogue_is_open(void) {
	return s_state != DLG_CLOSED;
}

bool got_dialogue_was_cancelled(void) {
	return s_cancelled;
}

void got_dialogue_open(const char *text, const unsigned char *pic, bool sound_on_reveal,
						int icon_obj) {
	if (s_state != DLG_CLOSED || !text) {
		return; // no reentrancy, matches got_script.c's own single-script-at-a-time policy
	}
	s_p = text;
	s_pic = pic;
	s_sound_on_reveal = sound_on_reveal;
	s_icon_obj = icon_obj;
	s_cancelled = false;
	s_x = 40;
	s_lc = 0;
	s_color = 14;
	s_pn = 0;
	s_pc = 0;
	s_reveal_time_inited = false; // don't let a previous box's clock reading carry over
	s_reveal_phase_ms = 0.0;
	s_prev_fire = key_flag[KEY_FIRE] != 0; // don't let an already-held confirm button from
	s_prev_esc = key_flag[KEY_ESC] != 0;   // whatever opened this box immediately close it too
	s_state = DLG_REVEALING;

	draw_box_frame();
	draw_portrait(); // real: `xput(152,65,pg,(pic+(pn*262)));` right after the border, frame 0
	draw_item_icon(); // real: `if(item) xfput(176,65,pg,objects[item]);` -- drawn once, same timing
}

// Real display_speech()'s own `if(item) xfput(176,65,pg,(char far*)objects[item]);` -- drawn once,
// right alongside the portrait's own initial draw above, not redrawn per character/page (the real
// box's own border and this icon are both one-time draws baked into the live page, same as
// draw_box_frame(); only the portrait's own 4-frame talking animation and the revealed text repaint
// as the box progresses). 24px right of the portrait (152,65) -- Jormangund boss-fight overhaul
// (wootbeer: "there should be an icon for the armor and hammer on that popup next to odin's face icon").
static void draw_item_icon(void) {
	const unsigned char *objects;

	if (!s_icon_obj) {
		return;
	}
	objects = got_get_objects();
	if (!objects) {
		return;
	}
	xfput(176, 65, GOT_PAGE0, (char *) (objects + (long) s_icon_obj * 262));
}

static void draw_box_frame(void) {
	const unsigned char *bpics = got_get_bpics();
	int l;

	// Real display_speech()'s own fixed box geometry -- NOT computed from content like
	// select_option()'s box (see got_menu.c), the real dialogue box is always this exact size.
	xfillrectangle(48, 64, 273, 145, GOT_PAGE0, 215);
	if (!bpics) {
		return;
	}
	xfput(32, 48, GOT_PAGE0, (char *) (bpics + 192L * 262));
	xfput(272, 48, GOT_PAGE0, (char *) (bpics + 193L * 262));
	xfput(32, 144, GOT_PAGE0, (char *) (bpics + 194L * 262));
	xfput(272, 144, GOT_PAGE0, (char *) (bpics + 195L * 262));
	for (l = 0; l < 14; ++l) {
		xfput(48 + l * 16, 48, GOT_PAGE0, (char *) (bpics + 196L * 262));
		xfput(48 + l * 16, 144, GOT_PAGE0, (char *) (bpics + 197L * 262));
	}
	for (l = 0; l < 5; ++l) {
		xfput(32, 64 + l * 16, GOT_PAGE0, (char *) (bpics + 198L * 262));
		xfput(272, 64 + l * 16, GOT_PAGE0, (char *) (bpics + 199L * 262));
	}
}

// Real xtext1()+xtext() two-pass draw -- see got_font.h's own comment: xtext1(x,y,...,color)
// turns out (verified against the real g_asm.asm source) to be exactly xtext() drawn one scanline
// lower, so this reproduces the real drop-shadow effect with the xtext() this port already has.
static void draw_char_shadowed(int x, int y, int ch, int color) {
	const unsigned char *glyph = got_font_glyph((unsigned char) ch);
	if (!glyph) {
		return;
	}
	xtext(x, y + 1, GOT_PAGE0, (char *) glyph, 0); // real xtext1(...,0) -- shadow
	xtext(x, y, GOT_PAGE0, (char *) glyph, color); // real xtext(...,color) -- the actual glyph
}

static void draw_portrait(void) {
	if (s_pic) {
		xfput(152, 65, GOT_PAGE0, (char *) (s_pic + (long) s_pn * 262));
	}
}

// Direct port of real display_speech()'s per-character body (the part of its while(1) loop past
// the ESC/end/escape-code/newline checks) -- called once per revealed character. Returns true if
// this call actually drew a glyph (or ended the box) and so should consume one real REVEAL_STEP_MS
// pacing step; false for a color-escape or newline, which real code (and this port) processes
// "for free", with no delay attributed to it -- see got_dialogue_update()'s own driving loop.
static bool reveal_one_char(void) {
	unsigned char ch = (unsigned char) *s_p;

	if (ch == 0) {
		draw_portrait(); // real: xput(152,65,pg,(pic+(1*262))); -- pn forced to frame 1 on end
		s_pn = 1;
		draw_portrait();
		s_state = DLG_END_WAIT;
		return true;
	}
	if (ch == '~' && ((s_p[1] >= '0' && s_p[1] <= '9') || (s_p[1] >= 'A' && s_p[1] <= 'F')
					   || (s_p[1] >= 'a' && s_p[1] <= 'f'))) {
		char h = s_p[1];
		int idx = (h >= '0' && h <= '9') ? (h - '0') : ((h | 0x20) - 'a' + 10);
		s_color = s_dialog_color[idx];
		s_p += 2;
		return false; // real: `continue;` -- consumes no reveal-delay tick, keep going this same frame
	}
	if (ch == '\n') {
		s_x = 40;
		++s_lc;
		++s_p;
		if (s_lc > 4) {
			s_pn = 1;
			draw_portrait();
			// Real xprint(216,134,"More...",pg,15) -- the shipped game's own real xprint()
			// (1_grp.c/2_grp.c), which got_xprint() now correctly matches (see that function's own
			// got_font.c comment on a previous mix-up with a different, never-shipped level-editor
			// xprint() this port had been built against instead). A plain literal with no "~N"
			// escapes either way, so this call site itself was never affected by that bug.
			got_xprint(216, 134, "More...", GOT_PAGE0, 15);
			s_state = DLG_PAGE_WAIT;
		}
		return false;
	}
	draw_char_shadowed(s_x + 12, 83 + s_lc * 10, ch, s_color);
	// Real code (1_back.c) gates this same WOOP-per-character sound on `!key_flag[key_fire] &&
	// !key_flag[ENTER] && !key_flag[SPACE] && !key_flag[key_magic]` (skip the sound entirely while
	// any advance key is held). A `!key_flag[KEY_FIRE]` gate was tried here on that basis, on the
	// theory that wootbeer was holding the advance button through the box's first screen only -- it
	// made things WORSE (blips on every screen, not just the first), meaning key_flag[KEY_FIRE]
	// reads "held" far more often in this port's actual per-frame input handling than a real
	// physical hold would (most likely: whatever taps advance "More..."/end a page leave the flag
	// set for several render frames afterward, so this gate was suppressing sound at the start of
	// EVERY screen, not just during a genuine fast-forward hold) -- reverted. Real behavior is
	// still worth having eventually, but not by copying the real gate condition verbatim without
	// first confirming this port's key_flag[] timing actually matches a real physical hold/release;
	// see the notes-file entry for this revert for where the real "screen 1 only" cause still needs
	// to be found.
	if (s_sound_on_reveal) {
		got_play_sound(SOUND_WOOP);
	}
	++s_pc;
	if (s_pc > 1) {
		s_pc = 0;
		s_pn = (s_pn + 1) > 3 ? 0 : s_pn + 1;
		draw_portrait();
	}
	++s_p;
	s_x += 8;
	return true;
}

static void close_dialogue(bool cancelled) {
	s_state = DLG_CLOSED;
	s_cancelled = cancelled;
	restore_page(); // real d_restore() -- see that function's own comment
	// wootbeer: "it's too hard to accidentally kill NPCs after talking to them, I often push the button
	// to skip dialogue and accidentally shoot/kill them." Root cause: this box's own FIRE-to-close
	// (DLG_END_WAIT's `if(fire && !s_prev_fire)` above) is edge-triggered on the PRESS, but the
	// physical button stays HELD for several render frames afterward -- and got_move_thor()'s own
	// hammer-throw check (`if(key_flag[KEY_FIRE]) got_thor_shoots();`) is deliberately NOT edge-
	// triggered (matches real movement_zero(), see that check's own comment: real code only avoids
	// spam-throwing via thor.shot_cnt's cooldown, not a button edge). So the instant this box closes
	// and got_advance_game() resumes next frame, FIRE reads "held" there too and Thor throws his
	// hammer for free -- almost always straight at the NPC he's still standing face-to-face with,
	// since talking to someone doesn't move Thor away from them first. Same bleed-through hazard
	// got_menu_close() (got_menu.c) and got_item_menu_close() (got_item.c) already guard against for
	// their own FIRE-confirms; this is the identical fix, in the one file that didn't have it yet.
	key_flag[KEY_FIRE] = 0;
}

void got_dialogue_update(void) {
	bool fire, esc;

	if (s_state == DLG_CLOSED) {
		return;
	}
	fire = key_flag[KEY_FIRE] != 0;
	esc = key_flag[KEY_ESC] != 0;

	if (esc && !s_prev_esc) {
		key_flag[KEY_ESC] = 0;
		close_dialogue(true); // real: ESC -> wait_not_response(); return 0;
		s_prev_fire = fire;
		s_prev_esc = esc;
		return;
	}

	switch (s_state) {
		case DLG_REVEALING:
			// Real: holding an advance key skips the per-character delay entirely (fast-forward);
			// otherwise wait REVEAL_STEP_MS of real elapsed time before the next character (see
			// that constant's own comment for why this is wall-clock time, not a frame count).
			if (fire) {
				reveal_until_step();
				s_reveal_time_inited = false; // resync the clock for whenever real pacing resumes
			} else {
				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);
				if (!s_reveal_time_inited) {
					s_reveal_last_time = now;
					s_reveal_time_inited = true;
				} else {
					double elapsed_ms = (double) (now.tv_sec - s_reveal_last_time.tv_sec) * 1000.0
							+ (double) (now.tv_nsec - s_reveal_last_time.tv_nsec) / 1.0e6;
					s_reveal_last_time = now;
					if (elapsed_ms > 0.0) {
						s_reveal_phase_ms += elapsed_ms;
					}
					// Guards a pathological huge elapsed_ms the same way got_step_palette_cycle()/
					// got_advance_game() (got_main.c) already guard their own real-time
					// accumulators -- but by DROPPING the backlog rather than catching it up: those
					// two run their catch-up work silently/invisibly (palette writes, movement
					// steps), so a bounded while-loop burst of several steps in one call is fine;
					// here, catching up would mean revealing several characters (each with its own
					// WOOP) either in one rapid burst or, worse, one per render frame until drained
					// -- either way, several SOUND_WOOP plays only ~8ms apart on this 120Hz device,
					// too close together for GotView.java's own stop-before-play fix to fully
					// silence the previous instance before the next starts, so they'd still
					// overlap and phase into a pitch-shifted "blip" instead of a clean blip each.
					// Diagnosed from wootbeer's report that screen 1 of Odin's dialogue (and only
					// screen 1) had periodic pitch/tone-changing blips with no input involved --
					// consistent with a one-time stall (first-ever draw of this box, first-ever
					// got_play_sound() JNI round trip this session, etc.) backlogging the phase
					// once at the very start, then draining it character-by-character across
					// however many subsequent frames it took to catch up, each one firing WOOP
					// too close to the last. Clamping here means a huge stall costs at most one
					// slightly-early character, never a burst.
					if (s_reveal_phase_ms > REVEAL_STEP_MS) {
						s_reveal_phase_ms = REVEAL_STEP_MS;
					}
					if (s_reveal_phase_ms >= REVEAL_STEP_MS) {
						s_reveal_phase_ms -= REVEAL_STEP_MS;
						reveal_until_step();
					}
				}
			}
			break;
		case DLG_PAGE_WAIT:
		case DLG_END_WAIT:
			if (fire && !s_prev_fire) {
				if (s_state == DLG_END_WAIT) {
					close_dialogue(false);
				} else {
					// Real: xfillrectangle(48,84,273,145,pg,215); lc=0; -- clears just the text
					// area below the portrait row, keeping the box frame and portrait in place.
					xfillrectangle(48, 84, 273, 145, GOT_PAGE0, 215);
					s_lc = 0;
					s_pn = 0;
					s_pc = 0;
					s_reveal_time_inited = false; // don't count time spent reading "More..." as a
					s_reveal_phase_ms = 0.0;      // head start on the next page's own first char
					s_state = DLG_REVEALING;
				}
			}
			break;
		default:
			break;
	}
	s_prev_fire = fire;
	s_prev_esc = esc;
}

void got_dialogue_draw(void) {
	if (s_state == DLG_CLOSED) {
		return;
	}
	// The box frame + portrait + revealed-so-far text are already baked into GOT_PAGE0 by
	// got_dialogue_update()'s incremental draws (matching real display_speech() drawing straight
	// into the live display page as it goes) -- draw_box_frame() only needs to run once, right
	// when the box opens, not every frame. See got_dialogue_open()'s own call below.
}

// ---------------------------------------------------------------------------------------------
// ASK prompt (real select_option(), reused by real cmd_ask())
// ---------------------------------------------------------------------------------------------

#define ASK_MAX_ITEMS 10
#define ASK_TITLE_COLOR 54
#define ASK_ITEM_COLOR 14
#define ASK_BOX_COLOR 215

static bool s_ask_open = false;
static char s_ask_title[64];
static const char *s_ask_items[ASK_MAX_ITEMS];
static int s_ask_count = 0;
static int s_ask_selected = 0;
static int s_ask_result = -1;
static bool s_ask_prev_up = false, s_ask_prev_down = false, s_ask_prev_fire = false;

// Mirror Mode's own exclude-rect support -- see got_ask_draw()'s own comment on where these are
// set, and got_ask_get_box_rect() (below got_dialogue_any_open()) for how they're read back.
// Stale/meaningless while !s_ask_open -- the getter guards that, never returning them otherwise.
static int s_ask_box_x1, s_ask_box_y1, s_ask_box_x2, s_ask_box_y2;

// Drives the hammer cursor's 4 real HAMPIC animation frames (got_ask_draw() below) -- this file's
// own local counter, same reasoning got_menu.c's own s_cursor_frame_counter comment gives; ticks
// once per got_ask_draw() call, same /7-per-frame speed got_title_draw_main_menu() established.
static int s_cursor_frame_counter = 0;

void got_ask_open(const char *title, const char *const *options, int count) {
	int i;
	if (s_ask_open || count <= 0) {
		return;
	}
	snprintf(s_ask_title, sizeof(s_ask_title), "%s", title ? title : "");
	if (count > ASK_MAX_ITEMS) {
		count = ASK_MAX_ITEMS;
	}
	for (i = 0; i < count; ++i) {
		s_ask_items[i] = options[i];
	}
	s_ask_count = count;
	s_ask_selected = 0;
	s_ask_result = -1;
	s_ask_prev_up = key_flag[72] != 0;
	s_ask_prev_down = key_flag[80] != 0;
	s_ask_prev_fire = key_flag[KEY_FIRE] != 0;
	s_ask_open = true;
}

bool got_ask_is_open(void) {
	return s_ask_open;
}

int got_ask_result(void) {
	return s_ask_result;
}

void got_ask_update(void) {
	bool up, down, fire;
	if (!s_ask_open) {
		return;
	}
	up = key_flag[72] != 0;
	down = key_flag[80] != 0;
	fire = key_flag[KEY_FIRE] != 0;

	if (up && !s_ask_prev_up) {
		s_ask_selected = (s_ask_selected - 1 + s_ask_count) % s_ask_count;
	}
	if (down && !s_ask_prev_down) {
		s_ask_selected = (s_ask_selected + 1) % s_ask_count;
	}
	if (fire && !s_ask_prev_fire) {
		// real select_option() (1_panel.c) returns a 1-based result:
		// "ret=pos+1;" where pos is the 0-based cursor position. Every
		// real SPEAK1 script branches on ASK results 1-based (e.g.
		// "if a=1 then goto YES" means the FIRST listed option). Match
		// that here -- s_ask_selected itself stays 0-based since it's
		// also used as a UI cursor/array index (see got_ask_draw()'s
		// highlight loop).
		s_ask_result = s_ask_selected + 1;
		s_ask_open = false;
		restore_page(); // real d_restore() -- see that function's own comment
		key_flag[KEY_FIRE] = 0; // same FIRE-bleeds-into-the-next-hammer-throw guard as
								 // close_dialogue() above -- see that function's own comment
	}
	s_ask_prev_up = up;
	s_ask_prev_down = down;
	s_ask_prev_fire = fire;
}

// Same real select_option() box-sizing/border/text-layout math as got_menu.c's own got_menu_draw()
// -- see that function's comment. Duplicated rather than shared: this box's item source (a
// script's own runtime string array) and got_menu's (two fixed static arrays) are different enough
// shapes that sharing would need its own small abstraction, and this project's established
// practice (got_font.c/got_menu.c as separate small focused files) favors a second, clearly-
// commented copy of a *seven-line* real math block over that indirection.
void got_ask_draw(void) {
	const unsigned char *bpics;
	int w, h, x1, y1, x2, y2, s, i, tw;

	if (!s_ask_open) {
		return;
	}
	++s_cursor_frame_counter;
	w = got_text_width(s_ask_title) / 8;
	for (i = 0; i < s_ask_count; ++i) {
		int iw = got_text_width(s_ask_items[i]) / 8;
		if (iw > w) {
			w = iw;
		}
	}
	if (w & 1) {
		++w;
	}
	w = w * 8 + 32;
	s = w / 16;
	h = s_ask_count * 16 + 32;
	x1 = (320 - w) / 2;
	x2 = (x1 + w) - 1;
	y1 = (192 - h) / 2;
	y2 = (y1 + h) - 1;
	if (x1 & 1) {
		++x1;
	}
	if (x2 & 1) {
		++x2;
	}

	// Mirror Mode -- see got_menu.c's own identical comment on got_menu_draw()'s matching capture
	// for the full rationale; same outer-extent math (the 16px border tiles drawn just below push
	// the real visible edge out this far).
	s_ask_box_x1 = x1 - 16;
	s_ask_box_y1 = y1 - 16;
	s_ask_box_x2 = x2 + 16;
	s_ask_box_y2 = y2 + 16;

	xfillrectangle(x1, y1, x2, y2, GOT_PAGE0, ASK_BOX_COLOR);

	bpics = got_get_bpics();
	if (bpics) {
		xfput(x1 - 16, y1 - 16, GOT_PAGE0, (char *) (bpics + 192L * 262));
		xfput(x2, y1 - 16, GOT_PAGE0, (char *) (bpics + 193L * 262));
		xfput(x1 - 16, y2, GOT_PAGE0, (char *) (bpics + 194L * 262));
		xfput(x2, y2, GOT_PAGE0, (char *) (bpics + 195L * 262));
		for (i = 0; i < s; ++i) {
			xfput(x1 + i * 16, y1 - 16, GOT_PAGE0, (char *) (bpics + 196L * 262));
			xfput(x1 + i * 16, y2, GOT_PAGE0, (char *) (bpics + 197L * 262));
		}
		for (i = 0; i < s_ask_count + 2; ++i) {
			xfput(x1 - 16, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 198L * 262));
			xfput(x2, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 199L * 262));
		}
	}

	tw = got_text_width(s_ask_title);
	got_xprint((320 - tw) / 2, y1 + 4, s_ask_title, GOT_PAGE0, ASK_TITLE_COLOR);
	// Cursor: the real animated HAMPIC hammer, not a plain ">" glyph -- see got_menu.c's own
	// identical comment for why (wootbeer: "all menu cursors should be the spinning hammer... this
	// should be consistent everywhere").
	for (i = 0; i < s_ask_count; ++i) {
		int iy = (y1 + 28) + i * 16;
		got_xprint(x1 + 32, iy, s_ask_items[i], GOT_PAGE0, ASK_ITEM_COLOR);
		if (i == s_ask_selected) {
			const unsigned char *hampic = got_get_hampic();
			if (hampic) {
				int frame = (s_cursor_frame_counter / 7) % 4;
				xfput(x1 + 8, iy - 3, GOT_PAGE0, (char *) (hampic + frame * 262));
			} else {
				got_xprint(x1 + 8, iy, ">", GOT_PAGE0, ASK_ITEM_COLOR);
			}
		}
	}
}

bool got_dialogue_any_open(void) {
	return s_state != DLG_CLOSED || s_ask_open;
}

// Mirror Mode's own exclude-rect support -- see modex.h's own modex_set_mirror_exclude_rect()
// comment for the full design, and got_menu.h's own got_menu_get_box_rect() for the sibling
// pattern this matches. Unlike the pause menu/item picker/ASK box (all sized from their own
// content, recomputed every frame), the dialogue box is real display_speech()'s own FIXED box
// geometry -- draw_box_frame()'s own comment -- so this is just those same literal constants
// (48-16=32, 64-16=48, 273+16=289 rounded down to the actual last drawn corner-tile edge at
// 272+16=288, 145+16=161 likewise 144+16=160 -- see draw_box_frame()'s own corner/edge xfput calls
// for the exact 16px tiles that push the real visible edge out this far), not a per-frame capture.
// Returns false (leaving the outputs untouched) whenever no dialogue box is actually open.
bool got_dialogue_get_box_rect(int *x1, int *y1, int *x2, int *y2) {
	if (s_state == DLG_CLOSED) {
		return false;
	}
	if (x1) *x1 = 32;
	if (y1) *y1 = 48;
	if (x2) *x2 = 288;
	if (y2) *y2 = 160;
	return true;
}

// Mirror Mode's own exclude-rect support for the ASK prompt -- same pattern as
// got_menu_get_box_rect()/got_item_menu_get_box_rect(), just reading back s_ask_box_x1/etc (set in
// got_ask_draw() below, right alongside that function's own identical box-sizing math). Returns
// false whenever no ASK prompt is actually open.
bool got_ask_get_box_rect(int *x1, int *y1, int *x2, int *y2) {
	if (!s_ask_open) {
		return false;
	}
	if (x1) *x1 = s_ask_box_x1;
	if (y1) *y1 = s_ask_box_y1;
	if (x2) *x2 = s_ask_box_x2;
	if (y2) *y2 = s_ask_box_y2;
	return true;
}
