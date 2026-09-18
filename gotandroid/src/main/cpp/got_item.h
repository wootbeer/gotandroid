#ifndef GOT_ITEM_H_
#define GOT_ITEM_H_

#include <stdbool.h>

// Item picker -- a port of real select_item() (1_back.c), the screen that lets the player browse
// which of Thor's currently-carried magic items (or the single quest-item slot) is "active" --
// i.e. which one got_use_item()-equivalent code (got_main.c's own got_use_item(), see that
// function's own comment) will actually run when the Magic key is held afterward. Real
// select_item() is a blocking inner while(1) loop (1_panel.c-style, same shape as real
// option_menu()/select_option() that got_menu.c already adapts) drawing a horizontal row of item
// icons and moving a highlight LEFT/RIGHT across them; this port adapts it the same way got_menu.c
// adapts select_option() -- a per-frame state machine instead of a blocking loop, and a vertical
// text-list box (reusing got_menu.c's own box-drawing conventions: BOX_COLOR/TITLE_COLOR/ITEM_COLOR
// 215/54/14, the real BPICS 192-199 corner/edge tiles) instead of a real icon row, since the real
// OBJECTS icon-sprite resource isn't wired into any drawing code in this port yet (matching
// got_hud.c's own "swatch standing in for a real icon" scope note for the exact same resource gap).
//
// Opened/closed by KEY_SELECT (real key_select, this port's real SPACE scancode 57 -- see
// got_main.c's own KEY_SELECT #define) instead of real select_item()'s own caller (real 1_main.c:
// `if(key_flag[key_select]){ key_flag[key_select]=0; select_item(); }`, called unconditionally
// every frame exactly like this port's own got_item_menu_poll_toggle_key() below). wootbeer chose the
// RP6's Y button for Select (X for Magic) via this project's own button-mapping question.
//
// Real select_item()'s own guard against opening while a magic effect is actively running (real:
// `if(tornado_used||lightning_used||thunder_flag||hourglass_flag||thor->num_moves>1||shield_on||
// restore_screen) return;`) isn't ported -- none of those flags exist in this port yet (see
// got_main.c's own got_use_item() comment on which magic effects are/aren't built), so there's
// nothing yet for the picker to conflict with; a real guard can be added the same day any of those
// effects is.

// True whenever the item picker is open. got_main.c's per-frame loop checks this the same way it
// already checks got_menu_is_open() -- gates got_advance_game() (gameplay freezes while browsing,
// same as the real blocking select_item() loop halting everything else) and decides whether to
// call got_item_menu_update()/got_item_menu_draw() this frame.
bool got_item_menu_is_open(void);

// Mirror Mode follow-up -- see got_menu.h's own got_menu_get_box_rect() comment for the full
// rationale (same fix, same pattern, for the item picker's own box instead of the pause menu's).
// Reports the picker's current outer box rect (screen pixel coordinates), valid only while
// got_item_menu_is_open() is true -- returns false otherwise.
bool got_item_menu_get_box_rect(int *x1, int *y1, int *x2, int *y2);

// Call exactly once per frame, unconditionally, but only while the pause menu (got_menu.c) is
// NOT open -- got_main.c's own render loop already nests it that way, matching how got_menu.c's
// own ESC toggle takes priority. Edge-detects KEY_SELECT: a fresh press with the picker closed
// opens it (rebuilding the item list fresh from Thor's current inventory, and pre-highlighting
// whichever item is currently active, matching real select_item()'s own `sel=thor_info.item-1`
// starting-position convention); a fresh press while it's already open confirms the highlighted
// item and closes it, the same "Select is also a confirm key" real select_item() itself allows
// (real accepted confirm keys: select/fire/enter/space/magic -- this port's own confirm keys are
// SELECT here and FIRE inside got_item_menu_update() below, the two this port's control scheme
// already dedicates to confirm-shaped actions).
void got_item_menu_poll_toggle_key(void);

// Call every frame the picker is open (skip while closed, same calling convention as
// got_menu_update()) -- UP/DOWN cycle the highlighted item (wrapping; a deliberate deviation from
// real select_item()'s own key_left/key_right, matching this port's vertical list instead of the
// real horizontal icon row -- wootbeer: "up/down makes more sense to cycle the highlight instead of
// left/right"), FIRE confirms the highlight into Thor's active item and closes the picker, ESC
// cancels without changing anything.
void got_item_menu_update(void);

// Call every frame the picker is open, in place of the normal game draw (same calling convention
// as got_menu_draw() -- draws into the real low-res GOT_PAGE0 page buffer, restoring the room's
// clean GOT_PAGE2 backdrop underneath first, same reasoning as got_menu_draw()'s own comment on
// why that restore has to happen fresh every frame, not just once on close).
void got_item_menu_draw(void);

#endif
