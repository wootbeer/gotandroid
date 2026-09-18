// Item picker -- see got_item.h for the overall design and scope. Reuses got_menu.c's own generic
// list-box drawing conventions (border box + title + items + a ">" cursor) rather than sharing code
// with it directly, matching how got_menu.c/got_title.c are already each self-contained files with
// their own small hand-copied set of the same real constants (see got_menu.c's own comment on why
// those constants are redefined per-file instead of shared via a header).

#include "got_item.h"
#include "got_font.h"
#include "modex.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// Real GoT scancodes (1_define.h) -- see got_menu.c's own identical block for why these are
// redefined here rather than shared. UP/DOWN cycle the highlight (not LEFT/RIGHT, despite real
// select_item()'s own real icon ROW using key_left/key_right for this -- wootbeer: "up/down makes more
// sense to cycle the highlight instead of left/right", a deliberate port-specific deviation since
// this port draws a vertical list, not the real horizontal icon row).
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_FIRE 56
#define KEY_SELECT 57
#define KEY_ESC 1

// Matches got_main.c's own GOT_PAGE0/GOT_PAGE2 #defines exactly -- see got_menu.c's own identical
// block for what these are and why the item picker draws/restores the same way the pause menu does.
#define GOT_PAGE0 3840u
#define GOT_PAGE2 34720u

// Real color indices from select_option()'s own source (1_panel.c), reused verbatim -- see
// got_menu.c's own identical block. select_item() (1_back.c) draws its own box with these same
// three real indices too, so this isn't a borrowed convention, it's the same one the real source
// itself already uses for both screens.
#define BOX_COLOR 215
#define TITLE_COLOR 54
#define ITEM_COLOR 14

// Real GoT's own shared key_flag[100] (got_main.c) -- same extern-sharing pattern got_menu.c/
// got_controls.c already use for the same array.
extern volatile char key_flag[100];

// got_main.c's own real BPICS1 tile getter -- see got_menu.c's own identical extern for what this
// is and why it's exposed via a getter rather than a shared global.
extern const unsigned char *got_get_bpics(void);

// Real animated HAMPIC hammer-cursor sheet -- see got_menu.c's own identical extern for what this is
// and why it's used here too. wootbeer: "all menu cursors should be the spinning hammer like the main
// menu uses... this should be consistent everywhere."
extern const unsigned char *got_get_hampic(void);

// got_main.c's own new item-state accessors, added alongside this file for the item-picker
// milestone -- see got_main.c's own comments on each for exactly which real THOR_INFO field it
// bridges.
extern unsigned int got_thor_get_inventory(void);
extern int got_thor_get_active_item(void);
extern void got_thor_set_active_item(int item);
extern const char *got_thor_get_magic_item_name(int slot);
extern const char *got_thor_get_quest_object_name(void);

// got_menu.c's own "Hourglass:" Enhancements toggle -- see got_menu.h's own comment. Gates whether
// item value 8 ("Hourglass") is added to this picker's list at all; unlike items 1-6 (an
// inventory-bit check) or 7 (the quest-item-carried flag), this isn't a real pickup with any backing
// state of its own to check -- the Enhancements toggle itself IS the only gate.
extern bool got_menu_enhancement_hourglass_enabled(void);

// got_menu.c's own open/closed state -- checked here so the item picker never opens on top of the
// pause menu (got_main.c's own render loop already keeps the two mutually exclusive by nesting,
// but this is a second, cheap belt-and-suspenders check at the one place SELECT is actually read).
extern bool got_menu_is_open(void);

// got_main.c's own "redraw every sprite" pass -- non-static specifically so callers like this one
// can reach it, matching got_dialogue.c's own identical use in its restore_page() (real d_restore(),
// 1_back.c: ALWAYS redraws every actor immediately after restoring the clean background, every
// single call -- see got_draw_all_sprites()'s own comment). got_item_menu_draw() below was missing
// this half of that same real pattern -- see that function's own comment on the bug that left. wootbeer:
// "when you open select item menu, destructible bushes and wooden blocks, and thor, and signs
// disappear from view" -- all four are ordinary actors (this port's SpriteAnim/s_sprites, real
// FAKEBUSH/BLOCK1/THOR-LTH/SIGN-family ACTOR resources), drawn only by this same per-frame sprite
// pass, which got_advance_game() (frozen while the picker is open, see got_item_menu_is_open()'s own
// comment) is what normally runs.
extern void got_draw_all_sprites(void);

typedef enum {
	ITEM_MENU_CLOSED = 0,
	ITEM_MENU_OPEN,
} ItemMenuState;

static ItemMenuState s_state = ITEM_MENU_CLOSED;
static int s_selected = 0;

// Mirror Mode's own exclude-rect support -- see got_item_menu_draw()'s own comment on where these
// are set, and got_item_menu_get_box_rect() below for how they're read back. Stale/meaningless
// while s_state != ITEM_MENU_OPEN, exactly like every other per-frame-computed layout local in this
// file -- the getter itself guards that, never returning them otherwise.
static int s_item_box_x1, s_item_box_y1, s_item_box_x2, s_item_box_y2;

// Drives the hammer cursor's 4 real HAMPIC animation frames (got_item_menu_draw() below) -- this
// file's own local counter, same reasoning got_menu.c's own s_cursor_frame_counter comment gives
// (this picker and the pause menu/title screen are never on screen at the same time, so there's no
// need to share one counter across files); ticks once per got_item_menu_draw() call, same /7-per-
// frame speed got_title_draw_main_menu() already established.
static int s_cursor_frame_counter = 0;

static bool s_prev_select = false;
static bool s_prev_up = false;
static bool s_prev_down = false;
static bool s_prev_fire = false;
static bool s_prev_esc = false;

// The built list for however many items Thor is currently carrying (0-7: up to six magic-item
// slots plus the one quest-item slot) -- real item value (1-6 magic, 7 quest) alongside its display
// label, rebuilt fresh every time the picker opens (see rebuild_list() below) rather than kept
// continuously in sync, matching got_menu.c's own "derive display state each time, don't cache it"
// convention (that file's refresh_top_labels()/refresh_turbo_labels() comment).
#define ITEM_LIST_MAX 8 // 6 magic-item slots + the quest-item slot + the Hourglass Enhancement (see
						 // rebuild_list()'s own comment)
static int s_list_items[ITEM_LIST_MAX];
static char s_list_labels[ITEM_LIST_MAX][32];
static const char *s_list_item_ptrs[ITEM_LIST_MAX];
static int s_list_count;

// Real: `sel=thor_info.item-1;` (select_item()'s own starting-highlight line) generalized slightly
// -- real select_item() only ever has to find item-1 in a fixed 0-6 icon row (item IS the index
// there); this port's list is sparse (only carried items appear at all), so the currently-active
// real item value is searched for by VALUE among whichever rows rebuild_list() actually populated,
// falling back to row 0 if the active item (e.g. 0 == "none yet") isn't in the list at all.
static void rebuild_list(void) {
	unsigned int inv = got_thor_get_inventory();
	int active = got_thor_get_active_item();
	const char *quest_name;
	int i;

	s_list_count = 0;
	for (i = 0; i < 6; ++i) {
		if (inv & (1u << i)) {
			s_list_items[s_list_count] = i + 1;
			snprintf(s_list_labels[s_list_count], sizeof(s_list_labels[s_list_count]), "%s",
					 got_thor_get_magic_item_name(i));
			s_list_item_ptrs[s_list_count] = s_list_labels[s_list_count];
			++s_list_count;
		}
	}
	quest_name = got_thor_get_quest_object_name();
	if (quest_name && s_list_count < ITEM_LIST_MAX) {
		s_list_items[s_list_count] = 7;
		snprintf(s_list_labels[s_list_count], sizeof(s_list_labels[s_list_count]), "%s", quest_name);
		s_list_item_ptrs[s_list_count] = s_list_labels[s_list_count];
		++s_list_count;
	}

	// Item 8, "Hourglass" -- the Hourglass Enhancement (see got_menu.h's own "Hourglass:" comment).
	// Not a real inventory bit -- gated purely on the Enhancements toggle, always appended last so it
	// never displaces any of the six real items or the quest-item slot. Labeled "Freeze Time" (the
	// effect's own name) in an earlier round; wootbeer: "change 'freeze time' in the item menu to
	// 'hourglass'" -- matching the Enhancements-menu row's own name instead.
	if (got_menu_enhancement_hourglass_enabled() && s_list_count < ITEM_LIST_MAX) {
		s_list_items[s_list_count] = 8;
		snprintf(s_list_labels[s_list_count], sizeof(s_list_labels[s_list_count]), "Hourglass");
		s_list_item_ptrs[s_list_count] = s_list_labels[s_list_count];
		++s_list_count;
	}

	s_selected = 0;
	for (i = 0; i < s_list_count; ++i) {
		if (s_list_items[i] == active) {
			s_selected = i;
			break;
		}
	}
}

bool got_item_menu_is_open(void) {
	return s_state == ITEM_MENU_OPEN;
}

// Mirror Mode's own exclude-rect support -- see modex.h's own modex_set_mirror_exclude_rect()
// comment for the full design. Returns false (leaving the outputs untouched) whenever the picker
// isn't actually open, matching got_item_menu_is_open() just above.
bool got_item_menu_get_box_rect(int *x1, int *y1, int *x2, int *y2) {
	if (s_state != ITEM_MENU_OPEN) {
		return false;
	}
	if (x1) *x1 = s_item_box_x1;
	if (y1) *y1 = s_item_box_y1;
	if (x2) *x2 = s_item_box_x2;
	if (y2) *y2 = s_item_box_y2;
	return true;
}

// Closes the picker and restores the live game view -- shared by every path that can close the
// picker (confirm, cancel-via-ESC). wootbeer: "after closing the select item menu it stays 'drawn' on
// the screen until it is overwritten" -- root cause: this function didn't exist yet, so closing
// just flipped s_state back to CLOSED and relied on got_item_menu_draw() (which only runs while
// OPEN) to naturally stop drawing the box -- but nothing ever painted over the LAST frame's box
// still sitting in GOT_PAGE0, since the normal game draw path doesn't redraw the whole background
// every frame (only sprites erase/redraw). got_menu_close() (got_menu.c) already solved this exact
// problem for the pause menu with a one-time GOT_PAGE2->GOT_PAGE0 restore copy on close; this is
// the identical fix, one frame later, in this file.
static void got_item_menu_close(void) {
	s_state = ITEM_MENU_CLOSED;
	// Same key_flag[KEY_FIRE] bleed-through guard got_menu_close() already established -- a held
	// touch/gamepad FIRE that just confirmed a selection would otherwise read as "still held" the
	// instant gameplay resumes next frame and throw Thor's hammer for free (see that function's own
	// comment for the full explanation; this is the identical hazard, in a different file).
	key_flag[KEY_FIRE] = 0;
	xcopyd2d(0, 0, 320, 192, 0, 0, GOT_PAGE2, GOT_PAGE0, 320, 320);
}

// Confirms whatever row is currently highlighted into Thor's active item and closes the picker --
// shared by both of this file's own confirm paths (a second SELECT press, and FIRE inside
// got_item_menu_update()), matching real select_item()'s own multiple-confirm-keys behavior (real:
// `if(fire_pressed||enter_pressed||...) { thor_info.item=sel+1; break; }`, see got_item.h's own
// comment on which keys this port picked for that).
static void confirm_and_close(void) {
	if (s_list_count > 0) {
		got_thor_set_active_item(s_list_items[s_selected]);
	}
	got_item_menu_close();
}

void got_item_menu_poll_toggle_key(void) {
	bool select = key_flag[KEY_SELECT] != 0;

	if (select && !s_prev_select) {
		if (s_state == ITEM_MENU_CLOSED) {
			if (!got_menu_is_open()) {
				rebuild_list();
				if (s_list_count > 0) {
					s_state = ITEM_MENU_OPEN;
				}
				// Real select_item() with nothing carried just falls straight through and returns --
				// no box ever appears. Same here: an empty list leaves s_state closed, so pressing
				// SELECT with nothing in inventory yet is a correctly-scoped no-op, not a bug.
			}
		} else {
			confirm_and_close();
		}
		key_flag[KEY_SELECT] = 0; // real: `key_flag[key_select]=0;` right after being read, same
								   // convention got_menu.c's own KEY_ESC handling already follows
	}
	s_prev_select = select;

	if (s_state == ITEM_MENU_OPEN) {
		bool esc = key_flag[KEY_ESC] != 0;
		if (esc && !s_prev_esc) {
			got_item_menu_close(); // cancel -- Thor's active item is left exactly as it was
			key_flag[KEY_ESC] = 0;
		}
		s_prev_esc = esc;
	} else {
		s_prev_esc = key_flag[KEY_ESC] != 0; // keep tracking so a press held across open->close
											  // doesn't read as a fresh edge the next time it matters
	}
}

void got_item_menu_update(void) {
	bool up, down, fire;

	if (s_state != ITEM_MENU_OPEN) {
		return;
	}
	if (s_list_count == 0) {
		// Shouldn't happen (poll_toggle_key() above only opens with a nonempty list), but Thor could
		// in principle lose every carried item while the picker sits open in a future round that adds
		// item-dropping -- bail out the same safe way rather than divide-by-zero below.
		got_item_menu_close();
		return;
	}

	up = key_flag[KEY_UP] != 0;
	down = key_flag[KEY_DOWN] != 0;
	fire = key_flag[KEY_FIRE] != 0;

	if (up && !s_prev_up) {
		s_selected = (s_selected - 1 + s_list_count) % s_list_count;
	}
	if (down && !s_prev_down) {
		s_selected = (s_selected + 1) % s_list_count;
	}
	if (fire && !s_prev_fire) {
		confirm_and_close();
	}

	s_prev_up = up;
	s_prev_down = down;
	s_prev_fire = fire;
}

// Direct adaptation of got_menu.c's own got_menu_draw() box-sizing/border/text layout (see that
// function's own comment for the real select_option() math this both reuse) -- a single, always-
// fits-in-8-rows list (ITEM_LIST_MAX is 7), so unlike got_menu.c's own top-level menu this never
// needs to scroll.
void got_item_menu_draw(void) {
	const unsigned char *bpics;
	int w, h, x1, y1, x2, y2, s;
	int tw;
	int i;
	static const char *const title = "Select Item";

	if (s_state != ITEM_MENU_OPEN) {
		return;
	}
	++s_cursor_frame_counter;

	// Restore the room's clean background under the box every frame, before laying anything out --
	// same reasoning as got_menu_draw()'s own identical line (the box never grows/shrinks here since
	// the list is rebuilt once on open, not every frame, but this keeps the two files' drawing
	// behavior identical rather than depending on that difference). Immediately followed by a full
	// sprite redraw -- see got_draw_all_sprites()'s own extern comment above for the bug this fixes
	// (Thor, the hammer, every enemy, and actor-based scenery all vanishing while the picker is open)
	// and the real d_restore()-precedent this now matches.
	xcopyd2d(0, 0, 320, 192, 0, 0, GOT_PAGE2, GOT_PAGE0, 320, 320);
	got_draw_all_sprites();

	w = got_text_width(title) / 8;
	for (i = 0; i < s_list_count; ++i) {
		int iw = got_text_width(s_list_item_ptrs[i]) / 8;
		if (iw > w) {
			w = iw;
		}
	}
	if (w & 1) {
		++w;
	}
	w = w * 8 + 32;
	s = w / 16;
	h = s_list_count * 16 + 32;
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
	s_item_box_x1 = x1 - 16;
	s_item_box_y1 = y1 - 16;
	s_item_box_x2 = x2 + 16;
	s_item_box_y2 = y2 + 16;

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
		for (i = 0; i < s_list_count + 2; ++i) {
			xfput(x1 - 16, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 198L * 262));
			xfput(x2, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 199L * 262));
		}
	}

	tw = got_text_width(title);
	got_xprint((320 - tw) / 2, y1 + 4, title, GOT_PAGE0, TITLE_COLOR);

	// Cursor: the real animated HAMPIC hammer, not a plain ">" glyph -- see got_menu.c's own
	// identical comment for why (wootbeer: "all menu cursors should be the spinning hammer... this
	// should be consistent everywhere").
	for (i = 0; i < s_list_count; ++i) {
		int iy = (y1 + 28) + i * 16;
		got_xprint(x1 + 32, iy, s_list_item_ptrs[i], GOT_PAGE0, ITEM_COLOR);
		if (i == s_selected) {
			const unsigned char *hampic = got_get_hampic();
			if (hampic) {
				int frame = (s_cursor_frame_counter / 7) % 4;
				xfput(x1 + 8, iy - 3, GOT_PAGE0, (char *) (hampic + frame * 262));
			} else {
				got_xprint(x1 + 8, iy, ">", GOT_PAGE0, ITEM_COLOR);
			}
		}
	}
}
