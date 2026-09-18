// Real on-screen touch D-pad for GoT, plus the gamepad-connected flag that hides it -- Descore's
// own controls.c (Descore-Mobile-master/Descore/src/main/cpp/controls.c) is the pattern this
// follows: a fixed table of hit-test rectangles, each mapped to 1-2 scancodes, with
// activateOnHover semantics so dragging a thumb from one button straight into an adjacent one
// presses/releases keys smoothly instead of needing a lift-and-retap. Adapted, not copied
// verbatim, for two real differences: GoT's input needs are far simpler than Descore's 21-button
// flight-sim scheme (this is just 8-directional movement -- key_up/down/left/right, see
// got_main.c's KEY_UP etc. -- plus FIRE, PAUSE (added for the pause/options-menu milestone), MAGIC
// (added for the inventory milestone -- "use held item", see got_main.c's own KEY_MAGIC edge-
// trigger in got_move_thor()), and now SELECT (opens got_main.c's new item-picker screen,
// got_item_menu_poll_toggle_key()) -- the full real action-button set is wired here now; and
// GoT's game canvas is a fixed 320x192 buffer stretched to the real screen by one GL quad
// (modexgl.c), not rendered at native screen resolution the way Descore's own canvas is -- these
// buttons are drawn as a *separate* screen-space GL overlay on top of that quad (see
// got_draw_touch_buttons()), not into the low-res page buffer, where they'd be a handful of
// blurry upscaled pixels.
//
// Button geometry is sized as a fraction of the real screen dimensions gotMain() already receives
// from Java (cell = 0.11 * min(screen_w,screen_h) * scale, see layout_buttons() below) -- that
// fraction-of-screen shape means the RESULT is already resolution-independent without any density
// conversion (a fraction of screen px stays the same fraction of screen regardless of dp/px ratio,
// since it never mixes an absolute size with a screen dimension the way Descore's OLD, pre-fix
// formula did). Where density DOES matter is the floor underneath it -- see
// TOUCH_MIN_CELL_DP/layout_buttons() below -- and the new user-adjustable Scaling slider (wootbeer,
// after seeing Descore's own port of this same feature: "we had also added a menu option with an
// adjustment to touch control scaling... can we add a new sub-menu to the pause menu as well?
// ...it can have an option called 'scaling'... this slider will be used to adjust the scaling",
// plus an "opacity" slider, see got_menu.c's own "Touch Options" submenu) -- both of which now do
// go through the same got_px_to_dp()/got_dp_to_px() (got_main.c) Java round-trip Descore's own
// pt_conv()/px_to_dp()/dp_to_px() use, so a device's actual display density is available wherever
// this file actually needs a physical (not just a screen-relative) size.

#include "got_controls.h"

#include <GLES/gl.h>
#include <android/log.h>
#include <jni.h>
#include <stdbool.h>

#define LOG_TAG "GotControls"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Real key_flag[100] (got_main.c) -- shared the same way the actual DOS source shares it across
// files (1_movpat.c/1_main.c/etc. all `extern` it too), not a GoT-Android-specific pattern.
extern volatile char key_flag[100];

// got_main.c's own JNI call-outs to GotView.java's new dpToPx(F)F/pxToDp(F)F -- see either
// function's own got_main.c comment. New for the Touch Options submenu's own Scaling slider (see
// this file's own top-of-file comment and layout_buttons()/TOUCH_MIN_CELL_DP below) -- same
// extern-declare-it-here-not-in-a-shared-header pattern this file already uses for
// got_sound_set_volume()-style call-outs elsewhere in this project (see got_menu.c's own identical
// extern declarations for those).
extern float got_px_to_dp(float px);
extern float got_dp_to_px(float dp);

// got_main.c's own demo-playback state (wootbeer: "how hard would it be to add the demo feature from the
// main menu of the actual game?") -- see s_demo_data's own comment there for the full design. Real
// keyboard_int()'s own `else if(demo) exit_flag=5;` aborts a running demo on ANY live key event
// instead of applying it to key_flag[] -- touchHandler() below is this touchscreen port's equivalent
// entry point for that same behavior (got_main.c's own keyHandler() covers the gamepad path).
extern bool got_demo_is_active(void);
extern void got_demo_request_abort(void);

// Real GoT scancodes (1_define.h's UP/DOWN/LEFT/RIGHT/ALT, already used as got_main.c's
// KEY_UP/KEY_DOWN/KEY_LEFT/KEY_RIGHT/KEY_FIRE) -- redefined here rather than shared via a header
// since got_main.c's copies are its own #defines, not exported; kept numerically in sync by
// hand-audit, same as the struct-layout constants this whole project already tracks manually
// against the real DOS source.
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_LEFT 75
#define KEY_RIGHT 77
#define KEY_FIRE 56 // real ALT scancode, 1_init.c's default key_fire binding -- now wired, see BTN_FIRE
#define KEY_MAGIC 29 // real CTRL scancode, 1_init.c's default key_magic binding -- see got_main.c's
					 // own KEY_MAGIC #define (kept numerically in sync by hand-audit, same as
					 // every other constant this file already redefines rather than shares) --
					 // now wired, see BTN_MAGIC
#define KEY_SELECT 57 // real SPACE scancode, 1_init.c's default key_select binding -- see
					  // got_main.c's own KEY_SELECT #define, same hand-audited numeric-sync policy
					  // -- now wired, see BTN_SELECT
#define KEY_ESC 1 // real l_define.h ESC scancode -- pause/options menu, see BTN_PAUSE and got_menu.c

#define NUM_BUTTONS 12
#define MAX_KEYS 2

#define BTN_UP 0
#define BTN_DOWN 1
#define BTN_LEFT 2
#define BTN_RIGHT 3
#define BTN_UP_LEFT 4
#define BTN_UP_RIGHT 5
#define BTN_DOWN_LEFT 6
#define BTN_DOWN_RIGHT 7
#define BTN_FIRE 8 // Thor's hammer throw -- see got_controls_init()'s own layout comment
#define BTN_PAUSE 9 // opens/closes got_menu.c's pause/options menu -- see got_controls_init()
#define BTN_MAGIC 10 // "use held item" -- see got_move_thor()'s own KEY_MAGIC edge-trigger, added
					 // for the inventory milestone alongside got_controls_init()'s own layout comment
#define BTN_SELECT 11 // "select item" -- opens got_main.c's new item-picker screen, see
					  // got_item_menu_poll_toggle_key()'s own KEY_SELECT edge-trigger

// Android MotionEvent action constants (avoids pulling in android/input.h just for these 5
// values) -- same subset DescoreActivity/DescoreView's own controls.c hardcodes the same way.
#define ACTION_DOWN 0
#define ACTION_UP 1
#define ACTION_MOVE 2
#define ACTION_POINTER_DOWN 5
#define ACTION_POINTER_UP 6

#define MAX_TRACKED_POINTERS 16

typedef struct {
	float x, y, w, h; // screen pixel space, see got_controls_init()
	unsigned char keys[MAX_KEYS];
	int num_keys;
} TouchButton;

static TouchButton buttons[NUM_BUTTONS];
static int touch_buttons[MAX_TRACKED_POINTERS]; // -1 = this pointer isn't over a button
static bool controls_initialized = false;
static int screen_w = 0, screen_h = 0;

// Set from Java whenever a gamepad-class InputDevice connects/disconnects (GotView.java's
// InputManager.InputDeviceListener, mirroring DescoreView.java's own). While true, the on-screen
// D-pad is neither drawn nor hit-tested -- same behavior Descore's Gamepad_connected gives it.
static bool gamepad_connected = false;

// New "Touch Options" submenu state (got_menu.c) -- wootbeer: "we had also added a menu option with an
// adjustment to touch control scaling [in Descore]... so can we add a new sub-menu to the pause
// menu as well?... it can have an option called 'scaling'... this slider will be used to adjust
// the scaling." Same 9-entry table and asymmetric range Descore's own controls.c already ships
// (Touch_scale_values[9]) -- Descore's own comment on it: "touch targets are already comfortable
// at the default, and most players adjusting this at all will want them bigger, not smaller," so
// only 2 steps go down from 1.00x but 6 go up, to 1.80x. Values themselves (not just the shape)
// ported verbatim -- no reason to retune numbers wootbeer's own Descore project already settled on.
#define TOUCH_SCALE_NUM_STEPS 9
#define TOUCH_SCALE_DEFAULT_STEP 2 // index of 1.00x below -- today's existing, pre-this-feature
									// button size exactly, so a fresh install (or an existing
									// CONFIG.DAT with no saved scale yet) looks identical to before
									// this round shipped. Must stay numerically in sync with
									// got_menu.c's own TOUCH_SCALE_DEFAULT_STEP (hand-audited, same
									// policy this whole project already uses for every other
									// cross-file constant).
static const float kTouchScaleValues[TOUCH_SCALE_NUM_STEPS] = {
	0.90f, 0.95f, 1.00f, 1.10f, 1.20f, 1.35f, 1.50f, 1.65f, 1.80f,
};
static int s_scale_step = TOUCH_SCALE_DEFAULT_STEP;

// Minimum absolute cell size, in dp -- not tied to screen resolution at all, unlike `cell` itself
// (see layout_buttons() below), so it stays a genuinely comfortable physical touch-target size on
// any device regardless of screen density, aspect ratio, or how far down the new Scaling slider is
// pulled. 44dp matches the common (iOS Human Interface Guidelines/Android accessibility) minimum
// comfortable touch-target recommendation. GoT's fraction-of-screen `cell` formula was already
// resolution-independent as a SCREEN FRACTION before this (unlike Descore's own pre-fix formula,
// which sized buttons as an absolute px count divided by an outdated fixed density assumption --
// see DescoreActivity.java's own buttonSizeBias comment for that history) -- what it never had was
// an absolute floor, so an unusually small/dense screen combined with a low Scaling setting could
// in principle shrink a button below a comfortable physical size with nothing to stop it. This is
// that stop.
#define TOUCH_MIN_CELL_DP 44.0f

// New Opacity slider, same submenu -- see got_controls_set_opacity() below for how got_menu.c
// drives this, and this file's own got_draw_touch_buttons() for where it's actually used. Starts
// at TOUCH_OPACITY_DEFAULT (today's existing, pre-this-feature alpha, see that #define's own
// comment) so an existing install looks identical to before this round shipped until the user
// actually raises the new slider -- matching TOUCH_SCALE_DEFAULT_STEP's own reasoning just above.
#define TOUCH_OPACITY_DEFAULT 0.30f
static float s_touch_opacity = TOUCH_OPACITY_DEFAULT;

// --- Gamepad remapping ("Remap Gamepad" pause-menu submenu, got_menu.c) ----------------------
//
// Ported from Descore's own gamepad_remap.c (Descore-Android), scoped way down: Descore remaps 14
// gameplay actions across its full button set; GoT only has 3 real action buttons to begin with
// (Fire/Magic/Select Item, see BTN_FIRE/BTN_MAGIC/BTN_SELECT above), so this is the same
// architecture -- a keyCode<->action table, a raw JNI forwarding channel decoupled from any
// pre-translation, a cross-thread capture flag pair -- at 1/4 the scale, not a "smaller version of
// a bigger system." D-pad, Start (pause), and Select (delete-save-confirm) stay permanently
// hardcoded in GotView.java's own gotScancode(), exactly like Descore's own D-pad/Start/Select
// exclusion (gamepad_remap.c's own top comment) -- this table only ever covers the 3 rows below.
//
// Mirrors android.view.KeyEvent.KEYCODE_BUTTON_* -- stable public Android API ints, same constants
// Descore's own gamepad_remap.c already mirrors (down to the THUMBR wootbeer later dropped from that
// project's own capturable pool -- this table matches Descore's CURRENT 7-button pool, not its
// original 8-button one).
#define GP_BUTTON_A       96
#define GP_BUTTON_B       97
#define GP_BUTTON_X       99
#define GP_BUTTON_Y       100
#define GP_BUTTON_L1      102
#define GP_BUTTON_R1      103
#define GP_BUTTON_THUMBL  106

#define GAMEPAD_NUM_ACTIONS 3
#define GAMEPAD_UNBOUND (-1)

typedef struct {
	const char *label;
	unsigned char scancode;   // Real GoT scancode this action always triggers -- never remapped
							   // itself, only which physical button drives it changes.
	int defaultKeyCode;       // Compiled-in default physical button -- wootbeer: "the default controls
							   // will be what the default controls are of my retroid pocket 6 right
							   // now," i.e. exactly what gotScancode() hardcoded before this round
							   // (A/B->Fire, X->Magic, Y->Select). Only one physical button per
							   // action fits this simple table, so A (not B) is the default for
							   // Fire -- B stays in the capturable pool for any of the three actions
							   // but is unbound by default; today BOTH A and B throw the hammer, and
							   // after this round only A does until B is deliberately rebound --
							   // flagged to wootbeer explicitly in this round's own notes writeup.
} GamepadRemapAction;

// Real GoT scancodes already #defined above (KEY_FIRE/KEY_MAGIC/KEY_SELECT) -- reused directly,
// not redefined a second time.
static const GamepadRemapAction kGamepadRemapActions[GAMEPAD_NUM_ACTIONS] = {
	{ "Fire",        KEY_FIRE,   GP_BUTTON_A },
	{ "Magic",       KEY_MAGIC,  GP_BUTTON_X },
	{ "Select Item", KEY_SELECT, GP_BUTTON_Y },
};

// Live, persisted bindings -- consulted every time a gamepad button event arrives via
// gamepadButtonRaw() below. Statically seeded to the defaults so a freshly-launched process
// (before got_config_init() ever runs) is already correct -- same "correct before any load"
// precedent kTouchScaleValues/s_scale_step's own static initializer already establishes above.
static int s_gamepad_bound_keycode[GAMEPAD_NUM_ACTIONS] = {
	GP_BUTTON_A, GP_BUTTON_X, GP_BUTTON_Y,
};

// Cross-thread state for the remap screen's capture step -- gamepadButtonRaw() below lands on
// whatever thread Android delivers key events on; got_menu.c's own per-frame poll
// (got_gamepad_remap_poll_capture()) runs on the native game thread. Same informal single-writer/
// single-reader volatile pattern this project already relies on for key_flag[]/Want_pause
// (got_main.c) -- no mutex, matching that established precedent.
static volatile bool s_gamepad_capturing = false;
static volatile int s_gamepad_captured_keycode = GAMEPAD_UNBOUND;

bool got_controls_gamepad_connected(void) {
	return gamepad_connected;
}

int got_gamepad_remap_bound_keycode(int action) {
	if (action < 0 || action >= GAMEPAD_NUM_ACTIONS) {
		return GAMEPAD_UNBOUND;
	}
	return s_gamepad_bound_keycode[action];
}

// Sets the LIVE binding directly -- called only from got_menu.c's own Apply step (after the user
// confirms a whole batch of staged changes at once, see that submenu's own comment on why this
// port stages rather than applies immediately like every other submenu here) and from got_main.c's
// own startup config load. Never called mid-capture; the capture flow's own in-progress choices
// live entirely in got_menu.c's staging array until Apply actually calls this.
void got_gamepad_remap_set_bound_keycode(int action, int keycode) {
	if (action < 0 || action >= GAMEPAD_NUM_ACTIONS) {
		return;
	}
	s_gamepad_bound_keycode[action] = keycode;
}

int got_gamepad_remap_default_keycode(int action) {
	if (action < 0 || action >= GAMEPAD_NUM_ACTIONS) {
		return GAMEPAD_UNBOUND;
	}
	return kGamepadRemapActions[action].defaultKeyCode;
}

const char *got_gamepad_remap_action_label(int action) {
	if (action < 0 || action >= GAMEPAD_NUM_ACTIONS) {
		return "";
	}
	return kGamepadRemapActions[action].label;
}

// Short display name for a capturable keycode (or GAMEPAD_UNBOUND) -- got_menu.c's own row labels
// ("Fire: A", etc.) and capture-prompt title use this, same shape Descore's own
// gamepad_remap_bound_name() already establishes.
const char *got_gamepad_remap_button_name(int keycode) {
	switch (keycode) {
		case GP_BUTTON_A:      return "A";
		case GP_BUTTON_B:      return "B";
		case GP_BUTTON_X:      return "X";
		case GP_BUTTON_Y:      return "Y";
		case GP_BUTTON_L1:     return "L1";
		case GP_BUTTON_R1:     return "R1";
		case GP_BUTTON_THUMBL: return "L3";
		default:               return "---";
	}
}

void got_gamepad_remap_start_capture(void) {
	s_gamepad_captured_keycode = GAMEPAD_UNBOUND;
	s_gamepad_capturing = true;
}

void got_gamepad_remap_cancel_capture(void) {
	s_gamepad_capturing = false;
	s_gamepad_captured_keycode = GAMEPAD_UNBOUND;
}

// Read-and-clear, called once per frame by got_menu.c's own MENU_GAMEPAD_CAPTURE handling in
// got_menu_update() -- returns GAMEPAD_UNBOUND until a capturable button has actually been pressed
// since got_gamepad_remap_start_capture(), then that keycode exactly once.
int got_gamepad_remap_poll_capture(void) {
	int captured = s_gamepad_captured_keycode;
	s_gamepad_captured_keycode = GAMEPAD_UNBOUND;
	return captured;
}

// Raw Android keyCode forwarding for the 7 capturable gamepad buttons -- GotView.java calls this
// (never gotScancode()'s own translated path) for BUTTON_A/B/X/Y/L1/R1/THUMBL specifically, so
// native code can tell "physical button A" apart from whatever scancode it currently happens to be
// bound to (see gamepad_remap.c's own identical architecture note in Descore -- ported verbatim in
// spirit, not literally, since this port has no key_handler()-style single dispatch point the way
// Descore's key.h does; key_flag[] is written directly here instead).
//
// While capturing (the Remap Gamepad screen's own "press a button for this action" step), every
// press just records itself for got_gamepad_remap_poll_capture() to pick up next frame -- including
// whichever button is CURRENTLY bound to the action being rebound, so re-confirming the same button
// works exactly like picking a new one. Outside capture, a press/release is dispatched straight to
// whichever action (if any) currently claims that keycode, exactly like the live remap table its
// name implies.
JNIEXPORT void JNICALL
Java_wootbeer_gotandroid_GotView_gamepadButtonRaw(JNIEnv *env, jclass type, jint key_code, jboolean down) {
	int i;
	(void) env;
	(void) type;

	if (s_gamepad_capturing) {
		if (down) {
			s_gamepad_captured_keycode = (int) key_code;
		}
		return;
	}

	for (i = 0; i < GAMEPAD_NUM_ACTIONS; i++) {
		if (s_gamepad_bound_keycode[i] == (int) key_code) {
			key_flag[kGamepadRemapActions[i].scancode] = down ? 1 : 0;
		}
	}
}

// Lays out the 8 D-pad buttons as a 3x3 grid (corners = diagonals, edges = cardinals, center
// empty) in the bottom-left corner of the real screen, plus one FIRE button (Thor's hammer throw)
// as a single square in the bottom-right corner, mirroring the D-pad's size and bottom margin so
// the two controls read as a matched pair. Each cell/button is sized as a fraction of the shorter
// screen dimension (times the user's own Scaling slider, see TOUCH_SCALE_NUM_STEPS's own comment)
// so both stay a sensible size on both phone and tablet aspect ratios, floored at TOUCH_MIN_CELL_DP
// so a low Scaling setting can never shrink them below a comfortable physical size either. A small
// gap is inset within each cell purely so adjacent buttons read as visually distinct on screen --
// hit-testing uses these same (post-inset) rects, not the full cell, which is a simple, deliberate
// first pass (see the comment on this file), not a claim that this is the final layout -- in
// particular FIRE's bottom-right placement is a starting guess, not verified against how the
// hammer throw actually feels to trigger one-handed on a Retroid Pocket 6 yet.
//
// Split out of got_controls_init() so got_controls_set_scale_step() below can re-run it any time
// the Scaling slider moves without needing the real screen dimensions passed back in -- mirroring
// Descore's own layout_buttons(), which "recomputes ALL button rects from cached logical dp screen
// size + current scale every time either changes." Reads screen_w/screen_h (cached by
// got_controls_init() below) rather than taking them as parameters for exactly that reason.
static void layout_buttons(void) {
	float cell, gap, origin_x, origin_y, min_cell;
	int row, col, i;
	int w = screen_w, h = screen_h;

	cell = 0.11f * (float) (w < h ? w : h) * kTouchScaleValues[s_scale_step];
	min_cell = got_dp_to_px(TOUCH_MIN_CELL_DP);
	if (cell < min_cell) {
		cell = min_cell; // see TOUCH_MIN_CELL_DP's own comment -- an absolute floor, not a fraction
	}
	gap = cell * 0.08f;
	origin_x = cell * 0.6f; // left margin
	origin_y = (float) h - cell * 3.6f; // bottom margin

	for (row = 0; row < 3; ++row) {
		for (col = 0; col < 3; ++col) {
			int idx = -1;
			unsigned char k0 = 0, k1 = 0;
			int nk = 0;

			if (row == 0 && col == 0) { idx = BTN_UP_LEFT;    k0 = KEY_UP;   k1 = KEY_LEFT;  nk = 2; }
			else if (row == 0 && col == 1) { idx = BTN_UP;    k0 = KEY_UP;                   nk = 1; }
			else if (row == 0 && col == 2) { idx = BTN_UP_RIGHT; k0 = KEY_UP;  k1 = KEY_RIGHT; nk = 2; }
			else if (row == 1 && col == 0) { idx = BTN_LEFT;  k0 = KEY_LEFT;                  nk = 1; }
			else if (row == 1 && col == 2) { idx = BTN_RIGHT; k0 = KEY_RIGHT;                 nk = 1; }
			else if (row == 2 && col == 0) { idx = BTN_DOWN_LEFT; k0 = KEY_DOWN; k1 = KEY_LEFT; nk = 2; }
			else if (row == 2 && col == 1) { idx = BTN_DOWN;  k0 = KEY_DOWN;                  nk = 1; }
			else if (row == 2 && col == 2) { idx = BTN_DOWN_RIGHT; k0 = KEY_DOWN; k1 = KEY_RIGHT; nk = 2; }
			else { continue; } // center cell -- dead zone, no button

			buttons[idx].x = origin_x + (float) col * cell + gap * 0.5f;
			buttons[idx].y = origin_y + (float) row * cell + gap * 0.5f;
			buttons[idx].w = cell - gap;
			buttons[idx].h = cell - gap;
			buttons[idx].keys[0] = k0;
			if (nk > 1) buttons[idx].keys[1] = k1;
			buttons[idx].num_keys = nk;
		}
	}

	// FIRE: one cell, bottom-right corner, same size and bottom margin as the D-pad's own cells --
	// a mirror-image placement rather than a hand-tuned one.
	buttons[BTN_FIRE].x = (float) w - cell * 1.6f - (cell - gap);
	buttons[BTN_FIRE].y = origin_y + cell * 2.0f + gap * 0.5f;
	buttons[BTN_FIRE].w = cell - gap;
	buttons[BTN_FIRE].h = cell - gap;
	buttons[BTN_FIRE].keys[0] = KEY_FIRE;
	buttons[BTN_FIRE].num_keys = 1;

	// MAGIC ("use held item", added for the inventory milestone): one more cell directly above
	// FIRE, same size and column -- a vertical pair in the bottom-right corner mirroring the D-pad's
	// own bottom-left cluster, so the two action buttons read as a matched pair the same way FIRE
	// and PAUSE already do horizontally. Not real 1_init.c's own default key_magic binding's layout
	// (the original has no on-screen buttons at all) -- a starting guess, same caveat this file's
	// own top comment already makes about FIRE's placement.
	buttons[BTN_MAGIC].x = buttons[BTN_FIRE].x;
	buttons[BTN_MAGIC].y = buttons[BTN_FIRE].y - cell;
	buttons[BTN_MAGIC].w = cell - gap;
	buttons[BTN_MAGIC].h = cell - gap;
	buttons[BTN_MAGIC].keys[0] = KEY_MAGIC;
	buttons[BTN_MAGIC].num_keys = 1;

	// SELECT ("select item", added for the item-picker milestone): one more cell directly above
	// MAGIC, same column -- extends the FIRE/MAGIC vertical pair in the bottom-right corner into a
	// 3-cell stack, same reasoning as MAGIC's own placement comment just above.
	buttons[BTN_SELECT].x = buttons[BTN_FIRE].x;
	buttons[BTN_SELECT].y = buttons[BTN_MAGIC].y - cell;
	buttons[BTN_SELECT].w = cell - gap;
	buttons[BTN_SELECT].h = cell - gap;
	buttons[BTN_SELECT].keys[0] = KEY_SELECT;
	buttons[BTN_SELECT].num_keys = 1;

	// PAUSE: top-right corner, well clear of the D-pad/FIRE cluster down in the bottom two
	// corners, and clear of got_hud.c's own translucent strip which only occupies the *top-left*
	// portion of the screen (health/magic bars + jewel/key/score counts, see that file) -- a
	// smaller cell than the movement buttons since it's tapped far less often, same reasoning as
	// its DOS original: real 1_main.c reads key_flag[ESC] once per frame, an on/off press exactly
	// like this button's own key_flag[KEY_ESC] set/clear below, not a held direction.
	buttons[BTN_PAUSE].w = cell * 0.7f;
	buttons[BTN_PAUSE].h = cell * 0.7f;
	buttons[BTN_PAUSE].x = (float) w - buttons[BTN_PAUSE].w - cell * 0.4f;
	buttons[BTN_PAUSE].y = cell * 0.4f;
	buttons[BTN_PAUSE].keys[0] = KEY_ESC;
	buttons[BTN_PAUSE].num_keys = 1;

	// Belt-and-suspenders safety clamp, mirroring Descore's own layout_buttons() -- every button
	// above is already anchored with a margin comfortably inside the screen at any scale from
	// 0.90x-1.80x (worked through by hand for this exact layout when the Scaling slider was added),
	// so this shouldn't ever actually trigger, but an unusual aspect ratio or a future layout tweak
	// is cheap insurance against a button silently clipping off-screen rather than just looking a
	// little cramped.
	for (i = 0; i < NUM_BUTTONS; ++i) {
		if (buttons[i].x < 0.0f) {
			buttons[i].x = 0.0f;
		}
		if (buttons[i].y < 0.0f) {
			buttons[i].y = 0.0f;
		}
		if (buttons[i].x + buttons[i].w > (float) w) {
			buttons[i].x = (float) w - buttons[i].w;
		}
		if (buttons[i].y + buttons[i].h > (float) h) {
			buttons[i].y = (float) h - buttons[i].h;
		}
	}

	LOGI("layout_buttons: %dx%d screen, scale=%.2fx, cell=%.1fpx, pad origin (%.1f,%.1f)", w, h,
		 kTouchScaleValues[s_scale_step], cell, origin_x, origin_y);
}

// Caches the real screen dimensions and performs the first layout -- called once, from gotMain(),
// before the render loop starts. See layout_buttons() above for the actual geometry, and
// got_controls_set_scale_step() below for how a later Scaling-slider change re-lays-out without
// needing to call this again.
void got_controls_init(int w, int h) {
	int i;

	screen_w = w;
	screen_h = h;

	layout_buttons();

	for (i = 0; i < MAX_TRACKED_POINTERS; ++i) {
		touch_buttons[i] = -1;
	}
	controls_initialized = true;
}

bool got_controls_touch_enabled(void) {
	return controls_initialized && !gamepad_connected;
}

void got_controls_set_scale_step(int step) {
	if (step < 0) {
		step = 0;
	} else if (step >= TOUCH_SCALE_NUM_STEPS) {
		step = TOUCH_SCALE_NUM_STEPS - 1;
	}
	s_scale_step = step;
	if (controls_initialized) {
		layout_buttons(); // re-lay-out immediately, same "moves the slider, sees it move" shape the
						   // menu's other live-adjusted sliders (Sound/Music gain) already have
	}
}

float got_controls_touch_scale_value(int step) {
	if (step < 0) {
		step = 0;
	} else if (step >= TOUCH_SCALE_NUM_STEPS) {
		step = TOUCH_SCALE_NUM_STEPS - 1;
	}
	return kTouchScaleValues[step];
}

void got_controls_set_opacity(float alpha) {
	if (alpha < TOUCH_OPACITY_DEFAULT) {
		alpha = TOUCH_OPACITY_DEFAULT; // defensive floor -- see got_controls.h's own comment; the
										// primary clamp lives in got_menu.c's own
										// TOUCH_OPACITY_MIN_ALPHA-based setter
	} else if (alpha > 1.0f) {
		alpha = 1.0f;
	}
	s_touch_opacity = alpha;
}

static bool point_in_button(float x, float y, const TouchButton *b) {
	return x >= b->x && y >= b->y && x <= b->x + b->w && y <= b->y + b->h;
}

static int button_at(float x, float y) {
	int i;
	for (i = 0; i < NUM_BUTTONS; ++i) {
		if (point_in_button(x, y, &buttons[i])) {
			return i;
		}
	}
	return -1;
}

static void press_button(int idx) {
	int i;
	for (i = 0; i < buttons[idx].num_keys; ++i) {
		key_flag[buttons[idx].keys[i]] = 1;
	}
}

static void release_button(int idx) {
	int i;
	for (i = 0; i < buttons[idx].num_keys; ++i) {
		key_flag[buttons[idx].keys[i]] = 0;
	}
}

// True if `key` appears in both buttons' key lists -- used by handle_move() below so a key held
// by both the button a drag is leaving and the one it's entering doesn't get a spurious
// release-then-press (which would show up as a one-frame stutter -- e.g. dragging from BTN_UP
// straight into BTN_UP_RIGHT should keep KEY_UP continuously held, only pressing KEY_RIGHT fresh).
static bool key_in_button(unsigned char key, int idx) {
	int i;
	for (i = 0; i < buttons[idx].num_keys; ++i) {
		if (buttons[idx].keys[i] == key) {
			return true;
		}
	}
	return false;
}

static void handle_down(int pointer_id, float x, float y) {
	int idx;
	if (pointer_id < 0 || pointer_id >= MAX_TRACKED_POINTERS) {
		return;
	}
	idx = button_at(x, y);
	touch_buttons[pointer_id] = idx;
	if (idx != -1) {
		press_button(idx);
	}
}

static void handle_up(int pointer_id) {
	int idx;
	if (pointer_id < 0 || pointer_id >= MAX_TRACKED_POINTERS) {
		return;
	}
	idx = touch_buttons[pointer_id];
	if (idx != -1) {
		release_button(idx);
	}
	touch_buttons[pointer_id] = -1;
}

static void handle_move(int pointer_id, float x, float y) {
	int prev_idx, cur_idx, i;
	if (pointer_id < 0 || pointer_id >= MAX_TRACKED_POINTERS) {
		return;
	}
	prev_idx = touch_buttons[pointer_id];
	cur_idx = button_at(x, y);
	if (cur_idx == prev_idx) {
		return; // still over the same button (or still over nothing) -- nothing changed
	}

	if (prev_idx != -1) {
		for (i = 0; i < buttons[prev_idx].num_keys; ++i) {
			unsigned char k = buttons[prev_idx].keys[i];
			if (cur_idx == -1 || !key_in_button(k, cur_idx)) {
				key_flag[k] = 0;
			}
		}
	}
	if (cur_idx != -1) {
		for (i = 0; i < buttons[cur_idx].num_keys; ++i) {
			unsigned char k = buttons[cur_idx].keys[i];
			if (prev_idx == -1 || !key_in_button(k, prev_idx)) {
				key_flag[k] = 1;
			}
		}
	}
	touch_buttons[pointer_id] = cur_idx;
}

JNIEXPORT jboolean JNICALL
Java_wootbeer_gotandroid_GotView_touchHandler(JNIEnv *env, jclass type, jint action,
		jint pointer_id, jfloat x, jfloat y, jfloat prev_x, jfloat prev_y) {
	(void) env;
	(void) type;
	(void) prev_x;
	(void) prev_y; // unlike Descore's own touchHandler(), nothing here needs the previous position

	if (got_demo_is_active()) {
		// Swallow every touch outright rather than dispatching to handle_down()/handle_move()/
		// handle_up() below -- a demo run has no real slot/state to steer, and real keyboard_int()'s
		// own equivalent gate (`if(!demo && !record){...applied...} else if(demo) exit_flag=5;`)
		// never applies a live event to key_flag[] during a demo either, it only ever aborts.
		//
		// Only a fresh press (ACTION_DOWN/ACTION_POINTER_DOWN) actually raises the abort flag, though
		// -- unlike real keyboard_int(), which aborts on ANY event because real GOT.EXE's menu and the
		// demo engine are two separate processes, here the exact same tap that just selected "Demo"
		// off the main menu is still physically being released a frame or two after got_start_demo()
		// flips demo-active on. Treating that release (or the move between down and up) as an abort
		// too self-aborted the demo almost immediately (wootbeer: "the demo will only 'flash' on the
		// screen for a bit as it runs, then goes back to the main menu"). A release/move can only ever
		// be the tail of a press that already happened, never new intent on its own, so it's still
		// swallowed here (never reaching handle_up()/handle_move() below) but doesn't abort; a
		// genuinely new finger touching down still aborts immediately, same as before. (A finger
		// already resting on screen when a demo happens to start won't itself raise the abort -- it
		// only ever shows up here as MOVE/UP, never a fresh DOWN -- but it's still fully swallowed, so
		// it can't steer Thor either; lifting and touching down again aborts normally.)
		if (action == ACTION_DOWN || action == ACTION_POINTER_DOWN) {
			got_demo_request_abort();
		}
		return JNI_TRUE;
	}
	if (!controls_initialized || gamepad_connected) {
		return JNI_FALSE;
	}
	switch (action) {
		case ACTION_DOWN:
		case ACTION_POINTER_DOWN:
			handle_down(pointer_id, x, y);
			break;
		case ACTION_UP:
		case ACTION_POINTER_UP:
			handle_up(pointer_id);
			break;
		case ACTION_MOVE:
			handle_move(pointer_id, x, y);
			break;
		default:
			return JNI_FALSE;
	}
	return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_wootbeer_gotandroid_GotView_setGamepadConnected(JNIEnv *env, jclass type, jboolean connected) {
	(void) env;
	(void) type;
	if (connected && !gamepad_connected) {
		// A gamepad just took over -- release anything the touch D-pad was holding down so a
		// finger left resting on screen doesn't leave a direction stuck on.
		int i;
		for (i = 0; i < MAX_TRACKED_POINTERS; ++i) {
			if (touch_buttons[i] != -1) {
				release_button(touch_buttons[i]);
				touch_buttons[i] = -1;
			}
		}
	}
	gamepad_connected = (bool) connected;
}

// Drawn as a separate screen-space GL overlay on top of whatever modex_present_frame() just
// drew -- see this file's own opening comment for why (GoT's page buffer is a low-res 320x192
// source texture, not native screen resolution, so drawing into it would look wrong). Simple flat
// translucent gray quads, no textures -- Descore's own draw_buttons() is just a darkened outline
// rectangle per button too (gr_rect() + Gr_scanline_darkening_level), not artwork; this is the
// same level of visual polish, ported in spirit rather than pixel-for-pixel (Descore's approach
// uses its own 2D canvas primitives, which GoT doesn't have).
void got_draw_touch_buttons(void) {
	int i;

	if (!controls_initialized || gamepad_connected) {
		return;
	}

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	// Orthographic projection in real screen pixels, origin top-left (matching Android's own
	// touch-coordinate convention, and the button rects above), y flipped so pixel row 0 is at
	// the top of the screen the way glOrtho's default bottom-left-origin otherwise wouldn't be.
	glOrthof(0.0f, (float) screen_w, (float) screen_h, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnableClientState(GL_VERTEX_ARRAY);
	glColor4f(0.7f, 0.7f, 0.7f, s_touch_opacity); // translucent light gray -- visible over any game
												   // art, not opaque. Started as a flat 0.30f (wootbeer:
												   // "can we slightly decrease the opacity of the
												   // touch controls? maybe by like 15%" -- 0.35 ->
												   // 0.30, see TOUCH_OPACITY_DEFAULT's own comment),
												   // now user-adjustable via the new Touch Options
												   // submenu's Opacity slider (got_menu.c), floored
												   // at that same 0.30 so it can only be raised, never
												   // lowered below what a user could lose track of the
												   // controls at (wootbeer: "make sure the minimum opacity
												   // is what our previous default was so the user
												   // doesn't lose complete track of the controls").
												   // Only this one alpha value: every button (D-pad
												   // cells, FIRE/MAGIC/SELECT, PAUSE) draws through
												   // this same glColor4f() call, so all of them move
												   // together.

	for (i = 0; i < NUM_BUTTONS; ++i) {
		const TouchButton *b = &buttons[i];
		GLfloat vertices[] = {
				b->x,          b->y,
				b->x + b->w,   b->y,
				b->x,          b->y + b->h,
				b->x + b->w,   b->y + b->h,
		};
		glVertexPointer(2, GL_FLOAT, 0, vertices);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}

	glDisableClientState(GL_VERTEX_ARRAY);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // restore the default color state modex_present_frame() expects
	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
}
