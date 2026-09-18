// Title screen / player-selection screen -- see got_title.h for the full design rationale and the
// important scope note on exactly what is and isn't a port here: the STATE MACHINE/input handling
// is still an original design (no real GOT.EXE source exists to port it from), but the main menu's
// own VISUALS (got_title_draw_main_menu() below) are generated from a real reference screenshot of
// the real GOT.EXE main menu, and its cursor is the real HAMPIC resource -- see that function's own
// comment. Built on the exact same got_font.h/modex.h primitives, and (for the player-select screen
// specifically) reusing got_menu.c's own box-border style (real bg_pics corner/edge decorative
// tiles, indices 192-199 -- see that file's own comment) for visual consistency with the pause
// menu, even though there's no real select_option() call that screen is porting either.

#include "got_title.h"
#include "got_font.h"
#include "got_story.h"
#include "got_title_bg.h"
#include "modex.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Same real scancodes got_menu.c redefines locally (1_define.h) -- see that file's own comment on
// why these are kept as hand-audited local copies instead of a shared header.
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_FIRE 56

// Added for the three-way High Scores split (wootbeer: "we need to separate the high scores into 'High
// Scores - Part I' 'High Scores - Part II' and 'High Scores - Part III'... there will be 3 separate
// screens that are switched between when viewing from the main menu") -- same real scancode VALUES
// got_main.c already #defines these as (line ~217-218 there) but this file has never had its own
// local copy before now since nothing here used them previously (confirmed unused anywhere in this
// file prior to this feature), matching this file's own established pattern of hand-audited local
// scancode copies rather than pulling in got_main.c's header for just these two.
#define KEY_LEFT 75
#define KEY_RIGHT 77

// New this round -- wootbeer: "I also would like to add a button to delete player saves from the
// 'Select Player' menu, make it the Retroid Pocket 6's 'Select' button, but also make sure to add
// a pop-up Y/N confirmation, can be the Retroid Pocket 6's 'A' button for confirm and 'B' button for
// cancel." No real GoT scancode to match here at all (real GoT has no delete-a-save feature, and
// this whole screen's own input handling is already original design -- see got_title.h's own scope
// note), so these three are this port's own invention: picked from real DOS scancode VALUES purely
// as a memorable mnemonic (83=Del, 28=Enter, 14=Backspace), not because the real game binds any of
// them this way. All three are free slots in the shared key_flag[100] array (GotView.java's own
// gotScancode()/gotExtraScancode() are the only senders -- see that file's own comment on why
// KEY_CONFIRM/KEY_CANCEL need to be brand new scancodes rather than reusing KEY_FIRE, which the
// physical A and B buttons already both alias to for ordinary gameplay).
//
// KEY_DELETE_SAVE opens GOT_APP_DELETE_CONFIRM below, straight from the Retroid Pocket 6's Select
// button (KeyEvent.KEYCODE_BUTTON_SELECT) -- a different physical button, and a different Android
// keycode, from the existing KEY_SELECT (got_item.c's in-game item picker, bound to face button Y),
// so the two don't collide despite the similar name.
//
// KEY_CONFIRM is only read by GOT_APP_DELETE_CONFIRM below, as a direct A=Yes shortcut alongside that
// screen's own ordinary Up/Down+Fire navigation (matching the rest of this screen's, and the pause
// menu's, existing Yes/No confirm style). KEY_CANCEL is read there too (B=No), and by two more
// screens as of wootbeer's own follow-up requests -- GOT_APP_EPISODE_SELECT (B backs straight out to
// GOT_APP_PLAYER_SELECT, same destination as navigating down to that screen's own "Back" row and
// firing on it) and GOT_APP_PLAYER_SELECT itself (B backs straight out to GOT_APP_TITLE, the main
// menu -- this screen never had ANY way back to the main menu before, not even an indirect one, so
// this is new territory rather than restoring a "Back" row's own shortcut the way Episode Select's
// was). wootbeer: "the 'b' button for menus... no longer backs out... it's acting like an 'a' press" (for
// Episode Select), then "select player needs 'b' to back out as well" -- B had only ever aliased to
// KEY_FIRE on both of these screens before this, so pressing it just confirmed/continued whatever was
// highlighted instead of backing out the way a console player expects a B/Back button to. GotView.java
// sends both of these scancodes in ADDITION to (never instead of) the existing shared KEY_FIRE both
// buttons already send, so gameplay Fire is completely unaffected outside these three screens.
#define KEY_DELETE_SAVE 83
#define KEY_CONFIRM 28
#define KEY_CANCEL 14

// Matches got_main.c's/got_menu.c's own GOT_PAGE0 exactly.
#define GOT_PAGE0 3840u

// Real select_option() color indices (1_panel.c), same ones got_menu.c already uses -- kept
// numerically identical so this screen and the pause menu read as the same visual system rather
// than two different UIs bolted together.
#define BOX_COLOR 215
#define TITLE_COLOR 54
#define ITEM_COLOR 14

// wootbeer: "can we change it from 3 player slots to 5 slots." Just this one number -- s_slot_names[]/
// s_slot_used[]/s_slot_episode[] below are all sized off NUM_SLOTS already, got_title_refresh_slots()/
// the Up/Down wraparound/got_title_draw_player_select() all already loop off NUM_SLOTS rather than a
// hardcoded 3, and got_save_slot_path() (got_main.c) builds each slot's filename from the slot number
// alone ("SAVE%d.DAT") rather than any fixed-size table -- so growing this by itself is enough on this
// file's side. got_main.c's own NUM_SAVE_SLOTS has to move in lockstep too (see that define's own
// comment) since that's the bounds check every save-file read/write/delete actually runs against.
#define NUM_SLOTS 5

// Real options_yesno[] (1_panel.c), same verbatim two-string list got_menu.c's own
// MENU_SAVE_CONFIRM/MENU_LOAD_CONFIRM already use for their own Yes/No boxes -- this screen's own
// local copy since got_menu.c's own s_yesno_items is static to that file, not shared.
#define YESNO_NUM_ITEMS 2
static const char *s_yesno_items[YESNO_NUM_ITEMS] = {"Yes", "No"};

// The new main menu's own 6 rows -- real order/labels straight from wootbeer's reference screenshot of
// the real GOT.EXE main menu. All 6 are live now; MAIN_BBS_INFO was the last one still
// drawn-but-silently-ignored on FIRE (see got_title_update()'s own GOT_APP_TITLE case) -- wootbeer
// originally: "we don't have to add the other options right now if we don't have the functions for
// them", so every non-MAIN_PLAY/MAIN_QUIT row started out that way (real labels, in the real menu's
// real order, FIRE silently ignored), same as any of this port's other not-yet-implemented systems
// being present but inert (e.g. got_script.c's own EXEC/VISIBLE commands, parsed but not executed --
// see that file's own comment). Each stopped being one of those the moment wootbeer asked for it
// specifically: MAIN_BBS_INFO ("let's add in the missing 'BBS' screen" -- see GOT_APP_BBS_INFO's own
// comment in got_title.h), MAIN_HIGH_SCORES ("can we wire up the high score system now?", eventually
// a real live table -- see GOT_APP_HIGH_SCORES's own comment), MAIN_CREDITS (no request quoted
// needed -- once the other three inert rows were gone, wootbeer asked for this one directly -- see
// GOT_APP_CREDITS's own comment), and finally MAIN_DEMO ("how hard would it be to add the demo
// feature from the main menu of the actual game?" -- see got_start_demo()'s own comment, got_main.c).
#define MAIN_MENU_NUM_ITEMS 6
static const char *s_main_menu_items[MAIN_MENU_NUM_ITEMS] = {
	"Play Game", "High Scores", "Credits", "Demo", "BBS Info", "Quit",
};
enum {
	MAIN_PLAY = 0,
	MAIN_HIGH_SCORES,
	MAIN_CREDITS,
	MAIN_DEMO,
	MAIN_BBS_INFO,
	MAIN_QUIT,
};
static int s_main_menu_selected = MAIN_PLAY;

extern volatile char key_flag[100];
extern const unsigned char *got_get_bpics(void);

// Real animated hammer-cursor sheet (got_main.c's own s_hampic/got_get_hampic() comment) -- this
// screen is the first to actually use it, standing in for the real GOT.EXE main menu's own hammer
// cursor (confirmed by wootbeer's own reference screenshot: a hammer, pointing left, next to whichever
// item is selected).
extern const unsigned char *got_get_hampic(void);

// got_main.c's own side of this screen -- see got_title.h's own comment on why the JNI/save-file/
// game-state work lives there and not here (this file is UI/input only, matching got_menu.c's own
// division of labor with got_main.c).
extern bool got_save_slot_peek(int slot, char *name_out, int name_out_len, int *episode_out);
extern void got_start_new_game(int slot, const char *name, int episode);
extern void got_continue_game(int slot);
// wootbeer: "how hard would it be to add the demo feature from the main menu of the actual game?" See
// got_main.c's own s_demo_data comment for the full design. Returns false (touching nothing) if the
// real DEMO resource never loaded, so MAIN_DEMO's own dispatch below only switches to GOT_APP_PLAYING
// on success -- same "just stays inert" degradation every other main-menu row got at first.
extern bool got_start_demo(void);
extern void got_request_name_prompt(int slot);
// wootbeer, after a recording he'd just made turned out not to be on disk: "how does the demo know to
// stop/recording and save. can it do it when I leave to the main menu?" Called at the top of
// got_title_return_to_menu() below (see that function's own comment) so a dev-only recording in
// progress (got_main.c's own GOT_RECORD_DEMO build flag) gets saved no matter which of this port's
// several paths actually lands here -- a plain no-op the rest of the time, including every normal
// (non-recording) build, so this is always safe to call unconditionally.
extern void got_demo_record_flush_on_exit(void);

// Whether episode `episode` (1-based) actually loaded -- got_main.c's own got_load_real_resources()
// tolerates episode 2/3's BPICS/SDAT failing to read (see that function's own comment) while still
// requiring episode 1, so this screen has to check before ever OFFERING an episode as a choice, not
// just before acting on one. Backed by got_main.c's own got_get_active_episode() plumbing --
// exposed as this one extra getter rather than a whole "is episode N available" query, since
// got_set_active_episode() already refuses (and logs) switching to an unavailable episode; this
// lets the UI pre-filter instead of silently failing a pick and leaving the player stuck.
extern bool got_episode_is_available(int episode);

// got_main.c's own side of the new delete-save feature -- see that function's own comment for what
// it actually does (and why it has no confirmation prompt of its own: this screen's own
// GOT_APP_DELETE_CONFIRM below is that confirmation).
extern bool got_delete_save_slot(int slot);

// got_main.c's own side of the real high-score table -- see that file's own High Score table
// section (above got_request_name_prompt()) for the full design and the score-qualifying/insertion
// logic this screen doesn't need to know about; got_title_draw_high_scores() below only ever reads
// the table, for display, never writes it (score-qualifying and insertion both happen entirely
// inside got_main.c, from got_show_render_buffer()'s own s_episode_complete_pending branch, before
// this screen is ever shown).
extern int got_highscore_count(int episode);
extern void got_highscore_get(int episode, int index, char *name_out, int name_out_len, int *score_out);

#define NUM_EPISODES 3

static GotAppState s_state = GOT_APP_TITLE;
static int s_selected_slot = 0;
static char s_slot_names[NUM_SLOTS][24];
static bool s_slot_used[NUM_SLOTS];
static int s_slot_episode[NUM_SLOTS]; // only meaningful where s_slot_used[i] is true

// GOT_APP_DELETE_CONFIRM's own Yes/No selection -- defaults to 1 ("No") every time the screen opens
// (see the GOT_APP_PLAYER_SELECT case below), not 0/"Yes" the way got_menu.c's own MENU_SAVE_CONFIRM/
// MENU_LOAD_CONFIRM default their own Yes/No boxes -- Save/Load aren't destructive if confirmed by
// reflex, but deleting a slot is, so this one deliberately defaults its cursor to the safe answer
// instead of matching that precedent exactly.
static int s_delete_confirm_selected = 1;
#define DELETE_CONFIRM_YES 0
#define DELETE_CONFIRM_NO 1

// GOT_APP_EPISODE_SELECT's own selection -- 0-based index into the 4-item "Episode 1"/"Episode 2"/
// "Episode 3"/"Back" list (see got_title_draw_episode_select() below), NOT the same numbering as
// the 1-based `episode` got_start_new_game()/got_set_active_episode() take -- that conversion
// (`s_episode_menu_selected + 1`) happens right at the one place it's actually used, in
// GOT_APP_EPISODE_SELECT's own fire handler below, since this screen is now the LAST step of the
// new-game flow (section 78) and calls got_start_new_game() directly rather than carrying the
// episode number forward to some later screen the way an earlier round of this flow needed to.
static int s_episode_menu_selected = 0;

// GOT_APP_CREDITS's own scroll position -- a plain line offset into kCredits[] (got_title_draw_credits()
// below), not a "selected" index the way every other screen's own s_*_selected is: there's nothing to
// select on a pure credits list, just a window that slides up/down. Reset to 0 every time the screen is
// (re-)entered (see the MAIN_CREDITS case above) and clamped to [0, CREDITS_NUM_LINES-CREDITS_VISIBLE_ROWS]
// inside got_title_draw_credits() itself, the same function that already knows both of those values.
static int s_credits_scroll = 0;

static bool s_prev_up = false;
static bool s_prev_down = false;
static bool s_prev_fire = false;
// Edge-detectors for the new delete-save feature's own three keys -- KEY_DELETE_SAVE (read only
// while GOT_APP_PLAYER_SELECT is up) and KEY_CONFIRM/KEY_CANCEL (read only while
// GOT_APP_DELETE_CONFIRM is up), same shared-across-states pattern as s_prev_up/s_prev_down/
// s_prev_fire just above.
static bool s_prev_del = false;
static bool s_prev_confirm = false;
static bool s_prev_cancel = false;

// GOT_APP_HIGH_SCORES's own new paging state, for the three-way split (wootbeer: "there will be 3
// separate screens that are switched between when viewing from the main menu"). s_highscore_page is
// 0-based (0/1/2 -> Part I/II/III), converted to got_main.c's 1-based `episode` numbering right at
// the two call sites that actually need it (got_title_draw_high_scores() below), matching
// s_episode_menu_selected's own established "screen keeps its own 0-based index, convert at the
// edge" convention just above. s_highscore_locked is true only when this screen was reached via
// got_title_show_high_scores(episode) right after finishing that episode (wootbeer: "when the player
// completes a part/episode and is shown the high score screen, it should only show just the page for
// that part") -- false when reached the ordinary way from the main menu's own "High Scores" row,
// where free Left/Right paging among all three is allowed instead. Reset every time the screen is
// (re-)entered from the main menu (see MAIN_HIGH_SCORES's own case below), same as
// s_credits_scroll/s_episode_menu_selected are reset on their own screens' own entry points.
static int s_highscore_page = 0;
static bool s_highscore_locked = false;
static bool s_prev_left = false;
static bool s_prev_right = false;

// Result handoff from GotView.java's name-entry dialog -- see got_title_name_entered()/
// got_title_name_entry_cancelled() below for why this exists instead of those two functions just
// acting immediately. 0 = no pending result, 1 = a name was entered (s_pending_slot/s_pending_name
// hold it), 2 = cancelled. got_title_update() (render thread) polls this once per frame and clears
// it back to 0 after acting on it.
static volatile int s_pending_result = 0;
static int s_pending_slot = -1;
static char s_pending_name[24];
// The episode picked on GOT_APP_EPISODE_SELECT, stashed the same way s_pending_slot/s_pending_name
// already are -- GOT_APP_STORY (got_story.c) needs it for the whole story sequence, and
// got_title_update()'s own GOT_APP_STORY case needs it again at the very end to make the
// got_start_new_game() call that used to happen immediately.
static int s_pending_episode = 1;

// Advances every render call regardless of state -- only GOT_APP_TITLE's own main-menu screen
// actually reads it, to cycle the real HAMPIC cursor's 4 animation frames (see
// got_title_draw_main_menu()), a plain frame counter rather than a clock_gettime() accumulator
// since a rough, render-rate-tied animation is more than good enough for a menu cursor (unlike the
// phase-transition/death-spin effects, nothing here needs to be real-time paced or resume correctly
// across a pause).
static int s_blink_counter = 0;

GotAppState got_title_state(void) {
	return s_state;
}

// Re-reads all 5 slots' own headers (name only -- got_save_slot_peek() doesn't touch the much
// larger save-file body) so the player-select screen always shows what's actually on disk, not
// stale data from an earlier visit. Called once, on the TITLE->PLAYER_SELECT transition below --
// not every frame, since nothing else in this app writes a save file while this screen is up.
static void got_title_refresh_slots(void) {
	int i;
	for (i = 0; i < NUM_SLOTS; ++i) {
		s_slot_names[i][0] = '\0';
		s_slot_episode[i] = 1;
		s_slot_used[i] = got_save_slot_peek(i, s_slot_names[i], (int) sizeof(s_slot_names[i]),
											 &s_slot_episode[i]);
	}
}

void got_title_force_playing(void) {
	s_state = GOT_APP_PLAYING;
}

// See got_title.h's own comment on this function -- called from got_menu.c's pause-menu "Quit to
// Home Screen" confirm, and from several places in got_main.c (demo end/abort, the recording tick
// cap, the post-save-quit flow, etc.) -- in short, every path this port has back to the main menu.
// Runs on the RENDER thread (got_menu.c's own dispatch_confirm(), unlike the name-entry dialog
// callbacks above, already runs there -- no cross-thread handoff needed here), so it's safe to touch
// s_state directly, same as got_title_force_playing() above.
//
// Bugfix (wootbeer, after using the pause menu's own "Quit to Home Screen" mid-recording: "it didn't
// seem the demo was saved. how does the demo know to stop/recording and save. can it do it when I
// leave to the main menu?"). got_demo_record_flush_on_exit() (see its own comment) used to only ever
// get called from one specific spot -- got_demo_record_tick() reaching its own tick cap -- so leaving
// early any other way, exactly what wootbeer did, skipped it and silently discarded the recording.
// Calling it here instead, first thing, means every single path back to the title screen saves a
// dev-only recording in progress the same way, with no need to separately patch each individual exit
// (this one, or any future one) to remember to do so -- a plain no-op the rest of the time.
void got_title_return_to_menu(void) {
	got_demo_record_flush_on_exit();
	s_state = GOT_APP_TITLE;
	s_main_menu_selected = MAIN_PLAY;

	// Bugfix (wootbeer: "when leaving the game via the 'return to opening screen' option, the game's
	// visible area briefly shrinks and then expands back when you return to the menu"). Root cause
	// has nothing to do with the pillarbox math itself -- it's a one-frame content/transform mismatch
	// at this exact transition. got_menu.c's own "Quit to Opening Screen" (and every other caller of
	// this function -- demo end/abort, the post-save-quit flow, etc., all funnel through this one
	// choke point, see this function's own opening comment) calls this mid-frame, from inside
	// got_menu_update()/got_advance_game()'s own dispatch, well BEFORE got_main.c's render loop
	// reaches its own got_title_state()-gated got_title_update()/got_title_draw() call at the top of
	// the NEXT frame. But that same render loop's display-mode block, further down in THIS frame,
	// already reads got_title_state() fresh (see its own comment) and switches straight to Mode C +
	// modex_set_menu_pillarbox(true) the instant s_state flips above -- so this frame's
	// modex_present_frame() presents whatever was last drawn into GOT_PAGE0 (frozen gameplay, plus
	// the pause menu's own box if that's how this was reached -- got_menu_draw() no-ops the instant
	// s_state goes MENU_CLOSED, so it never got a chance to redraw either) through the NEW,
	// differently-scaled menu transform. One frame later, got_title_draw() finally runs and replaces
	// it with the real main menu -- same transform, correct content -- which is what actually reads as
	// the screen "expanding back" a moment later; the intermediate frame (old content, new transform)
	// is the "briefly shrinks" wootbeer saw. Fixed by drawing the fresh main menu into GOT_PAGE0 right
	// here, the instant s_state flips, so the very same frame that switches to the menu's pillarbox
	// transform already has correct, freshly-composed content to show under it -- no mismatched frame
	// ever gets presented. got_title_draw() is a plain state-gated dispatcher (see its own definition)
	// that always fully overwrites GOT_PAGE0 for GOT_APP_TITLE via got_title_draw_main_menu()'s own
	// unconditional xcopys2d() of kMainMenuBg, so calling it early here is side-effect-free -- it's
	// the exact same call got_main.c's render loop would otherwise make one frame later anyway.
	got_title_draw();
}

// See this function's own got_title.h comment. GOT_APP_HIGH_SCORES needs no init of its own beyond
// the state switch itself -- it's a plain live read of the high-score table every frame
// (got_title_draw_high_scores(), below), same as when reached the normal way from the main menu's own
// "High Scores" row, and its own Fire handler already lands back on GOT_APP_TITLE with
// s_main_menu_selected left wherever it was -- since the main menu is never actually shown in between
// on this path, that's still whatever it was before the run started (normally MAIN_PLAY, from
// GOT_APP_PLAYER_SELECT's own entry), so no explicit reset is needed here the way
// got_title_return_to_menu() above needs one.
//
// `episode` (this project's own established 1-based numbering, matching got_main.c's own
// s_current_episode) LOCKS the screen to just that one episode's own table with paging disabled
// entirely -- added for the three-way High Scores split (wootbeer: "when the player completes a
// part/episode and is shown the high score screen, it should only show just the page for that
// part"). Clamped the same way got_main.c's own got_highscore_clamp_episode() is, so an
// out-of-range caller still lands on a real page instead of an empty one.
void got_title_show_high_scores(int episode) {
	if (episode < 1) {
		episode = 1;
	} else if (episode > NUM_EPISODES) {
		episode = NUM_EPISODES;
	}
	s_highscore_page = episode - 1;
	s_highscore_locked = true;
	s_state = GOT_APP_HIGH_SCORES;
}

// Both this and got_title_name_entry_cancelled() below run on the MAIN/UI thread -- called via JNI
// straight from GotView.java's name-entry overlay (promptPlayerName(), a borderless EditText laid
// directly over the game view -- see that method's own comment; was an AlertDialog+EditText before
// section 77) (see got_main.c's own
// Java_wootbeer_gotandroid_GotView_nameEntered()/_nameEntryCancelled() exports), not the render
// thread that owns GOT_PAGE0/the sprite/room state. got_start_new_game() was called directly from
// here originally, and that was the actual bug behind "the player select screen is still drawn over
// the playfield after starting a new game": got_start_new_game() draws a whole room into GOT_PAGE0
// and resets sprite/Thor state, real rendering work with no synchronization against the render
// thread's own concurrent got_title_draw() calls -- while s_state still read GOT_APP_NAME_ENTRY (the
// two statements below aren't atomic, and the dialog/JNI/save-file-write round trip this runs after
// takes long enough for several render-thread frames to land in between), the render thread kept
// painting the OLD player-select box into GOT_PAGE0 on top of the just-drawn room. Because normal
// gameplay drawing afterward only ever repaints small per-sprite bounding boxes from GOT_PAGE2 (see
// got_erase_sprite_previous(), got_main.c), never a full-screen redraw, that stray box paint had
// nothing to ever clear it back out again -- it just sat there indefinitely.
//
// Fixed by not touching any of that state here at all: stash the result instead, and let
// got_title_update() (below, render thread, next frame) actually call got_start_new_game() itself --
// the same deferred cross-thread handoff shape key_flag[]/Want_pause already use elsewhere in this
// port, just carrying a slot+name payload instead of a single flag. Ordering matters: the slot/name
// fields are written before the result flag that gates a render-thread read of them, so by the time
// got_title_update() observes s_pending_result != 0, s_pending_name is already fully written -- no
// torn read of a half-copied buffer.
void got_title_name_entered(int slot, const char *name) {
	s_pending_slot = slot;
	snprintf(s_pending_name, sizeof(s_pending_name), "%s", (name && name[0]) ? name : "Player");
	s_pending_result = 1;
}

void got_title_name_entry_cancelled(void) {
	// Cancel doesn't need to draw anything new (the last player-select frame is already correct,
	// see got_title_draw_player_select()'s own comment), so this could in principle just flip
	// s_state directly like this function used to -- kept going through the same pending-result
	// path as the entered case anyway, for one deferred place got_title_update() handles both
	// outcomes instead of two different cross-thread write patterns in this file.
	s_pending_result = 2;
}

void got_title_update(void) {
	bool up, down, fire;

	++s_blink_counter;

	switch (s_state) {
		case GOT_APP_TITLE:
			up = key_flag[KEY_UP] != 0;
			down = key_flag[KEY_DOWN] != 0;
			fire = key_flag[KEY_FIRE] != 0;
			if (up && !s_prev_up) {
				s_main_menu_selected = (s_main_menu_selected - 1 + MAIN_MENU_NUM_ITEMS)
										% MAIN_MENU_NUM_ITEMS;
			}
			if (down && !s_prev_down) {
				s_main_menu_selected = (s_main_menu_selected + 1) % MAIN_MENU_NUM_ITEMS;
			}
			if (fire && !s_prev_fire) {
				switch (s_main_menu_selected) {
					case MAIN_PLAY:
						got_title_refresh_slots();
						s_selected_slot = 0;
						s_state = GOT_APP_PLAYER_SELECT;
						break;
					case MAIN_HIGH_SCORES:
						// wootbeer: "can we wire up the high score system now?" See GOT_APP_HIGH_SCORES's
						// own got_title.h comment for why (the real GOT.EXE's own "High Scores" table
						// turned out to be a fixed developer-credits easter egg, not a live
						// leaderboard) and got_title_draw_high_scores() below.
						//
						// Unlike got_title_show_high_scores(episode) (called only right after finishing
						// an episode), reaching this screen from the main menu always starts unlocked
						// on Part I with free Left/Right paging among all three -- wootbeer: "there will be
						// 3 separate screens that are switched between when viewing from the main
						// menu." Reset explicitly on every entry (not just once at startup) so backing
						// out to a locked, mid-episode page and returning here later doesn't leave the
						// screen stuck locked on whatever episode was last completed.
						s_highscore_page = 0;
						s_highscore_locked = false;
						s_state = GOT_APP_HIGH_SCORES;
						break;
					case MAIN_BBS_INFO:
						// wootbeer: "let's add in the missing 'BBS' screen that appears when the player
						// selects the 'BBS' option in the main menu." A single static page, no
						// selection/cursor state of its own to initialize -- see GOT_APP_BBS_INFO's
						// own got_title.h comment and got_title_draw_bbs_info() below.
						s_state = GOT_APP_BBS_INFO;
						break;
					case MAIN_CREDITS:
						// See GOT_APP_CREDITS's own got_title.h comment. s_credits_scroll resets to
						// the top every time this screen is (re-)entered -- otherwise a player who
						// scrolled down, backed out, and came back in would land back where they left
						// off instead of at "Android Port:", which reads as broken on a screen with
						// no persistent state of its own to justify remembering a scroll position.
						s_credits_scroll = 0;
						s_state = GOT_APP_CREDITS;
						break;
					case MAIN_QUIT:
						// Real GoT's own title-screen Quit exits straight to DOS -- this is the
						// app-level equivalent (unlike got_menu.c's in-game "Quit to Home Screen",
						// which now returns here instead of exiting, see
						// got_title_return_to_menu()). No confirmation submenu -- wootbeer didn't ask
						// for one here, and there's no in-progress game to lose by quitting from
						// this screen the way there is from the pause menu.
						exit(0);
						break;
					case MAIN_DEMO:
						// wootbeer: "how hard would it be to add the demo feature from the main menu of
						// the actual game?" got_start_demo() (got_main.c) does everything else --
						// spawns Episode 1 fresh and starts feeding it the real recorded attract-mode
						// keystrokes one tick at a time -- this is just the same "switch to
						// GOT_APP_PLAYING" landing every other way into a game already uses. Only
						// switches on success, so a failed DEMO-resource load (see got_start_demo()'s
						// own comment) leaves this row inert instead of dropping into a blank/frozen
						// screen.
						if (got_start_demo()) {
							s_state = GOT_APP_PLAYING;
						}
						break;
					default:
						break;
				}
			}
			s_prev_up = up;
			s_prev_down = down;
			s_prev_fire = fire;
			break;

		case GOT_APP_BBS_INFO:
			// A single static page -- no Up/Down navigation, nothing to select. Any button (Fire,
			// same as A/B both already alias to everywhere else) closes it and returns straight to
			// the main menu, cursor left exactly where it was (still on "BBS Info", since
			// s_main_menu_selected is only ever touched by GOT_APP_TITLE's own up/down handling
			// above, never here).
			fire = key_flag[KEY_FIRE] != 0;
			if (fire && !s_prev_fire) {
				s_state = GOT_APP_TITLE;
			}
			s_prev_fire = fire;
			break;

		case GOT_APP_HIGH_SCORES: {
			// Any button (Fire) closes it and returns straight to the main menu, cursor left on
			// "High Scores" -- unconditionally, whether or not this screen is currently locked (see
			// s_highscore_locked's own comment). Real insertion/persistence logic DOES exist for this
			// table now (got_main.c's own High Score table section) -- it just never runs from here,
			// only from got_show_render_buffer()'s own s_episode_complete_pending branch, well before
			// this screen is ever shown; this screen only ever reads the table, never writes it.
			bool left, right;

			fire = key_flag[KEY_FIRE] != 0;
			if (fire && !s_prev_fire) {
				s_state = GOT_APP_TITLE;
			}
			s_prev_fire = fire;

			// Left/Right page-cycling among the three tables -- new for the three-way split (wootbeer:
			// "there will be 3 separate screens that are switched between when viewing from the main
			// menu"). Only live when this screen was reached unlocked from the main menu itself; a
			// screen shown right after finishing an episode (got_title_show_high_scores(episode))
			// stays locked to that one page the entire time it's up, per wootbeer's own "it should only
			// show just the page for that part." Wraps around both directions, same modulo-with-
			// MAIN_MENU_NUM_ITEMS shape the main menu's own Up/Down navigation above already uses.
			if (!s_highscore_locked) {
				left = key_flag[KEY_LEFT] != 0;
				right = key_flag[KEY_RIGHT] != 0;
				if (left && !s_prev_left) {
					s_highscore_page = (s_highscore_page - 1 + NUM_EPISODES) % NUM_EPISODES;
				}
				if (right && !s_prev_right) {
					s_highscore_page = (s_highscore_page + 1) % NUM_EPISODES;
				}
				s_prev_left = left;
				s_prev_right = right;
			}
			break;
		}

		case GOT_APP_CREDITS: {
			// Unlike every other popup on this screen, Up/Down here scroll the list one line at a
			// time instead of moving a selection -- there's nothing to select on a pure credits
			// list. Same edge-detected "one line per press" shape as got_menu_draw()'s own
			// selection-driven scrolling (wootbeer: "the scrolling mechanism from the pause/options menu
			// with the up/down arrows"), just driven directly by these two keys instead of following
			// a cursor. Clamped in got_title_draw_credits() itself (that function already knows
			// CREDITS_VISIBLE_ROWS and the real line count, so the clamp lives right next to the
			// values it depends on rather than duplicated here) -- this only ever adds/subtracts 1,
			// never needs to know the valid range itself.
			bool up2, down2;

			up2 = key_flag[KEY_UP] != 0;
			down2 = key_flag[KEY_DOWN] != 0;
			if (up2 && !s_prev_up) {
				--s_credits_scroll;
			}
			if (down2 && !s_prev_down) {
				++s_credits_scroll;
			}
			fire = key_flag[KEY_FIRE] != 0;
			if (fire && !s_prev_fire) {
				s_state = GOT_APP_TITLE;
			}
			s_prev_up = up2;
			s_prev_down = down2;
			s_prev_fire = fire;
			break;
		}

		case GOT_APP_PLAYER_SELECT: {
			bool del, cancel;

			up = key_flag[KEY_UP] != 0;
			down = key_flag[KEY_DOWN] != 0;
			fire = key_flag[KEY_FIRE] != 0;
			del = key_flag[KEY_DELETE_SAVE] != 0;
			cancel = key_flag[KEY_CANCEL] != 0;
			if (up && !s_prev_up) {
				s_selected_slot = (s_selected_slot - 1 + NUM_SLOTS) % NUM_SLOTS;
			}
			if (down && !s_prev_down) {
				s_selected_slot = (s_selected_slot + 1) % NUM_SLOTS;
			}
			// wootbeer: "select player needs 'b' to back out as well" -- same direct-cancel shortcut
			// section 82 gave Select Episode, back to the main menu instead. Checked FIRST, same
			// precedence as that fix and GOT_APP_DELETE_CONFIRM's own A/B distinction, so a single B
			// press (which also sets key_flag[KEY_FIRE] the same frame) can't fall through into
			// Continuing or starting whatever slot happens to be highlighted on its way out --
			// landing on MAIN_PLAY matches got_title_return_to_menu()'s own existing choice of where
			// the main-menu cursor should sit after leaving this screen.
			if (cancel && !s_prev_cancel) {
				s_state = GOT_APP_TITLE;
				s_main_menu_selected = MAIN_PLAY;
			} else if (fire && !s_prev_fire) {
				if (s_slot_used[s_selected_slot]) {
					got_continue_game(s_selected_slot);
					s_state = GOT_APP_PLAYING;
				} else {
					// An empty slot means a BRAND NEW game -- ask for a name first, THEN which
					// episode (section 78: wootbeer asked for this order specifically, swapped from the
					// original name-comes-last flow -- see GOT_APP_NAME_ENTRY's own comment on where
					// the episode ask happens now). got_title_name_entered()/
					// got_title_name_entry_cancelled() are still this whole new-game flow's only way
					// OUT of GOT_APP_NAME_ENTRY, unchanged.
					got_request_name_prompt(s_selected_slot);
					s_state = GOT_APP_NAME_ENTRY;
				}
			}
			// New this round -- wootbeer's own delete-save request (see KEY_DELETE_SAVE's own comment).
			// Only a USED slot can be deleted -- pressing Select on an already-empty "New Game" row
			// is a silent no-op, same "nothing to do" precedent this screen's own inert main-menu
			// rows (High Scores/Credits/Demo/BBS Info) already set, rather than opening a confirm box
			// that would just be confirming nothing. Independent of the cancel/fire branch above --
			// KEY_DELETE_SAVE is its own physical button (Select), not part of the A/B ambiguity.
			if (del && !s_prev_del && s_slot_used[s_selected_slot]) {
				s_delete_confirm_selected = DELETE_CONFIRM_NO; // safe default, see that static's own comment
				s_state = GOT_APP_DELETE_CONFIRM;
			}
			s_prev_up = up;
			s_prev_down = down;
			s_prev_fire = fire;
			s_prev_del = del;
			s_prev_cancel = cancel;
			break;
		}

		case GOT_APP_DELETE_CONFIRM: {
			bool confirm, cancel;

			up = key_flag[KEY_UP] != 0;
			down = key_flag[KEY_DOWN] != 0;
			fire = key_flag[KEY_FIRE] != 0;
			confirm = key_flag[KEY_CONFIRM] != 0;
			cancel = key_flag[KEY_CANCEL] != 0;
			if (up && !s_prev_up) {
				s_delete_confirm_selected = (s_delete_confirm_selected == DELETE_CONFIRM_YES)
											 ? DELETE_CONFIRM_NO : DELETE_CONFIRM_YES;
			}
			if (down && !s_prev_down) {
				s_delete_confirm_selected = (s_delete_confirm_selected == DELETE_CONFIRM_YES)
											 ? DELETE_CONFIRM_NO : DELETE_CONFIRM_YES;
			}
			// wootbeer: "when selecting the 'no' option ... the save is still deleted." Root cause:
			// GotView.java's gotScancode() maps BOTH the Retroid Pocket 6's A and B face buttons to
			// KEY_FIRE unconditionally (its own comment: "matching every menu in this game's own
			// 'Fire confirms whichever is highlighted' convention"), and separately,
			// gotExtraScancode() ALSO sends A as KEY_CONFIRM and B as KEY_CANCEL, ADDITIVELY --
			// meaning a single press of A sets key_flag[KEY_FIRE] and key_flag[KEY_CONFIRM] in the
			// exact same frame, every time, with no way to tell them apart. The previous version of
			// this check treated `confirm` as an unconditional, highlight-independent "yes" -- but
			// since it's checked in the DELETE branch first, that meant pressing A while "No" was
			// highlighted (fire=1 from the highlight check failing, confirm=1 unconditionally) still
			// deleted, exactly what wootbeer hit: he'd navigated to "No" and pressed the button he uses
			// to confirm on every other screen, only to have it delete anyway.
			//
			// Fix: check the CANCEL/"No" side first, and let it win on any ambiguity -- explicit
			// Cancel (B), OR Fire while "No" is highlighted, always backs out without deleting,
			// regardless of what Confirm/Fire also happen to say this same frame. Only fall through
			// to actually deleting when neither of those safe signals is present: explicit Confirm
			// (A) with "No" NOT highlighted, or Fire while "Yes" is highlighted. Net effect: B (and
			// D-pad-to-No then any select button) always safely cancels, exactly as before; A only
			// deletes when it isn't contradicted by "No" being the thing on screen, so navigating to
			// "No" and pressing ANY button -- A included -- can no longer delete a save by accident.
			// This is a deliberate asymmetry (favoring "don't delete" over "honor A unconditionally")
			// appropriate for a destructive, unrecoverable action.
			if ((cancel && !s_prev_cancel)
					|| (fire && !s_prev_fire && s_delete_confirm_selected == DELETE_CONFIRM_NO)) {
				s_state = GOT_APP_PLAYER_SELECT;
			} else if ((confirm && !s_prev_confirm)
					|| (fire && !s_prev_fire && s_delete_confirm_selected == DELETE_CONFIRM_YES)) {
				got_delete_save_slot(s_selected_slot);
				got_title_refresh_slots(); // the just-deleted slot now reads as "New Game" again
				s_state = GOT_APP_PLAYER_SELECT;
			}
			s_prev_up = up;
			s_prev_down = down;
			s_prev_fire = fire;
			s_prev_confirm = confirm;
			s_prev_cancel = cancel;
			break;
		}

		case GOT_APP_EPISODE_SELECT: {
			bool cancel;

			up = key_flag[KEY_UP] != 0;
			down = key_flag[KEY_DOWN] != 0;
			fire = key_flag[KEY_FIRE] != 0;
			cancel = key_flag[KEY_CANCEL] != 0;
			if (up && !s_prev_up) {
				s_episode_menu_selected = (s_episode_menu_selected - 1 + (NUM_EPISODES + 1))
										   % (NUM_EPISODES + 1);
			}
			if (down && !s_prev_down) {
				s_episode_menu_selected = (s_episode_menu_selected + 1) % (NUM_EPISODES + 1);
			}
			// wootbeer: "the 'b' button for menus... no longer backs out... it's acting like an 'a'
			// press." B has always aliased to the same KEY_FIRE scancode A does (GotView.java's own
			// gotScancode() comment -- "matching every menu in this game's own 'Fire confirms
			// whichever is highlighted' convention"), so on this screen specifically, pressing B
			// without first navigating down to the "Back" row just confirmed whichever episode
			// happened to be highlighted instead -- exactly the symptom wootbeer hit. Checked FIRST, same
			// precedence GOT_APP_DELETE_CONFIRM below already uses for its own A/B distinction, so a
			// single B press (which sets both KEY_FIRE and KEY_CANCEL the same frame) always backs
			// out and never also confirms an episode on the way past.
			if (cancel && !s_prev_cancel) {
				s_state = GOT_APP_PLAYER_SELECT;
			} else if (fire && !s_prev_fire) {
				if (s_episode_menu_selected == NUM_EPISODES) {
					// "Back" -- the last row (index NUM_EPISODES, i.e. 3) -- abandons the whole
					// new-game attempt, name included, and returns to the slot list with nothing
					// chosen. This screen is now the LAST step (section 78 swapped the order so
					// name entry comes first) rather than returning to GOT_APP_NAME_ENTRY to let the
					// player keep the name they already typed -- kept simple to match this screen's
					// own long-standing "go back one screen, no side effects" precedent rather than
					// adding a second way back into name entry; revisit if that turns out to be
					// annoying in practice. B (above) reaches this exact same destination directly,
					// without needing to navigate down to this row first.
					s_state = GOT_APP_PLAYER_SELECT;
				} else {
					int episode = s_episode_menu_selected + 1; // 0-based row -> 1-based episode
					if (got_episode_is_available(episode)) {
						// Name was already collected back in GOT_APP_NAME_ENTRY (now the PREVIOUS
						// step -- section 78) -- s_pending_slot/s_pending_name are already stashed
						// by got_title_name_entered(). Picking an episode here used to start the
						// game directly; now it stashes the episode too and hands off to the new
						// story sequence (got_story.c, wootbeer: "I want to do the splash screens that
						// show up before starting a new game / new episode") instead -- that
						// sequence's own GOT_APP_STORY case below is what actually calls
						// got_start_new_game() now, once the player's clicked through it.
						s_pending_episode = episode;
						got_story_begin(episode);
						s_state = GOT_APP_STORY;
					}
					// An unavailable episode (see got_episode_is_available()'s own comment) just
					// ignores the press -- same "silently inert" precedent this screen's own
					// High Scores/Credits/Demo/BBS Info main-menu rows already set, rather than
					// letting the player start a game that can't actually load.
				}
			}
			s_prev_up = up;
			s_prev_down = down;
			s_prev_fire = fire;
			s_prev_cancel = cancel;
			break;
		}

		case GOT_APP_NAME_ENTRY:
			// Polls for GotView.java's name-entry overlay result (see got_title_name_entered()/
			// got_title_name_entry_cancelled()'s own comment on why this is deferred here instead
			// of acting from the JNI callback directly). Section 78: this is now reached straight
			// from GOT_APP_PLAYER_SELECT's own empty-slot branch, BEFORE episode selection --
			// wootbeer asked for the order swapped from the original episode-then-name flow, so a
			// name being entered here just advances to GOT_APP_EPISODE_SELECT next
			// (s_pending_slot/s_pending_name are already stashed by got_title_name_entered(), and
			// GOT_APP_EPISODE_SELECT's own fire handler is what actually calls
			// got_start_new_game() once an episode is picked, now the true last step).
			if (s_pending_result != 0) {
				int result = s_pending_result;
				s_pending_result = 0;
				if (result == 1) {
					s_episode_menu_selected = 0;
					s_state = GOT_APP_EPISODE_SELECT;
				} else {
					s_state = GOT_APP_PLAYER_SELECT;
				}
			}
			break;

		case GOT_APP_STORY:
			// got_story.c owns its own input handling (Up/Down scroll, Fire to advance) for
			// whichever of its two screens is currently showing -- this case just advances it once
			// per frame and watches for the whole sequence finishing (the player's clicked through
			// the chapter-title card), at which point it makes the same got_start_new_game() call
			// that used to happen immediately on GOT_APP_EPISODE_SELECT's own fire press.
			got_story_update();
			if (got_story_finished()) {
				got_start_new_game(s_pending_slot, s_pending_name, s_pending_episode);
				s_state = GOT_APP_PLAYING;
			}
			break;

		case GOT_APP_PLAYING:
		default:
			break;
	}
}

// wootbeer: "when the Select Episode menu pops up you can see the frame of the Select Player menu
// underneath it. this is true for a lot of the frames on our Main Menu." Root cause: this function
// sizes its box purely from whatever `title`/`items` it's handed THIS call, with nothing clearing
// GOT_PAGE0 in between screens (see got_title_draw()'s own comment on why -- these screens
// deliberately reuse whatever was last drawn as their own backdrop). Select Player's items ("1. Bob
// (Ep.2)"-style, a name plus episode suffix) and Delete Confirm's title (`Delete "Bob"?`) can both
// end up wider than Select Episode's own content ("Episode 1"/"Select Episode"), and Select
// Episode's own 4 rows (3 episodes + Back) are taller than Select Player's 3 or Delete Confirm's 2
// -- so switching between them can leave whichever screen was wider/taller peeking out past the new,
// smaller box's own frame on any side.
//
// wootbeer's own proposed fix -- "just make all of our menu popups... the same width" -- is exactly
// right, and is simpler than the alternative (redrawing a full backdrop behind every box every
// frame, which these screens were never designed to do). TITLE_BOX_MIN_WIDTH_UNITS/
// TITLE_BOX_MIN_HEIGHT_PX below give every got_title_draw_box() call in this file (Select Player,
// Select Episode, Delete Confirm) a shared floor: whichever of the three needs the least room still
// gets sized up to match the floor, so all three land on the EXACT same box (same x1/y1/x2/y2)
// whenever their actual content fits inside it. Content that's still somehow wider/taller than the
// floor keeps growing past it exactly like before (so nothing gets clipped), which is the only case
// these three boxes could still end up visibly different sizes from each other -- an edge case, not
// the routine bleed-through wootbeer hit.
//
// Follow-up round -- wootbeer: "that works but they are far too wide. I would like them to be the same
// width as the main menu itself." The first pass's floor (24 units -> 224px wide, before the border
// tiles) was picked purely to comfortably fit worst-case CONTENT (a maximum-length slot name, the
// longest fixed string), with no reference to how wide got_title_draw_main_menu()'s own rope-bordered
// box actually looks on screen -- and that box turns out to be narrower than 224px, so once every
// popup got clamped up to fit content, they ended up visibly wider than the one box on this screen
// that ISN'T drawn by this function at all (the main menu's box is baked into kMainMenuBg's own
// background art, not a got_title_draw_box() call, so it was never part of the original "make every
// got_title_draw_box() caller share a floor" fix). Measured kMainMenuBg's own box directly (scanned
// the embedded pixel array for its border/interior color range, got_title_bg.h) rather than eyeballing
// it: its rope border's outer edge sits at x=58 and (by symmetry around this canvas's x=160 center)
// x=261, an outer width of ~204px. TITLE_BOX_MIN_WIDTH_UNITS below is picked to land got_title_draw_box()'s
// own outer width (fill width + the 16px border tile on each side, see the x1/x2 math below) as close
// to that as this function's own 8px-unit/16px-tile granularity allows: 18 units -> 176px fill width
// -> 207px outer width, within a few pixels of the main menu's own box and still an exact multiple of
// 16 (so the horizontal border tiles below tile cleanly edge-to-edge with no leftover gap).
//
// Same round, wootbeer: "can we change it from 3 player slots to 5 slots." That made Select Player
// (NUM_SLOTS rows, see that define's own comment) the tallest of the three screens -- 5*16+32 = 112px,
// taller than Select Episode's 4-row 96px and Delete Confirm's own 2-row 64px -- so
// TITLE_BOX_MIN_HEIGHT_PX (below, at the time) grew to 112 to match, keeping all three screens sharing
// one uniform height the same way they already shared one uniform width.
//
// Follow-up round -- wootbeer: "the 'delete player' sub-menu/popup the frame is too long (vertical) now,
// after the width change." Right: forcing Delete Confirm's own 2-row box up to Select Player's 5-row
// 112px floor left a small "Yes"/"No" list rattling around inside a frame more than twice as tall as it
// needed. The uniform-height floor was never something wootbeer actually asked for the way the uniform
// WIDTH was ("make all of our menu popups... the same width" / "the same width as the main menu
// itself") -- it was just the simplest available fix at the time for the ORIGINAL bleed-through bug
// (see this comment's own opening paragraph), because none of these three screens clear GOT_PAGE0
// between frames the way got_menu.c's own got_menu_draw() does (that function restores a known-clean
// backdrop from GOT_PAGE2 every single frame before drawing its own box -- see its own comment -- which
// is exactly why ITS various submenu boxes, top-level down to a 2-row Yes/No confirm, can each freely
// size to their own natural content with no shared floor and no bleed-through risk at all).
//
// This screen has an equivalent clean backdrop already sitting right here in the same file:
// kMainMenuBg, the same full-canvas background raster got_title_draw_main_menu() itself redraws fresh
// every frame via `xcopys2d(0,0,320,192,0,0,(char*)kMainMenuBg,GOT_PAGE0,320,320)` (see that function's
// own comment). got_title_draw_box() below now does the identical restore as its own first step, so
// EVERY box it draws -- Select Player, Select Episode, Delete Confirm alike -- sits on a guaranteed-
// fresh backdrop every frame, the same root-cause fix got_menu.c already uses, just newly applied here
// too. That removes the actual NEED for a shared height floor: TITLE_BOX_MIN_HEIGHT_PX is gone, and `h`
// below is sized purely from each screen's own `count` again, so Delete Confirm goes back to its
// natural, correctly-proportioned 64px. TITLE_BOX_MIN_WIDTH_UNITS is untouched -- that one WAS an
// explicit, deliberate wootbeer ask (matching the main menu's own box width), unrelated to the bleed-through
// workaround, so all three screens still land on the exact same width as each other and as the main
// menu, just no longer forced to the same height too.
#define TITLE_BOX_MIN_WIDTH_UNITS 18  // 18 * 8px chars + the box's own 32px padding = 176px fill
									   // width; +16px border tile each side = ~207px outer width,
									   // matching got_title_draw_main_menu()'s own box (see above)

// Real select_option()'s own box-sizing/border/text layout math (1_panel.c), the same port
// got_menu_draw() already uses -- see that function's own comment. Duplicated here in miniature
// rather than shared: this screen's box is a fixed 3-row list with no variable-width toggle labels
// (see got_menu.c section 35's own resize-artifact bug and fix), so the layout math is simpler and
// there was no clean way to share it without exposing got_menu.c's internals.
static void got_title_draw_box(const char *title, const char **items, int count, int selected) {
	const unsigned char *bpics;
	int w, h, x1, y1, x2, y2, s, i, vrows;
	int tw;

	// Fresh backdrop every frame, before any box math below -- see TITLE_BOX_MIN_WIDTH_UNITS's own
	// comment (the "Follow-up round" paragraph) for why this is here now: it's what lets `h` just
	// below size purely to this call's own `count` again, with no shared height floor needed to avoid
	// exposing a previous, differently-sized box's own frame underneath.
	xcopys2d(0, 0, 320, 192, 0, 0, (char *) kMainMenuBg, GOT_PAGE0, 320, 320);

	w = got_text_width(title) / 8;
	for (i = 0; i < count; ++i) {
		int iw = got_text_width(items[i]) / 8;
		if (iw > w) {
			w = iw;
		}
	}
	if (w & 1) {
		++w;
	}
	if (w < TITLE_BOX_MIN_WIDTH_UNITS) {
		w = TITLE_BOX_MIN_WIDTH_UNITS;
	}
	w = w * 8 + 32;
	s = w / 16;
	h = count * 16 + 32;
	vrows = (h / 16) - 2;
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

	xfillrectangle(x1, y1, x2, y2, GOT_PAGE0, BOX_COLOR);

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
		for (i = 0; i < vrows + 2; ++i) {
			xfput(x1 - 16, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 198L * 262));
			xfput(x2, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 199L * 262));
		}
	}

	tw = got_text_width(title);
	got_xprint((320 - tw) / 2, y1 + 4, title, GOT_PAGE0, TITLE_COLOR);

	// Cursor: the real animated HAMPIC hammer (got_title_draw_main_menu()'s own comment on
	// got_get_hampic()/s_blink_counter), not a separate plain ">" glyph -- wootbeer: "all menu cursors
	// should be the spinning hammer like the main menu uses... this should be consistent everywhere."
	// Reuses s_blink_counter (already ticking once per render call regardless of state, see that
	// static's own comment) so every got_title_draw_box() caller (Select Player, Select Episode,
	// Delete Confirm) animates in lockstep with the main menu's own cursor rather than drifting out of
	// phase with it. `iy - 3` is the exact same vertical-centering offset
	// got_title_draw_main_menu() already uses to align this 16px-tall sprite against a ~9px text row.
	for (i = 0; i < count; ++i) {
		int iy = (y1 + 28) + i * 16;
		got_xprint(x1 + 32, iy, items[i], GOT_PAGE0, ITEM_COLOR);
		if (i == selected) {
			const unsigned char *hampic = got_get_hampic();
			if (hampic) {
				int frame = (s_blink_counter / 7) % 4;
				xfput(x1 + 8, iy - 3, GOT_PAGE0, (char *) (hampic + frame * 262));
			} else {
				// Same fallback got_title_draw_main_menu() already uses when real art isn't
				// available.
				got_xprint(x1 + 8, iy, ">", GOT_PAGE0, ITEM_COLOR);
			}
		}
	}
}

// The real main menu -- see got_title.h's own scope note for the full provenance. Background is
// kMainMenuBg (got_title_bg.h), generated from wootbeer's own reference screenshot of the real GOT.EXE
// main menu: downsampled to this port's 320x192 canvas and quantized to the exact real PALETTE
// resource this port already loads (see that header's own comment for the generation details and
// why TITLE_COLOR/ITEM_COLOR below land on the same red/yellow the reference shows -- they're the
// same real palette indices, not a coincidence). Title and the 6 item labels are drawn live with
// this port's own real bitmap font (got_xprint(), same TEXT resource/glyph renderer as everywhere
// else in this port) rather than baked into the background raster, so they can be recolored/laid
// out precisely and so the reference's own non-interactive text was never baked in to begin with
// (see got_title_bg.h's own comment). The cursor is the real HAMPIC resource (got_get_hampic()),
// animated over 4 frames -- falls back to a plain ">" (matching got_menu.c's own established
// fallback) if HAMPIC failed to load.
//
// Layout constants below were measured directly off wootbeer's reference screenshot (item row
// positions, text indent) and converted to this port's 320x192 canvas -- not eyeballed -- so the
// proportions match the real thing: 6 rows on a 16px pitch starting at y=60, matching the
// reference's own even row spacing almost exactly once scaled down.
#define MAIN_MENU_TITLE_Y 26
#define MAIN_MENU_ITEM_Y0 60
#define MAIN_MENU_ITEM_PITCH 16
#define MAIN_MENU_ITEM_X 118
#define MAIN_MENU_CURSOR_X 96

static void got_title_draw_main_menu(void) {
	const char *title = "God of Thunder Menu";
	const unsigned char *hampic;
	int tw, i;

	// This background redraw used to go through modex_draw_sprite_frame(), the port's masked-
	// SPRITE blit (treats palette index 0/15 as transparent -- correct for real sprites, see
	// modexgl.c's comment on that function). kMainMenuBg isn't a sprite, it's a full opaque
	// background raster, so that was the wrong primitive on principle even though -- confirmed by
	// scanning the actual embedded array -- it never contains index 0 or 15 at all, meaning it
	// happened to behave identically either way. Switched to xcopys2d(), an unconditional no-
	// transparency source-to-page copy already used elsewhere in this port, matching how real
	// xfarput() itself always treats a full background image ("a plain unconditional copy...
	// appropriate for a full opaque background image meant to completely cover its target", see
	// modexgl.c's comment on xfarput) -- correct on principle, costs nothing, and removes any
	// future risk from this call specifically. This was NOT the actual cause of wootbeer's stray-
	// flashing-blue-pixels report, though -- see got_main.c's got_step_palette_cycle() and
	// got-android-port-notes_1.md section 76 for the real root cause and fix (a missing gate on
	// this port's palette-cycle animation, which was hijacking this exact background's real index
	// 240 -- ordinary static green border art -- into its own blue shimmer colors outside actual
	// gameplay).
	xcopys2d(0, 0, 320, 192, 0, 0, (char *) kMainMenuBg, GOT_PAGE0, 320, 320);

	tw = got_text_width(title);
	got_xprint((320 - tw) / 2, MAIN_MENU_TITLE_Y, title, GOT_PAGE0, TITLE_COLOR);

	for (i = 0; i < MAIN_MENU_NUM_ITEMS; ++i) {
		int iy = MAIN_MENU_ITEM_Y0 + i * MAIN_MENU_ITEM_PITCH;
		got_xprint(MAIN_MENU_ITEM_X, iy, s_main_menu_items[i], GOT_PAGE0, ITEM_COLOR);
	}

	hampic = got_get_hampic();
	{
		int cy = MAIN_MENU_ITEM_Y0 + s_main_menu_selected * MAIN_MENU_ITEM_PITCH;
		if (hampic) {
			// 4 real animation frames, cycled for the same idle "animated" cursor look got_menu.c's
			// own comment on hampic[4] describes -- purely cosmetic, s_blink_counter already exists
			// and ticks once per render call regardless of state (see its own comment), no new
			// timing state needed. wootbeer: "can we speed up the animated hammer by 2x?" -- was /15
			// (~4 frames/sec at 60Hz), now /7 (~8.6 frames/sec), a bit better than a clean 2x since
			// 15 doesn't halve evenly and wootbeer asked for faster, not exactly-2x-or-bust.
			int frame = (s_blink_counter / 7) % 4;
			// -3 vertically centers the 16px-tall hammer sprite against the ~9px-tall text row.
			xfput(MAIN_MENU_CURSOR_X, cy - 3, GOT_PAGE0, (char *) (hampic + frame * 262));
		} else {
			// Same fallback got_title_draw_box()/got_menu_draw() already use when real art isn't
			// available.
			got_xprint(MAIN_MENU_CURSOR_X, cy, ">", GOT_PAGE0, ITEM_COLOR);
		}
	}
}

// GOT_APP_BBS_INFO -- first shipped baking wootbeer's own real reference screenshot straight into a
// background raster (got_bbs_bg.h, the same technique kMainMenuBg itself uses -- see
// got-android-port-notes_1.md section 86), then rewritten to draw the real text live over a flat
// green fill and a plain outlined box (section 89), when wootbeer asked for "closer to an original
// creation instead of just showing my screenshot." wootbeer's follow-up after seeing that version:
// "I do want the actual wood rope border from the main menu, and the green background... The text
// should fit since it obviously fits in the one from the original game." Right -- section 89's own
// reasoning for skipping the rope border (it "comes in fixed 16px units, and this screen's own real
// text needs nearly the full 320px canvas width to fit") wasn't actually a hard blocker: the border
// tiles are decorative and drawn OUTSIDE this box's own fill rectangle (see got_title_draw_box()'s
// own comment on x1-16/x2/y1-16/y2 tile placement), so nothing stops the fill itself from being
// nearly the full canvas width, with the rope tiles simply extending a few pixels past the visible
// screen edge -- harmless, since every xpset()-based primitive (modexgl.c) already clips
// out-of-bounds coordinates rather than corrupting anything. And the "green background" doesn't
// need painting at all: this screen, like every other popup here (Select Player, Episode Select,
// Delete Confirm, High Scores), is only ever reached from GOT_APP_TITLE and draws its own box
// directly over whatever's already in GOT_PAGE0 -- which is always kMainMenuBg's own real grass
// background, still fully intact around a box that doesn't span the entire screen. So this version
// paints nothing but the box: real got_get_bpics() rope-tile border (the exact same corner/edge
// tiles got_title_draw_box() uses, indices 192-199), sized to BBS_BOX_FILL_W/H below rather than
// computed from content the way that function's own `w`/`h` are (its single-column one-item-per-row
// shape can't host this screen's multi-color, multi-column text), with the real BBS text drawn live
// on top exactly as section 89 already had it.
//
// BBS_BOX_FILL_W/H were sized by measuring, not guessing: this screen's widest real line comes to
// exactly 280px (see got_text_width()'s own real measurements, checked against a standalone
// character count before shipping), and its total real content is exactly 136px tall (6 paragraph
// rows + 1 white callout row + a gap + 2 membership rows + a divider + 3 phone rows, each
// BBS_LINE_PITCH apart) -- both then padded (12px/4px) and rounded up to the next clean multiple of
// 16 so the rope-border tiles below tile edge-to-edge with no leftover gap, the same reasoning this
// file's own TITLE_BOX_MIN_WIDTH_UNITS comment already gives. The resulting fill (304x144) plus its
// own 32px of border overshoots the 320px canvas width by 16px total -- 8px of decorative rope tile
// clipped off-canvas on each side, confirmed safe above -- but comfortably fits the 192px canvas
// height with real grass margin still showing above and below.
//
// Colors are the same real low-index EGA-style bank TITLE_COLOR/ITEM_COLOR already draw from (see
// this file's own #define comment for BOX_COLOR/TITLE_COLOR/ITEM_COLOR) -- 10 (light green) for the
// membership pitch, 11 (light cyan) for the phone numbers, 15 (white) for the "order online!"
// callout, and TITLE_COLOR's own literal value (54, red), inlined via got_xprint()'s real `/NN/`
// inline-color-escape syntax (got_font.c's own scan_text(), ported from real xprint() -- not a new
// primitive) rather than a fifth #define, for "hottest" -- the one word in this whole screen the
// reference itself calls out in a different color mid-line. The wording, line breaks, and all three
// phone numbers are transcribed directly from wootbeer's own reference screenshot of the real GOT.EXE
// "BBS Info" page (Software Creations BBS, the real shareware-era support board this game shipped
// with) -- not shortened or reworded.
#define BBS_GREEN 10
#define BBS_CYAN 11
#define BBS_WHITE 15
#define BBS_LINE_PITCH 10 // got_font.c's own 9px-tall glyphs plus a 1px gap -- tighter than every
						   // other popup's 16px row pitch (got_title_draw_box()'s own `h = count * 16
						   // + 32` line), needed to fit this screen's own 11 real lines of text into
						   // 192px tall
#define BBS_TEXT_PAD_H 12
#define BBS_TEXT_PAD_V 4
#define BBS_BOX_FILL_W 304 // 19 * 16px border-tile units -- see this function's own top comment
#define BBS_BOX_FILL_H 144 // 9 * 16px border-tile units -- see this function's own top comment

static void got_title_draw_bbs_info(void) {
	static const char *kParagraph[6] = {
		"The Software Creations BBS is a 24",
		"hour support system for the /54/hottest/14/",
		"games in shareware. You'll find the",
		"latest in shareware, talk with the",
		"authors, meet today's leaders in",
		"software production, and even /15/order",
	};
	// Continues the white "order"/"online!" callout from kParagraph's own last line above -- its own
	// separate got_xprint() call below needs its own explicit BBS_WHITE color param, since an inline
	// `/NN/` escape only affects the single got_xprint() call it appears in, not the next one.
	static const char *kOnlineLine = "online!";
	static const char *kMembership[2] = {
		"Call and become a member of the BBS",
		"voted #1 in the country!",
	};
	// Left label / phone number pairs -- real numbers, from wootbeer's own reference screenshot.
	// got_title_draw_box()'s own items[] are single-column only, so reusing it wasn't an option for
	// this screen's two-column layout; formatted into fixed-width fields below instead, so all three
	// phone numbers land in the same right-hand column regardless of label length.
	static const char *kPhoneLabels[3] = {
		"1200/2400",
		"2400-16.8K HST USR",
		"2400-16.8K V32/V42 bis",
	};
	static const char *kPhoneNumbers[3] = {
		"508-365-2359",
		"508-368-4137",
		"508-368-7036",
	};
	char phone_lines[3][40];
	const unsigned char *bpics;
	int i, x1, y1, x2, y2, s, vrows, y;

	for (i = 0; i < 3; ++i) {
		snprintf(phone_lines[i], sizeof(phone_lines[i]), "%-23s%12s", kPhoneLabels[i], kPhoneNumbers[i]);
	}

	x1 = (320 - BBS_BOX_FILL_W) / 2;
	x2 = x1 + BBS_BOX_FILL_W - 1;
	y1 = (192 - BBS_BOX_FILL_H) / 2;
	y2 = y1 + BBS_BOX_FILL_H - 1;
	s = BBS_BOX_FILL_W / 16;
	vrows = (BBS_BOX_FILL_H / 16) - 2;

	// The message box itself -- deliberately NOT preceded by any full-canvas fill: this screen
	// reuses whatever's already in GOT_PAGE0 (kMainMenuBg's own real grass background) as its own
	// backdrop, same convention got_title_draw_player_select()/got_title_draw_episode_select()/
	// got_title_draw_delete_confirm()/got_title_draw_high_scores() already establish, and exactly
	// what gives this screen "the green background... from the main menu" for free.
	xfillrectangle(x1, y1, x2, y2, GOT_PAGE0, BOX_COLOR);

	// Real rope-tile border -- identical tiles/indices to got_title_draw_box()'s own corner/edge
	// placement (192-195 corners, 196/197 horizontal edges, 198/199 vertical edges), just sized to
	// this function's own BBS_BOX_FILL_W/H instead of that function's content-measured w/h. Corner
	// tiles at x1-16/x2 and y1-16/y2 (and the vertical-edge loop below) legitimately land a few
	// pixels off the left/right edges of the 320px canvas for this screen specifically (see this
	// function's own top comment on the 16px total overshoot) -- every xfput()/xpset() call already
	// clips out-of-bounds pixels safely (modexgl.c), so this just means a sliver of rope texture
	// goes undrawn there, not any actual error.
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
		for (i = 0; i < vrows + 2; ++i) {
			xfput(x1 - 16, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 198L * 262));
			xfput(x2, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 199L * 262));
		}
	}

	y = y1 + BBS_TEXT_PAD_V;
	for (i = 0; i < 6; ++i) {
		got_xprint(x1 + BBS_TEXT_PAD_H, y, kParagraph[i], GOT_PAGE0, ITEM_COLOR);
		y += BBS_LINE_PITCH;
	}
	got_xprint(x1 + BBS_TEXT_PAD_H, y, kOnlineLine, GOT_PAGE0, BBS_WHITE);
	y += BBS_LINE_PITCH + 6; // + the reference's own blank line between the paragraph and the pitch

	for (i = 0; i < 2; ++i) {
		got_xprint(x1 + BBS_TEXT_PAD_H, y, kMembership[i], GOT_PAGE0, BBS_GREEN);
		y += BBS_LINE_PITCH;
	}

	y += 4;
	xline(x1 + BBS_TEXT_PAD_H, y, x2 - BBS_TEXT_PAD_H, y, BBS_WHITE, GOT_PAGE0);
	y += 6;

	for (i = 0; i < 3; ++i) {
		got_xprint(x1 + BBS_TEXT_PAD_H, y, phone_lines[i], GOT_PAGE0, BBS_CYAN);
		y += BBS_LINE_PITCH;
	}
}

// GOT_APP_CREDITS -- wootbeer, right after BBS Info's rope border shipped: "we should also do the credits
// screen now, since we don't have access to the original, we can do it in a similar style, but it
// probably won't have to be as wide." Unlike BBS Info/High Scores, there's no real reference
// screenshot at all here, so this is this port's own original layout -- built on the exact same real
// rope-bordered-box visual system every other popup on this screen already shares (got_get_bpics(),
// the identical 192-199 corner/edge tiles got_title_draw_box()/got_title_draw_bbs_info()/
// got_menu_draw() all draw with) rather than inventing a new one.
//
// kCredits[] below is wootbeer's own uploaded credits text (gotcredits.txt), transcribed verbatim,
// including spellings that differ from got_main.c's own kHighScoreDefaults[] -- "Gray Sirois"/"Adam
// Pederson" here vs. that table's "Gary Sirois"/"Adam Pedersen" -- two independent real sources, not
// a typo to reconcile.
//
// wootbeer's follow-up, before any of this shipped: "make the headings, the lines that end with the
// colons, the red color that has been used, and the rest of the text the yellow that has been used.
// make sure to keep the spacing so the names are offset from the headings." "The red/yellow that has
// been used" is this file's own two established row colors -- TITLE_COLOR (54) and ITEM_COLOR (14),
// the exact pair got_title_draw_box()'s own title/item rows and the real main menu's own title/item
// rows already draw with (see got_title_draw_main_menu()'s own comment on why those two happen to be
// the real low-index palette colors the reference screenshot shows) -- so this screen reuses them
// rather than inventing a new pair just for itself. CREDITS_NAME_INDENT below is the "keep the
// spacing so the names are offset from the headings" part -- every name line draws 16px (2 chars)
// further right than its own heading, mirroring gotcredits.txt's own 3-space indent convention at
// this font's fixed 8px-per-character advance.
//
// CREDITS_BOX_FILL_W was sized the same measured way BBS_BOX_FILL_W was (see that function's own top
// comment): this screen's widest real line is "Additional Level Design:"/"Additional Programming:"
// at 192px (24 chars x 8px, the indent doesn't apply to headings), padded 12px each side and rounded
// up to the next clean 16px multiple for gap-free border tiling -- 224px, narrower than BBS Info's
// 304px, matching wootbeer's own "it probably won't have to be as wide."
//
// 24 real lines (8 headings + 16 names) is too many to show at once on this screen's own 192px
// canvas, so this is the first screen in this file to actually need the scrolling wootbeer pointed to --
// "the scrolling mechanism from the pause/options menu with the up/down arrows" (got_menu_draw()'s
// own scroll/"^"/"v" pattern, see that function's own comment) -- just driven directly by Up/Down
// (got_title_update()'s own GOT_APP_CREDITS case) instead of following a moving selection, since
// there's nothing to select on a pure credits list.
typedef struct {
	const char *text;
	bool is_header; // true for a line ending in ':' (drawn in TITLE_COLOR); false for a name
					 // (drawn in ITEM_COLOR, indented CREDITS_NAME_INDENT past its own heading)
} GotCreditsLine;

static const GotCreditsLine kCredits[] = {
	// wootbeer: "in the credits screen, can we move my 'android port:' and 'wootbeer' entry, and put it
	// underneath 'original programming:' and 'ron davis'. basically just swapping the two entries" --
	// the two heading/name pairs traded places, nothing else about them (is_header flags, indent,
	// scrolling, line count) changed.
	{"Original Programming:", true},
	{"Ron Davis", false},
	{"Android Port:", true},
	{"wootbeer", false},
	{"Graphics:", true},
	{"Gray Sirois", false},
	{"Level Design:", true},
	{"Adam Pederson", false},
	{"Additional Programming:", true},
	{"Jason Blochowiak", false},
	{"Music:", true},
	{"Roy Davis", false},
	{"Additional Level Design:", true},
	{"Ron Davis", false},
	{"Doug Howell", false},
	{"Ken Heckbert", false},
	{"Evan Heckbert", false},
	{"Play Testing:", true},
	{"Ken Heckbert", false},
	{"Doug Howell", false},
	{"Tom King", false},
	{"Kelly Rogers Legault", false},
	{"Michael Smith", false},
	{"Rik Pierce", false},
};
#define CREDITS_NUM_LINES (int) (sizeof(kCredits) / sizeof(kCredits[0]))
#define CREDITS_VISIBLE_ROWS 10 // how many kCredits[] rows show at once -- see this function's own
								 // top comment on why 24 real lines need scrolling to begin with
#define CREDITS_LINE_PITCH 12 // tighter than got_title_draw_box()'s 16px row pitch (same reasoning
							   // BBS_LINE_PITCH already gives -- got_font.c's own 9px-tall glyphs
							   // plus a gap), needed to fit CREDITS_VISIBLE_ROWS into a reasonably
							   // sized box rather than the full canvas height
#define CREDITS_TEXT_PAD_H 12
#define CREDITS_NAME_INDENT 16 // 2 characters -- "keep the spacing so the names are offset from the
								// headings"
#define CREDITS_BOX_FILL_W 224 // 14 * 16px border-tile units -- see this function's own top comment
#define CREDITS_BOX_FILL_H 160 // 10 * 16px border-tile units -- comfortably fits CREDITS_VISIBLE_ROWS
								// (10 rows * 12px pitch = 120px) below the title row with room left
								// for the "^"/"v" scroll markers, same got_menu_draw()-style layout
								// (title at y1+4, rows starting at y1+28) as every other popup here

static void got_title_draw_credits(void) {
	const char *title = "Credits";
	const unsigned char *bpics;
	int x1, y1, x2, y2, s, vrows, i, tw, y;
	int max_scroll;

	max_scroll = CREDITS_NUM_LINES - CREDITS_VISIBLE_ROWS;
	if (max_scroll < 0) {
		max_scroll = 0;
	}
	if (s_credits_scroll < 0) {
		s_credits_scroll = 0;
	}
	if (s_credits_scroll > max_scroll) {
		s_credits_scroll = max_scroll;
	}

	x1 = (320 - CREDITS_BOX_FILL_W) / 2;
	x2 = x1 + CREDITS_BOX_FILL_W - 1;
	y1 = (192 - CREDITS_BOX_FILL_H) / 2;
	y2 = y1 + CREDITS_BOX_FILL_H - 1;
	s = CREDITS_BOX_FILL_W / 16;
	vrows = (CREDITS_BOX_FILL_H / 16) - 2;

	// Same "don't clear the screen" convention every popup on this screen but the main menu itself
	// follows (see got_title_draw_bbs_info()'s own comment) -- kMainMenuBg's real grass backdrop
	// shows through around this box for free, "the green background" wootbeer originally asked BBS Info
	// for and this screen gets along with it, with zero extra drawing code.
	xfillrectangle(x1, y1, x2, y2, GOT_PAGE0, BOX_COLOR);

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
		for (i = 0; i < vrows + 2; ++i) {
			xfput(x1 - 16, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 198L * 262));
			xfput(x2, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 199L * 262));
		}
	}

	tw = got_text_width(title);
	got_xprint((320 - tw) / 2, y1 + 4, title, GOT_PAGE0, TITLE_COLOR);

	// Same "^"/"v" scroll markers, same title-row/bottom-border-row placement, as got_menu_draw()'s
	// own established convention (see that function's own comment).
	if (s_credits_scroll > 0) {
		got_xprint(x2 - 24, y1 + 4, "^", GOT_PAGE0, ITEM_COLOR);
	}
	if (s_credits_scroll < max_scroll) {
		got_xprint(x2 - 24, y2 - 12, "v", GOT_PAGE0, ITEM_COLOR);
	}

	y = y1 + 28;
	for (i = 0; i < CREDITS_VISIBLE_ROWS; ++i) {
		int line_index = s_credits_scroll + i;
		int x;
		if (line_index >= CREDITS_NUM_LINES) {
			break;
		}
		x = x1 + CREDITS_TEXT_PAD_H + (kCredits[line_index].is_header ? 0 : CREDITS_NAME_INDENT);
		got_xprint(x, y, kCredits[line_index].text, GOT_PAGE0,
				   kCredits[line_index].is_header ? TITLE_COLOR : ITEM_COLOR);
		y += CREDITS_LINE_PITCH;
	}
}

// GOT_APP_HIGH_SCORES -- wootbeer, after this port first shipped this screen as a static display of
// wootbeer's own reference screenshot (see got-android-port-notes_1.md section 87): "that works but the
// game actually has a high score system... need this actual system wired up and this screen to be
// created to track the actual scores." Unlike GOT_APP_BBS_INFO just above (genuinely a fixed,
// non-interactive page -- there's really nothing to redraw there), this screen's own content
// changes over time now (got_main.c's own High Score table, see got_highscore_count()/
// got_highscore_get()'s shared comment), so it can't stay a single baked xcopys2d() copy any more --
// reuses got_title_draw_box() instead, the exact same live rope-bordered box every other
// content-driven screen on this file already draws with (Select Player, Select Episode, Delete
// Confirm), formatting each row as a fixed-width "name" field then a right-aligned score so the two
// columns line up the way wootbeer's own reference screenshot's separate name/score boxes did -- not a
// pixel-for-pixel match (this screen never needed to be exact, per that reference screenshot's own
// framing), but the same rope-bordered-wood-box/red-title/yellow-rows look every other popup here
// already has, now actually reflecting whatever's really in the table.
//
// HIGH_SCORE_ROWS_MAX below is a generous upper bound on how many rows this function will ever try
// to draw, NOT the real row count (got_highscore_count() below is) -- unlike NUM_SLOTS/
// NUM_SAVE_SLOTS's own must-move-in-lockstep pair (that file's own comment), this doesn't need to
// match got_main.c's own GOT_HIGHSCORE_MAX_ENTRIES exactly, just be large enough to never truncate
// it -- simpler and safer than adding a second constant this file would have to remember to update.
#define HIGH_SCORE_ROWS_MAX 16
#define HIGH_SCORE_NAME_FIELD 17 // wide enough for "Jason Blochowiak" (16 chars) plus one gap space

// One title string per page, straight off wootbeer's own original reference screenshot (itself titled
// "High Scores - Part I" -- see got-android-port-notes_1.md section 87 -- confirming the real game
// always had this same three-way per-episode split, which this port is only now catching up to).
// Indexed by s_highscore_page (0-based) below.
static const char *s_highscore_titles[NUM_EPISODES] = {
	"High Scores - Part I", "High Scores - Part II", "High Scores - Part III",
};

static void got_title_draw_high_scores(void) {
	const char *items[HIGH_SCORE_ROWS_MAX];
	char labels[HIGH_SCORE_ROWS_MAX][40];
	char name[32];
	int score;
	int i, count;
	int episode = s_highscore_page + 1; // got_main.c's own 1-based numbering, see s_highscore_page's comment

	count = got_highscore_count(episode);
	if (count > HIGH_SCORE_ROWS_MAX) {
		count = HIGH_SCORE_ROWS_MAX;
	}
	for (i = 0; i < count; ++i) {
		got_highscore_get(episode, i, name, sizeof(name), &score);
		snprintf(labels[i], sizeof(labels[i]), "%-*s%6d", HIGH_SCORE_NAME_FIELD, name, score);
		items[i] = labels[i];
	}
	// selected = -1: no row is ever "selected" on this screen (no navigation, see
	// got_title_update()'s own GOT_APP_HIGH_SCORES case), so got_title_draw_box()'s own cursor
	// column never matches any real index here.
	got_title_draw_box(s_highscore_titles[s_highscore_page], items, count, -1);
}

// Shared by GOT_APP_PLAYER_SELECT and GOT_APP_NAME_ENTRY -- while GotView.java's own name-entry
// overlay has focus (see got_title.h's own comment on got_title_request_name_prompt()), this port has nothing
// new of its own to draw, so the last player-select frame just stays up underneath it.
static void got_title_draw_player_select(void) {
	const char *items[NUM_SLOTS];
	char labels[NUM_SLOTS][32];
	int i;

	for (i = 0; i < NUM_SLOTS; ++i) {
		if (s_slot_used[i]) {
			// "1. Bob (Ep.2)" -- shows which episode that slot is actually playing, since
			// Continuing it (unlike an empty slot) skips GOT_APP_EPISODE_SELECT entirely and just
			// resumes whatever that save was already on (got_continue_game(), got_main.c).
			snprintf(labels[i], sizeof(labels[i]), "%d. %s (Ep.%d)", i + 1, s_slot_names[i],
					 s_slot_episode[i]);
		} else {
			snprintf(labels[i], sizeof(labels[i]), "%d. %s", i + 1, "New Game");
		}
		items[i] = labels[i];
	}
	got_title_draw_box("Select Player", items, NUM_SLOTS, s_selected_slot);
}

// GOT_APP_DELETE_CONFIRM -- reused got_title_draw_box() one more time, same visual system as the two
// screens above it and the pause menu's own Yes/No boxes (wootbeer: "Make this new popup be in the same
// style as the game's other menu screens"). Names the slot by its saved player name so a "Yes" here
// can't be confused for a different slot than the one just highlighted on GOT_APP_PLAYER_SELECT.
static void got_title_draw_delete_confirm(void) {
	char title[40];

	snprintf(title, sizeof(title), "Delete \"%s\"?", s_slot_names[s_selected_slot]);
	got_title_draw_box(title, s_yesno_items, YESNO_NUM_ITEMS, s_delete_confirm_selected);
}

// GOT_APP_EPISODE_SELECT -- shown only when starting a BRAND NEW game (see that state's own
// got_title.h comment). Reuses got_title_draw_box() exactly like got_title_draw_player_select()
// above, for the same rope-bordered-box visual consistency with the rest of this screen and the
// pause menu. An episode that failed to load (got_episode_is_available()) is still listed -- not
// hidden -- but suffixed "(unavailable)" so the player understands why FIRE does nothing on it,
// rather than a row silently vanishing or silently doing nothing with no explanation.
static void got_title_draw_episode_select(void) {
	const char *items[NUM_EPISODES + 1];
	char labels[NUM_EPISODES][24];
	int i;

	for (i = 0; i < NUM_EPISODES; ++i) {
		int episode = i + 1;
		if (got_episode_is_available(episode)) {
			snprintf(labels[i], sizeof(labels[i]), "Episode %d", episode);
		} else {
			snprintf(labels[i], sizeof(labels[i]), "Episode %d (unavailable)", episode);
		}
		items[i] = labels[i];
	}
	items[NUM_EPISODES] = "Back";
	got_title_draw_box("Select Episode", items, NUM_EPISODES + 1, s_episode_menu_selected);
}

void got_title_draw(void) {
	switch (s_state) {
		case GOT_APP_TITLE:
			got_title_draw_main_menu();
			break;
		case GOT_APP_BBS_INFO:
			got_title_draw_bbs_info();
			break;
		case GOT_APP_HIGH_SCORES:
			got_title_draw_high_scores();
			break;
		case GOT_APP_CREDITS:
			got_title_draw_credits();
			break;
		case GOT_APP_PLAYER_SELECT:
		case GOT_APP_NAME_ENTRY:
			// GOT_APP_NAME_ENTRY is reached straight from GOT_APP_PLAYER_SELECT's own empty-slot
			// branch now (section 78 swapped the new-game flow's order so a name is asked for
			// BEFORE the episode, not after) -- this port has nothing new of its own to draw while
			// GotView.java's own name-entry overlay has focus (same reasoning this case's original
			// comment already gave, just pointed at the other backdrop now that the order flipped),
			// so the player-select frame is the correct one to keep showing underneath it.
			got_title_draw_player_select();
			break;
		case GOT_APP_DELETE_CONFIRM:
			got_title_draw_delete_confirm();
			break;
		case GOT_APP_EPISODE_SELECT:
			// Reached from GOT_APP_NAME_ENTRY once a name's been entered (section 78) -- a real,
			// independently-navigable screen in its own right (not "waiting on Android"), so it
			// draws its own box normally rather than needing to fall through to some other case's
			// backdrop the way GOT_APP_NAME_ENTRY above does.
			got_title_draw_episode_select();
			break;
		case GOT_APP_STORY:
			// got_story.c draws its own two full-canvas screens (real backdrop/icons/text, or the
			// chapter-title card) -- no fallback-to-some-other-backdrop needed the way
			// GOT_APP_NAME_ENTRY above does, since got_story_draw() always fully covers the canvas
			// itself either way.
			got_story_draw();
			break;
		case GOT_APP_PLAYING:
		default:
			break; // got_show_render_buffer()'s own PLAYING branch draws everything now
	}
}
