#ifndef GOT_PANEL_H_
#define GOT_PANEL_H_

#include <stdbool.h>

// Real authentic status panel -- Display Modes "A" (Force 4:3, pillarboxed) and "B" (authentic
// panel, full-bleed stretch), both of which show the exact real 320x48 STATUS panel image, unlike
// Mode "C" (got_hud.c's own minimal translucent overlay, which reclaims this screen space instead
// of reserving it). See modex.h's own modex_set_display_mode()/MODEX_PANEL_PAGE comments for how
// the three modes actually differ at the presentation layer, and got_menu.h's own
// got_menu_display_mode() for how the player picks one -- this file only ever draws the panel's
// OWN content into MODEX_PANEL_PAGE; it doesn't decide which mode is active or how that page ends
// up on screen.
//
// A direct port of real 1_panel.c's own init_status_panel()/display_health()/display_magic()/
// display_jewels()/display_keys()/display_score()/display_item() -- adapted from that file's own
// "draw the background once at init, then redraw incrementally every time one stat changes" model
// (each real add_*() call re-invokes its own display_*()) to this port's established "redraw
// everything fresh, every frame, straight from current Thor state" convention instead (matching
// got_hud.c's own got_draw_hud(), see that file's header comment) -- there's no incremental-update
// hook to wire into every got_thor_add_jewels()/got_thor_add_keys()/damage/pickup call site this
// way, at the cost of redrawing ~15,360 background pixels a frame that mostly don't change, which
// a 2020s mobile GPU doesn't notice.

// Loads the real "STATUS" GOTRES.DAT resource (the panel's background art -- see got_panel.c's own
// comment for the byte-level format verification). Call once during startup resource loading,
// while the resource archive is open (got_main.c's got_load_real_resources(), same timing as
// got_font_init()/OBJECTS/PALETTE). Persistent for the app's lifetime once loaded, same policy as
// every other resource this port never frees. Not fatal if it fails: got_panel_draw() falls back
// to a flat dark backdrop instead of the real artwork, same tolerance OBJECTS/PALETTE/the font
// already get elsewhere in got_load_real_resources().
void got_panel_init(void);

// True once the real STATUS background loaded successfully.
bool got_panel_ready(void);

// Draws the full authentic panel (background + health/magic bars + jewels/keys/score digits +
// active-item icon) into MODEX_PANEL_PAGE for this frame -- call once per frame, BEFORE
// modex_present_frame() (this draws into the same low-res page-buffer system got_menu_draw()/
// got_dialogue_draw() etc. already draw into ahead of that same call, unlike got_draw_hud()/
// got_hud_draw_boss_health(), which are screen-space GL overlays drawn AFTER it) -- and only while
// the active display mode is A or B (see got_main.c's own render-loop call site; mirrors
// got_draw_hud()'s own "only while a game is running" gating). health/magic are clamped to their
// real 0-150 range internally, matching got_draw_hud()'s own doc comment; jewels/keys/score are
// trusted as already-clamped by got_main.c's own add-methods, same trust got_draw_hud() already
// extends them. `active_item`/`active_object` are the raw real thor_info.item (0-7)/thor_info.object
// values -- see got_panel.c's own display_item() comment for the two different real icon-index
// formulas this function picks between (item==7 vs. 1-6); `active_object` is ignored unless
// `active_item==7`. objects_buf is got_main.c's own persistent OBJECTS resource buffer (NULL if
// OBJECTS never loaded, in which case the item icon slot is left blank, same tolerance got_hud.c's
// own carried-item swatch already has for a zero/absent item). `hourglass_icon`, added for the
// Hourglass Enhancement (item value 8 -- see got_menu.h's own "Hourglass:" comment), is a separate
// 262-byte custom tile (wootbeer's own hand-supplied hourglass.png, converted the same way the Tombstone
// Enhancement's own art was -- see got_main.c's own s_custom_hourglass_tile comment) rather than
// anything from objects_buf, since there's no real OBJECTS index this belongs in; NULL leaves that
// icon slot blank too, same tolerance as objects_buf.
void got_panel_draw(int health, int magic, int jewels, int keys, int score, int active_item,
					 int active_object, const unsigned char *objects_buf,
					 const unsigned char *hourglass_icon);

#endif
