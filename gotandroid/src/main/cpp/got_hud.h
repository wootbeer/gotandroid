#ifndef GOT_HUD_H_
#define GOT_HUD_H_

// Minimal/alternate HUD overlay ("mode C" from the status-panel design notes) -- health/magic bars
// plus jewels/keys/score counts, drawn as a slim translucent screen-space GL strip near the top of
// the screen. See got_hud.c for the full rationale (why this is a deliberate departure from the
// real game's own fixed panel, and the custom digit renderer standing in for a real font system
// that doesn't exist yet).

// Sizes the HUD from the real screen dimensions -- called once, from gotMain(), same timing as
// got_controls_init().
void got_hud_init(int screen_w, int screen_h);

// Draws the HUD strip on top of whatever got_show_render_buffer() already drew this frame --
// called every frame, after modex_present_frame(), same timing as got_draw_touch_buttons(). Values
// are read fresh from got_main.c's own Thor state each call and passed in directly (no shared
// header/globals between the two files, same arrangement got_controls.c already uses for
// key_flag[]). health/magic are clamped to their real 0-150 range internally; jewels/keys/score
// are trusted as already-clamped (see got_thor_add_jewels()/got_thor_add_keys()/etc. in
// got_main.c, which already enforce their own real ranges). `active_item`/`active_object` are the
// raw real thor_info.item (0-7)/thor_info.object values -- drawn whenever active_item is nonzero
// (real display_item(): `if(thor_info.item){...}`, ANY active item, not just a carried quest item --
// see got_hud.c's own comment for why an earlier round had this wrong), same "swatch standing in for
// a real icon" treatment the jewel/key counts already use -- no icon-sprite rendering or font/text
// system exists yet to draw the real item name (see this file's own top comment). `active_object` is
// ignored unless `active_item==7`.
void got_draw_hud(int health, int magic, int jewels, int keys, int score, int active_item,
				   int active_object);

// Episode 1 boss round: a segmented boss health bar, drawn only while a boss fight is active (see
// got_main.c's own render-loop call site, gated on `s_boss_active`). Originally a wide horizontal bar
// top-center; now a vertical bar anchored near the right edge of the screen, matching real
// 1_panel.c's own boss_status() layout (fixed-pixel 10-segment vertical bar at the real screen's own
// right edge) as closely as this "mode C" overlay's screen-space GL strip can, reusing boss_status()'s
// own real proportions as fractions of the device screen instead of literal page-buffer pixels (see
// got_hud.c's own comment on this function). Moved off the top-center position for the Jormangund
// boss-fight overhaul (wootbeer: right-of-screen placement matters because the second boss is otherwise
// partly covered by it). Split into the same 10 segments real boss_status() uses so the fight still
// reads as "chunky" health rather than a smooth gradient. `health`/`max` are real ACTOR_NFO values
// (100/100 for the snake boss); a max<=0 draws nothing rather than dividing by zero.
void got_hud_draw_boss_health(int health, int max);

#endif
