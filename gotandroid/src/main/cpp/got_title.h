#ifndef GOT_TITLE_H_
#define GOT_TITLE_H_

#include <stdbool.h>

// Title screen / player-selection screen -- what runs before a room, Thor, or the pause menu exist
// at all. wootbeer: "I think its time to add the stuff before the first board of the game actually
// starts. so the logo/intro screens, the title screen, and main menu... we can add the save and
// load game feature and option to the pause menu and wire it all up." Scoped down from that in
// conversation: no logo/intro story crawl for now (real story(), 1_init.c, is a whole separate,
// fairly involved scrolling-text-and-picture effect -- wootbeer: "skip for now"), a minimal placeholder
// title screen (wootbeer's own choice over reusing real story-sequence art in an original layout), and
// "player selection" as named save slots (5 of them as of wootbeer's own later request -- see
// got_title.c's own NUM_SLOTS comment) -- wootbeer: "there has to be [more than one], it's called
// 'player selection', and then in the options menu during gameplay it's a very simple load game /
// save game system, it only saves and loads to the slot the user chose during the initial 'player
// selection' screen."
//
// IMPORTANT SCOPE NOTE, unlike every other UI surface this port has built so far (the pause menu,
// the HUD, the touch D-pad): there is NO real EXECUTABLE CODE to port this from. Real GOT1.EXE (the
// only gameplay-engine binary/source this project works from, see got-android-port-notes_1.md
// section 1a) is only the gameplay engine -- title screen, main menu, and player/save selection all
// lived in a separate real executable (run_gotm()/GOT.EXE, 1_main.c) that was never part of the
// decompiled archive this project works from, and still isn't (a real copy has since turned up in
// one of wootbeer's own reference archives -- trn-gotr.zip -- but as a standalone real-mode DOS binary
// with no source and no way to run/disassemble it in this environment, not something this port can
// actually port logic FROM). So this file's INPUT HANDLING/STATE MACHINE is still an original
// design, not a port. What changed (see got_title.c's own comment on got_title_draw_main_menu()):
// this screen's VISUALS are no longer original-design placeholder art -- wootbeer supplied a real
// reference screenshot of the real GOT.EXE main menu (rope-bordered wood box, red title, yellow
// items, animated hammer cursor), and this port's own background art is generated straight from
// that screenshot, quantized to the real PALETTE resource. The hammer cursor itself is the real
// HAMPIC resource, not reference-derived at all -- see got_main.c's own s_hampic comment. The one
// piece that was ALREADY a faithful port despite the missing-GOT.EXE gap is the save/load file
// FORMAT and the real fields it captures -- see got_main.c's own GotSaveHeader/got_save_write_slot()/
// got_save_read_slot_full() comments, ported from real save_game()/load_game() (1_file.c), which
// (unlike the title/menu program) ARE part of the gameplay engine's own decompiled source.

typedef enum {
	GOT_APP_TITLE = 0,        // The real main menu -- rope-bordered wood box on a grass background
							   // (wootbeer's own real reference screenshot, see got_title.c), "Play
							   // Game"/"High Scores"/"Credits"/"Demo"/"BBS Info"/"Quit" with a real
							   // animated hammer cursor. Only "Play Game" (-> GOT_APP_PLAYER_SELECT)
							   // and "Quit" actually do anything yet -- see got_title.c's own
							   // got_title_draw_main_menu()/got_title_update() comments for why the
							   // other four are drawn but inert for now.
	GOT_APP_PLAYER_SELECT,    // 5 named slots (grown from the original 3 -- wootbeer: "can we change it
							   // from 3 player slots to 5 slots") -- pick an existing one to Continue, or an empty one
							   // to start naming a new game. Reached only via "Play Game" on the
							   // main menu above now, not directly from GOT_APP_TITLE itself. The
							   // Retroid Pocket 6's B button (KEY_CANCEL) backs straight back out to
							   // GOT_APP_TITLE (wootbeer: "select player needs 'b' to back out as well",
							   // right after the same request for GOT_APP_EPISODE_SELECT below) --
							   // this screen never had any way back to the main menu before this.
	GOT_APP_EPISODE_SELECT,   // "Episode 1"/"Episode 2"/"Episode 3"/"Back" -- reached only from
							   // GOT_APP_NAME_ENTRY once a name's been entered for a BRAND NEW game
							   // (Continuing an existing slot skips this entirely, since that slot's
							   // episode is already fixed, read from its own save header, see
							   // got_save_slot_peek()'s own episode_out param). Section 78 made this
							   // the LAST step of the new-game flow (wootbeer asked for name-then-episode
							   // instead of the original episode-then-name order) -- picking an
							   // episode here calls got_start_new_game() directly and lands on
							   // GOT_APP_PLAYING; "Back" abandons the whole attempt, name included,
							   // and returns to GOT_APP_PLAYER_SELECT -- the Retroid Pocket 6's B
							   // button (KEY_CANCEL) also reaches that same destination directly,
							   // without navigating down to the "Back" row first (wootbeer: "the 'b'
							   // button for menus... no longer backs out... it's acting like an 'a'
							   // press" -- B had only ever aliased to Fire on this screen before).
							   // Reuses got_title_draw_box() (the same generic box helper
							   // GOT_APP_PLAYER_SELECT already draws with), same visual system as
							   // everywhere else in this screen.
	GOT_APP_NAME_ENTRY,       // waiting on GotView.java's own name-entry overlay -- promptPlayerName(),
							   // a borderless EditText laid directly over the game view, see that
							   // method's own comment (was an AlertDialog+EditText before section 77) --
							   // (see got_title_request_name_prompt()) for a brand new slot's name.
							   // Reached straight from GOT_APP_PLAYER_SELECT's own empty-slot branch
							   // now (section 78's reordering); a name entered here advances to
							   // GOT_APP_EPISODE_SELECT above next, not straight into the game.
	GOT_APP_DELETE_CONFIRM,   // "Delete Save?" Yes/No box -- reached only from GOT_APP_PLAYER_SELECT,
							   // only on a USED slot (the Retroid Pocket 6's physical Select button,
							   // KEY_DELETE_SAVE, see got_title.c's own comment on that key). Same
							   // got_title_draw_box() visual system as GOT_APP_EPISODE_SELECT above,
							   // for "the same style as the game's other menu screens" wootbeer asked
							   // for -- navigable with the usual Up/Down+Fire, plus a direct A/B
							   // (KEY_CONFIRM/KEY_CANCEL) shortcut on top, see got_title.c's own
							   // GOT_APP_DELETE_CONFIRM case for both paths. Returns to
							   // GOT_APP_PLAYER_SELECT either way, slots refreshed if it deleted one.
	GOT_APP_BBS_INFO,         // the main menu's "BBS Info" row -- previously one of the four
							   // deliberately-inert rows (wootbeer: "we don't have to add the other
							   // options right now if we don't have the functions for them," see
							   // got_title.c's own s_main_menu_items comment), now real. First shipped
							   // as a background image baked straight from wootbeer's own reference
							   // screenshot (got_bbs_bg.h's kBbsInfoBg, since deleted); wootbeer's
							   // follow-up ("I want it to be closer to an original creation instead
							   // of just showing my screenshot") replaced that with the real text
							   // (transcribed verbatim from that same reference) drawn live via
							   // got_xprint() over a plain filled box -- see
							   // got_title_draw_bbs_info()'s own comment for the full writeup,
							   // including why the reference's own wood-grain/rope-border art isn't
							   // reproduced. Reached only from GOT_APP_TITLE's MAIN_BBS_INFO row; any
							   // button (Fire) returns straight back to GOT_APP_TITLE, cursor left
							   // exactly where it was (still on "BBS Info"), since nothing about
							   // picking a main-menu row needs to change just because the player
							   // looked at this page and came back.
	GOT_APP_HIGH_SCORES,      // the main menu's "High Scores" row. First shipped as a single static
							   // page (wootbeer: "can we wire up the high score system now?", then, once
							   // this port's own research turned up no real score-insertion logic
							   // anywhere in the decompiled gameplay-engine source to port, a baked
							   // display of wootbeer's own reference screenshot -- same shape as
							   // GOT_APP_BBS_INFO just above). wootbeer's follow-up correction: "the game
							   // actually has a high score system, the game keeps score when you kill
							   // enemies, collect items, etc. need this actual system wired up and
							   // this screen to be created to track the actual scores" -- right:
							   // thor_info.score (real 1_panel.c) really is tracked live during play
							   // (got_thor_get_score(), got_main.c), it just needed to actually be
							   // CHECKED against something. Now is: got_main.c's own High Score table
							   // section (its own top comment, above got_request_name_prompt()) checks
							   // it the instant the one real "this game session is over" event this
							   // port has fires -- finishing Episode 1 (wootbeer's own chosen trigger, of
							   // three offered, since the real game hands off to its own high-score
							   // screen at exactly this same moment) -- and this screen now draws
							   // whatever's actually in that table live (got_title_draw_high_scores(),
							   // got_title.c), not a fixed baked image. The seven names from wootbeer's
							   // own reference screenshot (Ron Davis, Gary Sirois, Adam Pedersen,
							   // Jason Blochowiak, Roy Davis, Wayne Cimmerman, Dan Linton) are still
							   // in here -- confirmed genuine developer-credits data, not placeholder,
							   // by "Jason Blochowiak" matching utility/digisnd/digisnd.c's own real
							   // copyright name verbatim -- just now as the table's own DEFAULT seed
							   // (got_main.c's own kHighScoreDefaults), fully replaceable by a real
							   // qualifying score rather than fixed forever. Reached only from
							   // GOT_APP_TITLE's MAIN_HIGH_SCORES row; any button (Fire) returns
							   // straight back to GOT_APP_TITLE, cursor left on "High Scores" -- same
							   // as before, that part of this screen's own shape didn't change.
							   //
							   // Split into three separate per-episode tables/screens (wootbeer: "we need
							   // to separate the high scores into 'High Scores - Part I' 'High Scores
							   // - Part II' and 'High Scores - Part III'. there will be 3 separate
							   // screens that are switched between when viewing from the main menu.
							   // when the player completes a part/episode and is shown the high score
							   // screen, it should only show just the page for that part") -- matching
							   // wootbeer's own original reference screenshot, itself titled "High Scores
							   // - Part I", confirming the real game always had this same 3-way split.
							   // `s_highscore_page`/`s_highscore_locked` (got_title.c) track which
							   // part is showing and whether Left/Right can page between them: reached
							   // from the main menu's own row, unlocked, starting on Part I, Left/Right
							   // cycle freely (wrapping) between all three; reached via got_title_show_
							   // high_scores(episode) (just below) after finishing a run, LOCKED to
							   // that one episode's own page with paging disabled entirely, matching
							   // wootbeer's own "should only show just the page for that part." Either way,
							   // Fire still returns straight back to GOT_APP_TITLE, unchanged.
	GOT_APP_CREDITS,          // the main menu's "Credits" row -- the last of the four originally-
							   // inert rows to go real (Demo is still the one exception). Unlike BBS
							   // Info/High Scores, there's no real reference screenshot to work from
							   // here at all -- wootbeer: "since we don't have access to the original, we
							   // can do it in a similar style" -- so this screen is this port's own
							   // original layout (got_title_draw_credits(), got_title.c), just reusing
							   // the same real rope-bordered-box visual system (got_get_bpics()) every
							   // other popup here already does, with the real name/role list wootbeer
							   // provided verbatim (got_title.c's own kCredits[]). 24 lines is too
							   // many to fit this screen's own 192px canvas at once, so this is the
							   // first screen here to actually need the scrolling wootbeer pointed to --
							   // "the scrolling mechanism from the pause/options menu with the up/down
							   // arrows" (got_menu_draw()'s own MENU_MAX_VISIBLE_ITEMS/scroll/"^"/"v"
							   // pattern) -- Up/Down scroll one line at a time rather than moving a
							   // selection (there's nothing to select on a pure credits list), Fire
							   // still returns to GOT_APP_TITLE same as every other one-way popup here.
	GOT_APP_STORY,            // the new-game story sequence (got_story.c) -- real narration text
							   // then a chapter-title card, shown once an episode's been picked and
							   // BEFORE the game itself actually starts. Reached only from
							   // GOT_APP_EPISODE_SELECT's own fire handler, in place of what used to
							   // be an immediate got_start_new_game() call; got_title_update()'s own
							   // GOT_APP_STORY case makes that same call itself, but only once
							   // got_story_finished() goes true (the player's dismissed the chapter
							   // title), then moves straight to GOT_APP_PLAYING exactly as before.
							   // wootbeer: "I want to do the splash screens that show up before starting
							   // a new game / new episode."
	GOT_APP_PLAYING,          // a game is running -- got_show_render_buffer()'s existing menu/
							   // phase-transition/death-spin/script/got_advance_game() dispatch owns
							   // the frame from here, same as before this file existed
} GotAppState;

// Current top-level app state -- got_show_render_buffer() (got_main.c) switches its whole per-frame
// dispatch on this, the same way it already switches on got_menu_is_open()/
// got_phase_transition_is_active()/etc.
GotAppState got_title_state(void);

// Advances title/player-select/name-entry input by one frame -- UUP/DOWN/FIRE via the same shared
// key_flag[] array got_menu.c already reads (fed by both a physical D-pad/gamepad and this port's
// own on-screen touch buttons, see got_controls.c), so no new input plumbing was needed. Call once
// per render call instead of got_advance_game()/got_menu_update() while got_title_state() isn't
// GOT_APP_PLAYING. Ignores key_flag[] during GOT_APP_NAME_ENTRY (there's nothing for this port's
// own input to do while Android's own dialog has focus) but is NOT a no-op there -- it's also the
// only place that polls for and acts on GotView.java's dialog result (see got_title_name_entered()/
// got_title_name_entry_cancelled() below for why that result is handled here, on the render thread,
// rather than immediately from the JNI callback that receives it).
void got_title_update(void);

// Draws whichever of the three non-gameplay screens is current into GOT_PAGE0 -- call after
// got_title_update(), before modex_present_frame(), instead of got_menu_draw()/the HUD, under the
// same condition as got_title_update() above.
void got_title_draw(void);

// Called once, from gotMain() (got_main.c), only when got_load_real_resources() fails -- there's no
// real BPICS1/font to draw a title/player-select screen with, and no real SDAT1 to save/load a
// slot's world state against either, so this skips straight to GOT_APP_PLAYING and lets the old
// got_test_draw() synthetic-shapes fallback run exactly like every pre-title-screen build of this
// app already did. Never called for any other reason -- a normal launch always starts at
// GOT_APP_TITLE (this file's own static initial state).
void got_title_force_playing(void);

// Called from got_menu.c's pause-menu "Quit to Home Screen" confirm (see that file's own
// dispatch_confirm() comment on the bug this replaces: it used to call exit(0), which really did
// just close the whole app despite its label already saying "Home Screen" -- wootbeer: "we do need to
// add the 'return to home screen' option from the pause/options menu in game, it currently just
// closes the app"). Drops straight back to GOT_APP_TITLE's own main menu, selection reset to "Play
// Game" -- got_show_render_buffer()'s own `got_title_state() != GOT_APP_PLAYING` dispatch (see that
// function's own comment) picks this up automatically on the very next frame, the same way it
// already picks up GOT_APP_PLAYER_SELECT/GOT_APP_NAME_ENTRY, no other wiring needed. Deliberately
// does NOT tear down or reset any in-progress room/Thor/save-slot state -- there's nothing to tear
// down that Play Game -> Select Player -> Continue/New Game doesn't already fully re-initialize the
// next time the player actually starts a game (got_continue_game()/got_start_new_game()), the exact
// same as if the app were freshly launched and this were the first screen shown.
void got_title_return_to_menu(void);

// Jormangund boss-fight overhaul follow-up (wootbeer: "crossing the bridge worked. however it just takes
// you to the main menu after adding the score, instead of showing you the high score screen"). Drops
// into GOT_APP_HIGH_SCORES instead of straight back to GOT_APP_TITLE -- same render-thread-only
// safety as got_title_return_to_menu() just above (called from the same got_show_render_buffer()
// dispatch), and same destination the main menu's own "High Scores" row already reaches, so
// GOT_APP_HIGH_SCORES's own existing Fire-to-continue handling (got_title_update()'s own case, which
// lands back on GOT_APP_TITLE) is all that's needed to get from here back to the main menu -- no new
// screen, no new state, just a new way in. Called from got_show_render_buffer()'s own
// s_episode_complete_pending/s_highscore_prompt_outstanding dispatch (got_main.c) once an episode's
// run is fully over: after a qualifying score's name has been entered (or that prompt cancelled), and
// also when the score didn't qualify for the table at all -- either way the player just finished a
// run and the table is the more useful thing to land on than the bare main menu, matching classic
// arcade "game over, here's the board" convention. See GOT_APP_HIGH_SCORES's own got_title.c comment,
// which already anticipated this call site before it existed ("[insertion logic] just never runs from
// here, only from got_show_render_buffer()'s own s_episode_complete_pending branch, well before this
// screen is ever shown") -- this function is what actually connects the two.
//
// `episode` (this project's own established 1-based numbering, matching s_current_episode) LOCKS the
// screen to just that one episode's own table with paging disabled entirely -- added for the
// three-way High Scores split (wootbeer: "when the player completes a part/episode and is shown the high
// score screen, it should only show just the page for that part"), see s_highscore_locked's own
// comment for exactly what "locked" changes. Always called with whichever episode the just-finished
// run was actually playing (`s_current_episode` at the got_main.c call site), never a fixed value.
void got_title_show_high_scores(int episode);

// Called from got_main.c's own JNI export (Java_wootbeer_gotandroid_GotView_nameEntered()) once the
// player confirms a name for the empty slot got_request_name_prompt() (got_main.c) was called for.
// Runs on the MAIN/UI thread, NOT the render thread -- so, deliberately, this does NOT call
// got_start_new_game() itself (an earlier version did, and that was a real bug: see this function's
// own definition comment in got_title.c for what it broke). It only records the result;
// got_title_update() above picks it up and does the actual game-start on the render thread, on the
// very next frame. `name` is a plain NUL-terminated UTF-8 string from Android's own EditText --
// got_start_new_game() truncates/sanitizes it, this file doesn't need to.
void got_title_name_entered(int slot, const char *name);

// Called from the same JNI export when the player instead dismisses/cancels the dialog (an empty
// name, or the Android back button/outside-tap) -- same deferred-to-got_title_update() handoff as
// got_title_name_entered() above, resulting in a return to GOT_APP_PLAYER_SELECT with nothing
// changed, same slot still empty, no game started.
void got_title_name_entry_cancelled(void);

#endif
