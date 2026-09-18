#ifndef GOT_STORY_H_
#define GOT_STORY_H_

#include <stdbool.h>

// New-game "story" sequence -- the two-screen sequence real GoT shows once per brand new episode,
// before the game itself begins: a scrollable page of real narration text on a plain black backdrop
// (real STORY1/STORY2/STORY3 script, Odin's face and -- episode 1 only -- the hammer icon scrolling
// alongside their own real lines, real action3.mp3 playing, all drawn under the real STORYPAL
// palette), followed by a plain chapter-title card ("God Of Thunder" / "Part N: <subtitle>", no
// music, normal game palette). wootbeer's original request: "I want to do the splash screens that show
// up before starting a new game / new episode... we will use existing game fonts / colors and assets
// to recreate these screens in code, do not just re-use my screenshots as is." Real source confirmed
// via direct read of _g1's (and g2's/g3's) own 1_init.c/2_init.c/3_init.c story()/initialize()
// functions -- see got_story.c's own top comment for the full citation, including two rounds of
// on-device feedback that shaped the final look (dropping the real OPENP1-12 backdrop and a
// rope-bordered-box treatment in favor of plain black, and wiring up the real STORYPAL palette that
// turned out to be the actual cause of "the colors are all funky"/"almost inverted"). Not a literal
// port of real story()'s own raw VGA-page-flipping mechanics (this port has no equivalent of real
// hardware page-flip scrolling) -- just the real assets, real text/color escapes, and real per-line
// icon placement, driven by a discrete per-line Up/Down scroll (wootbeer: "if all the text doesn't fit on
// one screen we can use a scrolling method like we did on the credits" -- got_title.c's own
// GOT_APP_CREDITS scroll, the established precedent this matches exactly).
//
// Owns its own tiny state machine (which of the two screens is showing, current scroll position);
// got_title.c's own GOT_APP_STORY state is a thin wrapper around the four calls below, exactly the
// same shape its GOT_APP_BBS_INFO/GOT_APP_CREDITS cases already are around got_title.c's own local
// draw functions -- the difference here is just that the underlying logic lives in its own file
// rather than a local static function, since it's substantial enough (real resource loads, a real
// palette switch, a real fourth color-escape syntax, its own scroll state) to warrant one.

// Called once at startup (got_main.c's got_load_real_resources(), alongside every other real
// resource this port loads once and keeps for the app's lifetime) -- currently a no-op placeholder
// (all the actual resource loading lives in got_main.c itself, exposed via got_get_storypic()/
// got_get_storypal()/got_get_story_text(), same one-getter-per-resource pattern as got_get_hampic()
// and friends) but kept as a real entry point in case that ever needs to move here instead, matching
// got_font_init()'s own shape.
void got_story_init(void);

// Begins the new-game story sequence for `episode` (1-based, matching every other episode-numbered
// API in this port). Called from got_title.c's GOT_APP_EPISODE_SELECT fire handler INSTEAD OF
// calling got_start_new_game() directly -- got_title.c stashes its own already-collected
// slot/name and switches to a new GOT_APP_STORY state; got_story_finished() below (once true) is
// its cue to finally call got_start_new_game() and switch to GOT_APP_PLAYING, exactly mirroring
// what used to happen immediately. Resets to the very first (story-text) screen, scrolled to the
// top, and starts its own action3.mp3 (real: wootbeer's own explicit pick, not tied to any real
// per-episode resource -- see got_main.c's own MUSIC_STORY comment).
void got_story_begin(int episode);

// Advances input handling for whichever of the two story screens is currently showing. Call once
// per frame (got_title.c's own got_title_update(), its GOT_APP_STORY case) while the sequence is
// running. Up/Down scroll the current screen's own content (story-text screen only -- the
// chapter-title card is always short enough to need none); Fire advances to the next screen,
// or -- on the chapter-title card -- finishes the whole sequence (got_story_finished() below then
// starts reading true).
void got_story_update(void);

// Draws whichever of the two story screens is currently showing. Call once per frame (got_title.c's
// own got_title_draw(), its GOT_APP_STORY case) in place of any other got_title_draw_*() call, onto
// the same GOT_PAGE0 canvas every other title-flow screen draws onto.
void got_story_draw(void);

// True once the player has dismissed the chapter-title card -- got_title.c's own GOT_APP_STORY case
// checks this once per frame right after got_story_update() and, the moment it goes true, calls
// got_start_new_game() with its own already-stashed slot/name/episode and switches to
// GOT_APP_PLAYING. Stays false for the whole rest of the sequence, including while still on the
// story-text screen.
bool got_story_finished(void);

#endif
