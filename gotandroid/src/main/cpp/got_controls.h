#ifndef GOT_CONTROLS_H_
#define GOT_CONTROLS_H_

#include <stdbool.h>

// On-screen touch D-pad + gamepad auto-detect for GoT -- see got_controls.c for the full
// rationale (Descore's own controls.c pattern, adapted for GoT's much simpler input needs and
// its very different fixed-low-res-buffer-plus-GL-quad rendering, which is why these buttons are
// drawn as a separate screen-space overlay rather than into the modex.h page buffer). The JNI
// entry points Java calls (touchHandler(), setGamepadConnected()) live in got_controls.c itself,
// not here -- this header is only what got_main.c/got_menu.c need to call into got_controls.c.

// Sizes and positions the on-screen D-pad's hit-test rectangles from the real screen dimensions
// (the same w/h gotMain() itself receives from Java) -- called once, from gotMain(), before the
// render loop starts.
void got_controls_init(int screen_w, int screen_h);

// Draws the on-screen D-pad (translucent rectangles, screen-pixel space) on top of whatever
// got_show_render_buffer() already drew this frame -- called every frame, after
// modex_present_frame(), before eglSwapBuffers(). No-ops (draws nothing) while a real gamepad is
// connected -- see got_controls.c's Gamepad_connected.
void got_draw_touch_buttons(void);

// New for the "Touch Options" pause-menu submenu (got_menu.c) -- wootbeer: "only make this entry
// appear though if touch controls are enabled, since it doesn't have a use if the user is using a
// controller." True once got_controls_init() has run and no gamepad is currently connected --
// see got_controls.c's own gamepad_connected comment for why a connected gamepad hides/disables
// the on-screen D-pad entirely (and so has no use for a scaling/opacity option either).
bool got_controls_touch_enabled(void);

// Selects one of got_controls.c's own Touch_scale_values[] table entries (0-based index, see that
// table's own comment for the exact range and why it's asymmetric) and immediately re-lays-out
// every on-screen button at the new size -- called from got_menu_set_touch_scale_step()
// (got_menu.c) every time the new "Scaling" slider actually changes, and once at startup from
// got_config_init() (got_main.c) to restore whatever was last saved to CONFIG.DAT.
void got_controls_set_scale_step(int step);

// Pure lookup into the same Touch_scale_values[] table got_controls_set_scale_step() indexes --
// got_menu.c's own format_scale_label() uses this to show the actual multiplier text (e.g.
// "1.00x") next to the Scaling slider's bar, not just the raw step number.
float got_controls_touch_scale_value(int step);

// Sets the translucent alpha got_draw_touch_buttons() renders every button with -- called from
// got_menu_set_touch_opacity_step() (got_menu.c) every time the new "Opacity" slider changes, and
// once at startup from got_config_init(). Clamped internally to the same
// [previous-default, fully-opaque] floor got_menu.c's own TOUCH_OPACITY_MIN_ALPHA already enforces
// on the step value before converting it to a float here -- this is a second, defensive clamp, not
// the only one.
void got_controls_set_opacity(float alpha);

// New for the "Remap Gamepad" pause-menu submenu (got_menu.c) -- see got_controls.c's own "Gamepad
// remapping" section comment for the full design. True whenever a real gamepad is currently
// connected -- the same live gamepad_connected flag got_controls_touch_enabled() already exposes
// the inverse of, just under its own name so got_menu.c's TOP_TOUCH_OPTIONS row (which now shows
// EITHER "Touch Options" OR "Remap Gamepad" depending on this) doesn't have to read
// !got_controls_touch_enabled() and reason through the double negative.
bool got_controls_gamepad_connected(void);

// Actions this port lets a gamepad button be remapped to -- Fire (0), Magic/"Use Item" (1), Select
// Item (2), matching got_controls.c's own kGamepadRemapActions[] order exactly (hand-synced, same
// policy this whole project already uses for every other cross-file constant -- see
// TOUCH_SCALE_NUM_STEPS's own comment for the precedent). D-pad/Start/Select-for-delete-save stay
// hardcoded in GotView.java and are never part of this table.
//
// got_gamepad_remap_bound_keycode()/got_gamepad_remap_default_keycode() return GAMEPAD_UNBOUND
// (-1, got_controls.c's own #define -- not exposed here since got_menu.c defines its own hand-
// synced copy, same policy) for an out-of-range action index, and can themselves return that same
// sentinel for a legitimately-unbound action (only reachable via the remap screen's own "steal"
// logic -- see got_menu.c's own capture-handling comment).
int got_gamepad_remap_bound_keycode(int action);

// Commits a new LIVE binding for one action -- called only from got_menu.c's own Apply step (once
// per action, after the whole batch of staged changes is confirmed at once) and from got_main.c's
// own got_config_init() startup load. Never called while the remap screen's capture step is still
// in progress -- see got_gamepad_remap_poll_capture() below for that flow.
void got_gamepad_remap_set_bound_keycode(int action, int keycode);

// Compiled-in default physical button for one action -- wootbeer: "the default controls will be what
// the default controls are of my retroid pocket 6 right now." Used both by got_menu.c's own Reset
// row and by got_controls.c's own static live-table initializer (so a fresh install/process is
// already correct before any config load).
int got_gamepad_remap_default_keycode(int action);

// Short label for one action's row ("Fire", "Magic", "Select Item") -- got_menu.c's own remap
// screen uses this to build each row's full text ("Fire: A", etc.).
const char *got_gamepad_remap_action_label(int action);

// Short display name for a capturable keycode ("A", "B", "X", "Y", "L1", "R1", "L3"), or "---" for
// GAMEPAD_UNBOUND or anything else not in the capturable pool -- got_menu.c's own row labels and
// capture-prompt title both use this.
const char *got_gamepad_remap_button_name(int keycode);

// Starts/cancels the remap screen's "press a button for this action" capture step -- see
// got_gamepad_remap_poll_capture() below for how got_menu.c actually reads the result. Cancelling
// (ESC/Start, or backing out of the capture screen some other way) discards whatever was about to
// be captured without changing any staged or live binding.
void got_gamepad_remap_start_capture(void);
void got_gamepad_remap_cancel_capture(void);

// Read-and-clear poll for got_menu.c's own per-frame MENU_GAMEPAD_CAPTURE handling in
// got_menu_update() -- returns GAMEPAD_UNBOUND until a capturable button has actually been pressed
// since got_gamepad_remap_start_capture(), then that keycode exactly once. got_menu.c is
// responsible for its own staging-array "steal" logic (clearing any OTHER action currently bound
// to the same captured keycode) once it gets a real result back from this.
int got_gamepad_remap_poll_capture(void);

#endif
