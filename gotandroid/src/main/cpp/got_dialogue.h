#ifndef GOT_DIALOGUE_H_
#define GOT_DIALOGUE_H_

#include <stdbool.h>

// The real dialogue-box UI GLOBE (and every other real speaking actor) uses -- a direct port of
// real display_speech() (1_back.c) for SAY/TEXT/ITEMSAY boxes, plus a select_option()-backed ASK
// prompt for the real ASK command, both driven by got_script.c's script interpreter. Adapted from
// display_speech()'s/select_option()'s own real blocking while(1) loops to this port's per-frame
// render loop, the same way got_menu.c already adapted select_option() for the pause menu (in
// fact this file's ASK box reuses that exact real box-sizing/border math again, a third real
// caller of the same real function).
//
// Draws into the real low-res page buffer (GOT_PAGE0), not a GL overlay -- same reasoning as
// got_menu.c's own (real display_speech()/select_option() are themselves part of GoT's own
// in-game rendering, using the same xfillrectangle/xfput/xtext primitives as everything else).

// ---- SAY / TEXT / ITEMSAY box (real display_speech()) ----

// Opens the dialogue box and starts the real typewriter-paced reveal of `text` (already fully
// concatenated by got_script.c's cmd_say(), including real embedded '\n' paragraph breaks and
// "~N"/"~0" real color-escape codes -- see got_script.c's own comment). `pic` is the real 4-frame,
// 262-bytes-per-frame portrait sheet (e.g. ODINPIC) drawn at its fixed real position (152,65); NULL
// for none (real TEXT command boxes have no portrait). `sound_on_reveal` mirrors real
// display_speech()'s own `tf` parameter: SAY/ITEMSAY pass true (WOOP plays as each character
// reveals, matching real cmd_say(mode,1)), TEXT passes false (real cmd_say(0,0)). `icon_obj` is real
// display_speech()'s own `item` parameter -- a real, already-offset 0-based OBJECTS resource index
// (real cmd_say()'s own `if(obj) obj+=10;`, ITEMSAY only; ordinary SAY/TEXT always pass 0) -- drawn
// at (176,65), 24px right of the portrait, only when nonzero (real: `if(item)
// xfput(176,65,pg,objects[item]);`). See got_script.c's own cmd_say() for where this value comes
// from and got_dialogue.c's own draw_portrait() sibling for how it's drawn.
void got_dialogue_open(const char *text, const unsigned char *pic, bool sound_on_reveal,
						int icon_obj);
bool got_dialogue_is_open(void);
// True once the box that just closed was dismissed via ESC (skipped) rather than read to the end
// -- real display_speech() returns 0 for that case. Valid to read only immediately after
// got_dialogue_is_open() has just gone from true to false this frame.
bool got_dialogue_was_cancelled(void);
// Must be called every frame got_dialogue_is_open() is true, before got_dialogue_draw().
void got_dialogue_update(void);
void got_dialogue_draw(void);

// ---- ASK prompt (real select_option(), as real cmd_ask() itself uses) ----

// Opens a list-selection prompt with `title` and `count` `options` (real ASK's own comma-
// separated option strings) -- direct reuse of real select_option()'s box-sizing/border/cursor
// logic (see got_menu.c's own got_menu_draw() for the first real port of that same function).
void got_ask_open(const char *title, const char *const *options, int count);
bool got_ask_is_open(void);
void got_ask_update(void);
void got_ask_draw(void);
// -1 while still open or if never opened; once closed, the 1-based index the player
// picked (option 1 = the first listed option) -- matches real select_option()'s own
// "ret=pos+1;" (1_panel.c), which every real SPEAK1 script's ASK-branching logic
// (e.g. "if a=1 then goto YES") is written against.
int got_ask_result(void);

// True if either box above is currently open -- got_main.c's render loop gates normal gameplay
// advancement and the pause menu on this, the same way it already gates on got_menu_is_open().
bool got_dialogue_any_open(void);

// Mirror Mode follow-up -- wootbeer, after playtesting the first round: "the mirror mode works as
// described, but the brief un-mirror is pretty noticeable, not really during pausing, but it
// happens during dialogue and other pop-ups too I assume. is it too hard to work around this?" See
// got_menu.h's own got_menu_get_box_rect() comment for the full rationale (same fix, same pattern,
// for these two boxes instead of the pause menu's). Each reports its own current outer box rect
// (screen pixel coordinates), valid only while that box is actually open -- returns false
// otherwise. got_main.c's render loop checks all four of these siblings
// (got_menu_get_box_rect()/got_item_menu_get_box_rect()/these two) every frame and passes whichever
// one is currently open to modex_set_mirror_exclude_rect() (modex.h) -- at most one can ever be
// open at once in practice, matching this port's established mutually-exclusive menu/dialogue/
// item-picker nesting.
bool got_dialogue_get_box_rect(int *x1, int *y1, int *x2, int *y2);
bool got_ask_get_box_rect(int *x1, int *y1, int *x2, int *y2);

#endif
