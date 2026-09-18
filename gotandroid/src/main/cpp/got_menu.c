// Pause / options menu -- see got_menu.h for the overall design and scope. Generic single-level
// list-menu machinery (border box + title + items + a ">" selection cursor), reused for both the
// top-level menu and the quit-confirm submenu, the same way real select_option() is one generic
// function real option_menu()/ask_exit() both just call with different item arrays.

#include "got_menu.h"
#include "got_font.h"
#include "modex.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Real GoT scancodes (1_define.h) -- redefined here rather than shared via a header, matching how
// got_controls.c already redefines its own copies of these same constants ("kept numerically in
// sync by hand-audit, same as the struct-layout constants this whole project already tracks
// manually against the real DOS source").
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_FIRE 56
#define KEY_ESC 1
// Same real scancode VALUE got_title.c's own KEY_CANCEL already uses (see that file's own comment on
// why this and KEY_CONFIRM exist at all -- GotView.java's gotExtraScancode() sends this in ADDITION
// to KEY_FIRE whenever the B face button is pressed, never on its own). Not previously read in this
// file -- see got_menu_update()'s own comment on why that's the "B toggles a cheat instead of backing
// out" bug wootbeer hit.
#define KEY_CANCEL 14
// Same real scancode VALUE got_title.c's own KEY_CONFIRM already uses -- GotView.java's
// gotExtraScancode() sends this whenever physical A is pressed, ALONGSIDE (never instead of)
// whatever key_flag[] entry A's current gamepad-remap binding actually drives (see got_controls.c's
// own "Gamepad remapping" section) -- exactly the same "send both, let native sort it out" shape
// KEY_CANCEL/B already established here. This is what lets physical A always confirm a menu even
// after Fire has been remapped off it (wootbeer, after seeing Descore's own latest gamepad-remap round:
// "should GoT's remapper get the same decoupled A/B-always-confirms-menus treatment Descore just
// got?" -- yes), the same way KEY_CANCEL/B already always backs out regardless of what's bound to
// Fire. See got_menu_update()'s own `confirm` read, below, for where this is actually consumed.
#define KEY_CONFIRM 28
// Same real scancode VALUES got_title.c's/got_controls.c's/got_main.c's own copies already use --
// wootbeer: "each one will be an option with a slider similar to descore to set the audio/gain level of
// the sound, and music channels" (the new Sound/Music submenu, below) -- the only reader of these two
// in this file; every other submenu here is Up/Down/Fire only.
#define KEY_LEFT 75
#define KEY_RIGHT 77
// 10 steps of 10% each, 0-10 -- see s_sound_gain/s_music_gain's own comment (below) for why an int
// step count instead of a float 0.0-1.0 directly.
#define SOUND_MUSIC_GAIN_STEPS 10

// Matches got_main.c's own GOT_PAGE0/GOT_PAGE2 #defines exactly (see that file and modexgl.c's
// page_index()) -- the menu draws into GOT_PAGE0 like everything else in the live page, and
// restores from GOT_PAGE2 (the room's continuously-maintained clean background copy -- see
// got_erase_door()/got_pick_up_object() for the same GOT_PAGE2-then-copy pattern) on close.
#define GOT_PAGE0 3840u
#define GOT_PAGE2 34720u

// Real color indices from select_option()'s own source (1_panel.c) -- reused verbatim, not guessed,
// so the box renders with whatever tones the user's real PALETTE resource actually gives those
// three indices.
#define BOX_COLOR 215
#define TITLE_COLOR 54
#define ITEM_COLOR 14

// Largest item count got_menu_draw()'s own box/border math (below) can lay out without any part of
// it -- the box fill, or the 16px-tall decorative corner/edge tiles it draws 16px above/below that
// fill -- clipping off the fixed 320x192 low-res canvas everything in this port draws into (see
// got_main.c's own GOT_PAGE0 comment). That canvas gets scaled to whatever the actual device screen
// size/aspect is by a single GL quad afterward (got_main.c's EGL/GL setup) -- uniformly, so this is
// a fixed-canvas-resolution ceiling, not a per-device one; a menu that doesn't fit the 320x192
// canvas wouldn't fit on ANY device, and one that does fits identically on all of them. 8 is exactly
// that ceiling (a `count`-item box's full footprint, fill plus border, is `count*16+63` px tall;
// 8*16+63=191, exactly the last row of a 192-tall canvas indexed from 0..191 -- 9 already
// overflows it, at 207px).
// got_menu_draw() below scrolls instead of shrinking text/spacing once a list exceeds this, so
// future submenus stay safe too, not just today's 10-item top level (TOP_NUM_ITEMS, got_menu.h).
#define MENU_MAX_VISIBLE_ITEMS 8

// Real GoT's own shared key_flag[100] (got_main.c) -- same extern-sharing pattern got_controls.c
// already uses for the same array.
extern volatile char key_flag[100];

// Real BPICS1 tile data (got_main.c's own persistent s_bpics) -- needed here only for the menu
// box's real decorative corner/edge tiles (indices 192-199, ported straight from select_option()'s
// own xfput() calls). Exposed via this one getter rather than making s_bpics itself non-static.
extern const unsigned char *got_get_bpics(void);

// Real animated HAMPIC hammer-cursor sheet (got_main.c's own s_hampic/got_get_hampic() comment,
// got_title.c's own got_title_draw_main_menu() -- the first screen to use it) -- wootbeer: "all menu
// cursors should be the spinning hammer like the main menu uses... this should be consistent
// everywhere." got_menu_draw() below uses this instead of a plain ">" glyph for the same reason.
extern const unsigned char *got_get_hampic(void);

// got_main.c's own "redraw every sprite" pass -- non-static specifically so callers like this one
// can reach it, matching got_dialogue.c's own identical use in its restore_page() (real d_restore(),
// 1_back.c: ALWAYS redraws every actor immediately after restoring the clean background, every
// single call -- see got_draw_all_sprites()'s own comment). got_menu_draw() below was missing this
// half of that same real pattern -- see this file's own got_menu_draw() comment on the bug that left.
extern void got_draw_all_sprites(void);

// got_main.c's own real select_music()-equivalent (1_panel.c) -- see that function's own comment
// for exactly what it does on each transition. Called AFTER s_music_enabled is flipped below, so
// got_play_music()'s own got_menu_music_enabled() gate already reads the new state by the time
// this runs.
extern void got_music_set_enabled_from_menu(bool enabled);

// got_main.c's own JNI call-outs to GotView.java's setSoundVolume(F)V/setMusicVolume(F)V -- see
// either function's own got_main.c comment. New for the Sound/Music submenu's own two gain sliders
// (below); no real select_sound()/select_music() (1_panel.c) equivalent exists for a gain LEVEL --
// real Sound/Music only ever pick a hardware output or an on/off, never a volume -- so these two are
// this port's own addition layered on top of that real "Sound/Music" grouping (see this file's own
// scope comment for the real options_menu[0]="Sound/Music" citation), same as this port's Display
// Mode/Enhancements rows are layered on top of other real menu structure elsewhere in this file.
extern void got_sound_set_volume(float vol);
extern void got_music_set_volume(float vol);

// got_controls.c's own Touch Options plumbing -- wootbeer: "it can have an option called 'scaling'...
// and then we can also add a entry called 'opacity'" (see this file's own MENU_TOUCH_OPTIONS
// submenu, shown only while no gamepad is connected -- see TOP_TOUCH_OPTIONS's own enum comment for
// how that row now shares its slot with the newer "Remap Gamepad" submenu below instead of hiding
// outright). Same extern-declare-it-here pattern as got_sound_set_volume()/got_music_set_volume()
// just above.
extern bool got_controls_touch_enabled(void);
extern void got_controls_set_scale_step(int step);
extern float got_controls_touch_scale_value(int step);
extern void got_controls_set_opacity(float alpha);

// got_controls.c's own new gamepad-remapping plumbing -- wootbeer: "I would like to add another similar
// menu feature to what we have in descore: the gamepad remapper. I want a new pause menu option
// here that appears when a gamepad is connected, it can be in the same place that the touch options
// entry is." Same extern-declare-it-here pattern as the Touch Options plumbing just above. See
// got_controls.h's own comments for what each of these actually does.
extern bool got_controls_gamepad_connected(void);
extern int got_gamepad_remap_bound_keycode(int action);
extern void got_gamepad_remap_set_bound_keycode(int action, int keycode);
extern int got_gamepad_remap_default_keycode(int action);
extern const char *got_gamepad_remap_action_label(int action);
extern const char *got_gamepad_remap_button_name(int keycode);
extern void got_gamepad_remap_start_capture(void);
extern void got_gamepad_remap_cancel_capture(void);
extern int got_gamepad_remap_poll_capture(void);

// got_main.c's own app-wide config persistence (its own small CONFIG.DAT file, same "separate file,
// not per-save-slot" precedent as HISCORE.DAT -- see that function's own comment) -- wootbeer, after the
// sliders themselves shipped: "yes do add a way to save these two settings between app sessions,"
// then: "instead of audioset.dat can we name it config.dat... that way if we ever have to add other
// settings to save we can just use that more generic file name" (hence the generic got_config_*
// name here too, not got_audio_settings_* -- see got_main.c's own GotConfig comment). Called from
// got_menu_set_sound_gain()/got_menu_set_music_gain() below, every time either value actually
// changes.
extern void got_config_save(void);

// got_main.c's own real thor_dies()-equivalent -- see that function's own comment for the full
// checkpoint/respawn/death-spin sequence. Called directly from this file's own dispatch (not
// deferred through a flag the way a lethal COMBAT hit is, see got_thor_damaged()'s own comment on
// why that path needs to defer) since the menu's dispatch already runs at a safe point outside any
// per-tick sprite loop -- immediately safe to run the whole sequence from here.
extern void got_menu_action_die(void);

// got_main.c's own real help()-equivalent (1_file.c: `void help(void){ odin_speaks(2008,-1); }`) --
// fires the real SPEAK1 label 2008 through this port's already-working script/dialogue engine
// (got_script_execute(), the same one GLOBE's dialogue and every item pickup's flavor text already
// use), straight from the user's own real GOTRES.DAT -- no separate help text of this port's own
// invention.
extern void got_menu_action_help(void);

// got_main.c's own real save_game()/load_game()-equivalents (1_file.c) -- both act on
// got_main.c's own s_active_slot (the slot got_title.c's player-select screen chose), called only
// after this file's own MENU_SAVE_CONFIRM/MENU_LOAD_CONFIRM Yes/No submenu confirms, matching real
// save_game()/load_game()'s own `select_option(options_yesno,"Save Game?",0)`/`"Load Game?"` gate.
extern void got_menu_action_save(void);
extern void got_menu_action_load(void);

// got_main.c's own MENU_QUIT_SAVE_CONFIRM handoff (see that state's own comment, below, and
// s_quit_after_save_pending's own comment in got_main.c) -- arms a deferred quit that only actually
// fires once the Odin save-confirmation dialogue got_menu_action_save() just above queues has
// finished playing, rather than performing it immediately the way this file's own perform_quit_action()
// does for the "No, don't save" path. `to_dos` picks which of the two pending quit actions to run --
// true for "Quit to DOS" (exit(0)), false for "Quit to Opening Screen" (got_title_return_to_menu()).
extern void got_menu_quit_after_save(bool to_dos);

// got_title.c's own state-machine reset -- see that file's own comment on got_title_return_to_menu()
// for exactly what it does. Used by this file's own "Quit to Home Screen" (below) instead of
// exit(0), now that there's somewhere real to return TO.
extern void got_title_return_to_menu(void);

// got_main.c's own live, visual-only re-pick of Thor's/the hammer's sprite -- wootbeer: "did notice one
// thing with the armor and hammer override enhancement we added, it doesn't take effect unless we
// save and reload the game, is it easy to make it happen 'instantly'?" Called right after either row
// toggles below (dispatch_confirm()'s own ENH_HAMMER_COLOR/ENH_ARMOR_COLOR cases) so the change is
// visible the very next frame, matching got_main.c's own comment on why this is a distinct, narrower
// function than got_load_player_sprites() (which would reset Thor's live health/position mid-game).
extern void got_refresh_player_sprite_colors(void);

// Cheat Codes menu, "All Items:" -- got_main.c's own one-shot inventory grant/revoke (see that
// function's own comment for the real inventory bit scheme and how toggling off only removes what
// the cheat itself added). Called directly from dispatch_confirm()'s own CHEATS_ALL_ITEMS case.
extern void got_cheat_set_all_items(bool enabled);

typedef enum {
	MENU_CLOSED = 0,
	MENU_TOP,
	MENU_QUIT_CONFIRM,
	MENU_QUIT_SAVE_CONFIRM,
	MENU_SOUND_MUSIC,
	MENU_TOUCH_OPTIONS,
	MENU_GAMEPAD_REMAP,
	MENU_GAMEPAD_CAPTURE,
	MENU_SKILL,
	MENU_DISPLAY,
	MENU_ENHANCEMENTS,
	MENU_CHEATS,
	MENU_SAVE_CONFIRM,
	MENU_LOAD_CONFIRM,
} MenuState;

static MenuState s_state = MENU_CLOSED;
static int s_selected = 0;

// Drives the hammer cursor's 4 real HAMPIC animation frames (got_menu_draw() below) -- this file's
// own local counter rather than sharing got_title.c's s_blink_counter, since the pause menu and the
// title screen are never on screen at the same time anyway (see got_menu_is_open()'s own gate in
// got_main.c); ticks once per got_menu_draw() call, same /7-per-frame speed got_title_draw_main_menu()
// already established.
static int s_cursor_frame_counter = 0;

static bool s_prev_esc = false;
static bool s_prev_up = false;
static bool s_prev_down = false;
static bool s_prev_fire = false;
static bool s_prev_cancel = false; // B face button -- see got_menu_update()'s own comment
static bool s_prev_confirm = false; // A face button's own dedicated KEY_CONFIRM -- see that
									 // #define's own comment and got_menu_update()'s `confirm` read
static bool s_prev_left = false; // Sound/Music submenu's own gain sliders -- see got_menu_update()'s
static bool s_prev_right = false; // own comment

static bool s_sound_enabled = true;
static bool s_music_enabled = true;
// Sound/Music submenu's own gain sliders -- 0-SOUND_MUSIC_GAIN_STEPS (10 steps of 10% each), not a
// float directly, so the label/bar text (format_gain_label(), below) always lands on a clean
// percentage with no rounding to fuss over. wootbeer: "the current sound and music level will be the
// max, and it should just go down to zero from there" -- both default to the top of the range
// (SOUND_MUSIC_GAIN_STEPS, defined alongside the submenu below), matching this port's actual output
// level before this feature existed (GotView.java's own sfxVolume/musicVolume fields default to
// 1.0f too, so a fresh launch sounds identical to before this round shipped). Session-only, same as
// Enhancements/Cheats above -- not part of the save file (unlike s_sound_enabled/s_music_enabled
// just above, which are); nothing stops that being added later if wootbeer wants these remembered too.
static int s_sound_gain = SOUND_MUSIC_GAIN_STEPS;
static int s_music_gain = SOUND_MUSIC_GAIN_STEPS;
// Real setup.skill's own 0-2 range/default -- see got_menu.h's own comment on got_menu_skill_level().
static int s_skill_level = 1;
// This port's own real-panel/minimal-HUD display mode -- see got_menu.h's own comment on
// got_menu_display_mode() for what 0/1/2 mean and why 2 (Mode C) is the default.
static int s_display_mode = 2;
// "Hammer Color:"/"Armor Color:" -- 0=Default/1=Part I/2=Part II/3=Part III, see got_menu.h's own
// comment. Both default to 0 (Default -- whatever the real game itself would show).
static int s_enh_hammer_color = 0;
static int s_enh_armor_color = 0;
// Enhancements submenu state -- see got_menu.h's own "Enhancements" comment. Defaults to true
// ("Default" -- the new, faster WORMY/SPIDER shot speed), same session-only, not-in-the-save-file
// shape as the Cheat Codes state just below.
static bool s_enhancement_fast_enemy_shots = true;
// "Boss Speed:"/"Hammer Speed:" -- 0=Default/1=Slow/2=Fast, see got_menu.h's own comment. Both
// default to 0 (Default).
static int s_enh_boss_speed = 0;
static int s_enh_hammer_speed = 0;
// "Tombstone:" -- see got_menu.h's own comment. Defaults to Off (a new, non-canonical feature, not a
// corrected default).
static bool s_enh_tombstone = false;
// "Mirror Mode:" -- see got_menu.h's own comment. Defaults to Off, same shape as Tombstone: a new,
// non-canonical feature, session-only, not in the save file (got_main.c's render loop just reads this
// getter fresh every frame, same as every other Enhancements row).
static bool s_enh_mirror_mode = false;
// "Hourglass:" -- see got_menu.h's own comment. Defaults to Off, same shape as Tombstone/Mirror Mode:
// a real, functional real-game item (real use_hourglass(), 1_object.c) that was simply never wired
// into any real item slot/inventory bit -- wootbeer: "put it in the enhancements menu though. title it
// Hourglass:" (rejecting bundling it into the All Items cheat). Gates whether item value 8 ("Freeze
// Time") is selectable through the item picker at all -- see got_item.c's own rebuild_list() comment.
static bool s_enh_hourglass = false;

// Cheat Codes submenu state -- see got_menu.h's own "Cheat Codes" comment for why all three default
// OFF and are never persisted to a save file (unlike s_skill_level/s_sound_enabled/s_music_enabled
// above, which the save/load feature does read and write).
static bool s_cheat_unlimited_keys = false;
static bool s_cheat_god_mode = false;
static bool s_cheat_unlimited_jewels = false;
static bool s_cheat_unlimited_magic = false;
static bool s_cheat_all_items = false;

// TOP_NUM_ITEMS is the top-level menu's item count -- every row is always visible now (see
// TOP_TOUCH_OPTIONS's own enum comment above), so this is simply s_top_items[]'s fixed size, no
// longer a "maximum capacity, sometimes one less" figure.
#define TOP_NUM_ITEMS 12
static char s_skill_top_label[28];
static char s_display_top_label[32];
static const char *s_top_items[TOP_NUM_ITEMS];
// Top-level item indices -- named instead of left as bare numbers in dispatch_confirm()/
// got_menu_poll_toggle_key() below, since several submenus now each need to know which top-level
// row to land back on (ESC from a submenu, or "Continue Game" from the quit-confirm).
//
// TOP_SOUND_MUSIC used to be two separate rows here, TOP_SOUND/TOP_MUSIC, each an in-place "On"/
// "Off" toggle -- wootbeer: "can we create a new sub-menu under the pause/options menu that has a
// heading of Sound/Music. we will move the options for 'Sound Effects:' and 'Music:' into this
// menu." Folded back into the single row real options_menu[0] ("Sound/Music", cited above) actually
// is -- see MENU_SOUND_MUSIC's own comment, below, for the new submenu those two toggles moved into
// (plus the two new gain sliders wootbeer asked for alongside them).
//
// Order is wootbeer's own explicit re-request, not real options_menu[]'s (1_panel.c) own ordering
// anymore: "can we rearrange the entries into this order?: Resume / Sound/Music / Display Mode: /
// Skill Level: / Save Game / Load Game / Die / Help / Cheat Codes / Enhancements / Quit Game." Two
// swaps from the previous order (which itself matched real options_menu[]'s own Sound/Music, Skill
// Level, Save Game, Load Game, Die, Help, Quit for seven of its eight real rows, plus this port's own
// Display Mode/Enhancements slotted in): Display Mode now comes before Skill Level rather than after
// it, and Cheat Codes/Enhancements swap places (Cheat Codes now sits right after Help, Enhancements
// right before Quit). The eighth real row, Turbo Mode, is still deliberately not here -- see
// got_menu.h's own comment on why it was removed.
//
// TOP_TOUCH_OPTIONS used to be a genuinely conditional row -- hidden from the list entirely
// whenever a gamepad was connected (wootbeer's own original request: "only make this entry appear
// though if touch controls are enabled, since it doesn't have a use if the user is using a
// controller"), via a one-row-shift visual_from_top()/top_from_visual() translation layer that used
// to live here. Simplified away once "Remap Gamepad" was added (wootbeer: "I want a new pause menu
// option here that appears when a gamepad is connected, it can be in the same place that the touch
// options entry is"): touch controls and a connected gamepad are perfect complements in this port
// (got_controls_touch_enabled() is defined as "controls initialized AND no gamepad connected" --
// see that function's own got_controls.c comment), so this ROW is now always visible, it's only the
// CONTENT that's conditional -- "Touch Options" or "Remap Gamepad" depending on
// got_controls_gamepad_connected(), see refresh_top_labels()/dispatch_confirm()'s own
// TOP_TOUCH_OPTIONS cases below. With nothing left to ever hide, every TOP_X value is simply its
// own visual position again, so the translation layer (and every `visual_from_top()`/
// `top_from_visual()` call site it needed) is gone.
enum {
	TOP_RESUME = 0,
	TOP_SOUND_MUSIC = 1,
	TOP_TOUCH_OPTIONS = 2,
	TOP_DISPLAY = 3,
	TOP_SKILL = 4,
	TOP_SAVE = 5,
	TOP_LOAD = 6,
	TOP_DIE = 7,
	TOP_HELP = 8,
	TOP_CHEATS = 9,
	TOP_ENHANCEMENTS = 10,
	TOP_QUIT = 11,
};

// wootbeer: "'Quit to Opening Screen', not 'Quit to home Screen'" (renamed to match got_title.c's own
// terminology for that screen -- see got_title_bg.h's own comment) -- "also we need to add the
// 'Quit to DOS' option, that will close the game entirely" (real Quit-to-DOS behavior, i.e. this
// port's actual process exit, now its own separate row instead of overloading "...Opening Screen").
#define QUIT_NUM_ITEMS 3
static const char *s_quit_items[QUIT_NUM_ITEMS] = {
	"Continue Game", "Quit to Opening Screen", "Quit to DOS",
};
enum {
	QUIT_CONTINUE = 0,
	QUIT_TO_OPENING_SCREEN = 1,
	QUIT_TO_DOS = 2,
};

// wootbeer: "there should be a pop-up asking the player if they want to save there game either when
// exiting to main screen or exiting to 'dos'." No real precedent for this -- real ask_exit()'s own
// caller (1_main.c: `xdos=ask_exit(); if(xdos==2 || xdos==3) break;`) never prompts to save before
// either quit path, it just breaks straight out of the main loop -- so this is a new, wootbeer-requested
// UX addition layered on top of the real Quit Game submenu above, not a real-fidelity port.
// Remembers which of the two quit rows just above (QUIT_TO_OPENING_SCREEN/QUIT_TO_DOS) is pending
// while MENU_QUIT_SAVE_CONFIRM is on screen, so both the "Yes"/"No" dispatch and a cancel back out of
// it (got_menu_cancel_submenu() below) know which quit action to actually perform once the save
// question resolves, or which row to re-select after backing out. Only meaningful while
// s_state==MENU_QUIT_SAVE_CONFIRM; no need to reset it elsewhere since it's always written (in
// dispatch_confirm()'s own MENU_QUIT_CONFIRM case, below) before that state is ever entered.
static int s_pending_quit_action = QUIT_TO_OPENING_SCREEN;

// Real options_yesno[] (1_panel.c) verbatim -- shared by MENU_SAVE_CONFIRM and MENU_LOAD_CONFIRM
// below, matching real save_game()/load_game()'s own identical
// `select_option(options_yesno,"...",0)` call shape (only the title string differs between them).
#define YESNO_NUM_ITEMS 2
static const char *s_yesno_items[YESNO_NUM_ITEMS] = {"Yes", "No"};

// Real options_skill[] (1_panel.c) verbatim -- a "pick one" list (the cursor position itself IS
// the current skill, matching real select_skill()'s own `sel=setup.skill;` initial-position
// convention below), not an in-place toggle like Sound Effects/Music, so these three don't need
// per-frame-refreshed label buffers the way the toggle rows below do -- the strings never change.
#define SKILL_NUM_ITEMS 3
static const char *s_skill_items[SKILL_NUM_ITEMS] = {"Easy Enemies", "Normal Enemies",
													   "Tough Enemies"};

// This port's own "Display Mode" submenu -- no real options_skill[]-style array to port (see
// got_menu.h's own top comment), same "cursor position IS the current value" shape as Skill
// Level's own submenu just above, not an in-place toggle. Order matches modex.h's own
// ModexDisplayMode enum (A=0, B=1, C=2) exactly, so got_menu_display_mode()'s return value can be
// forwarded straight to modex_set_display_mode() with a plain cast, no translation table needed.
#define DISPLAY_NUM_ITEMS 3
static const char *s_display_items[DISPLAY_NUM_ITEMS] = {
	"Force 4:3 (Pillarboxed)",
	"Authentic Panel (Stretched)",
	"Minimal HUD (More Playfield)",
};

// Enhancements submenu -- six in-place toggle rows so far (wootbeer: "we will add other options to this
// new Enhancements menu later"), same "stays in the submenu after confirming" shape Sound Effects/
// Music at the top level and the Cheat Codes submenu just below already use -- two four-way (Hammer
// Color, Armor Color), one two-way (Enemy Atk. Speed) or two three-way (Boss Speed, Hammer Speed),
// plus one two-way (Tombstone) cycle, not a separate "pick one" list like Skill Level/Display Mode.
// See got_menu.h's own "Enhancements" comments for the full rationale on each. Hammer Color/Armor
// Color placed first, above every existing row, per wootbeer's own explicit placement request.
#define ENHANCEMENTS_NUM_ITEMS 8
static char s_enh_hammer_color_label[36];
static char s_enh_armor_color_label[36];
static char s_enh_atk_speed_label[36];
static char s_enh_boss_speed_label[36];
static char s_enh_hammer_speed_label[36];
static char s_enh_tombstone_label[36];
static char s_enh_mirror_mode_label[36];
static char s_enh_hourglass_label[36];
static const char *s_enhancements_items[ENHANCEMENTS_NUM_ITEMS];
enum {
	ENH_HAMMER_COLOR = 0,
	ENH_ARMOR_COLOR = 1,
	ENH_ATK_SPEED = 2,
	ENH_BOSS_SPEED = 3,
	ENH_HAMMER_SPEED = 4,
	ENH_TOMBSTONE = 5,
	ENH_MIRROR_MODE = 6,
	ENH_HOURGLASS = 7,
};

// Shared 0=Default/1=Slow/2=Fast name table for both Boss Speed and Hammer Speed's own labels.
static const char *const s_enh_speed_names[3] = {"Default", "Slow", "Fast"};

// Shared 0=Default/1=Part I/2=Part II/3=Part III name table for both Hammer Color and Armor Color's
// own labels -- see got_main.c's own s_enh_color_part_tier for what each "Part" actually loads.
static const char *const s_enh_part_names[4] = {"Default", "Part I", "Part II", "Part III"};

// Cheat Codes submenu -- five independent in-place toggle rows, same "stays in the submenu after
// confirming" shape Sound Effects/Music at the top level already use. See got_menu.h's own
// "Cheat Codes" comment for the full rationale. "All Items:" (added in a later round, wootbeer: "add a
// new option in the 'Cheat Code' menu, that will be for 'All Items'") is the one row here that isn't
// a live per-tick gate -- toggling it calls got_main.c's own got_cheat_set_all_items() directly (see
// dispatch_confirm()'s own CHEATS_ALL_ITEMS case), which does a real one-shot inventory mutation.
#define CHEATS_NUM_ITEMS 5
static char s_cheat_keys_label[28];
static char s_cheat_god_label[28];
static char s_cheat_jewels_label[28];
static char s_cheat_magic_label[28];
static char s_cheat_all_items_label[28];
static const char *s_cheats_items[CHEATS_NUM_ITEMS];
enum {
	CHEATS_UNLIMITED_KEYS = 0,
	CHEATS_GOD_MODE = 1,
	CHEATS_UNLIMITED_JEWELS = 2,
	CHEATS_UNLIMITED_MAGIC = 3,
	CHEATS_ALL_ITEMS = 4,
};

// Sound/Music submenu -- wootbeer: "can we create a new sub-menu under the pause/options menu that has a
// heading of Sound/Music. we will move the options for 'Sound Effects:' and 'Music:' into this menu.
// In this menu I also wanted to add two new options 'Sound:' and 'Music:' each one will be an option
// with a slider similar to descore to set the audio/gain level of the sound, and music channels. the
// current sound and music level will be the max, and it should just go down to zero from there. and
// then of course just keep the old option to toggle a mute for the sound and music channel we already
// have. only do this if it's possible though to easily change the levels." It was: GotView.java's own
// SFX/music playback already funnels through exactly two call sites (soundPool.play()'s own per-call
// volume params, and a single shared MediaPlayer for music), so one gain float apiece was enough --
// see got_main.c's own got_sound_set_volume()/got_music_set_volume() comments for the full plumbing.
// Real precedent for the grouping itself: options_menu[0] in 1_panel.c literally is "Sound/Music" (one
// combined row -- see this file's own top-of-enum comment on TOP_SOUND_MUSIC), though the real game
// dispatches it to two sequential full-screen pickers (select_sound() then select_music()) rather than
// a single persistent submenu -- this port groups the two toggles wootbeer asked to move here, plus the
// two new sliders (no real equivalent; real select_sound()/select_music() only ever pick a hardware
// output or a plain on/off, never a gain level), into one submenu instead, matching how Enhancements/
// Cheats above already group several related rows together rather than mirroring real code's own
// separate-screens shape exactly.
//
// Gain rows are Left/Right-adjusted in place (got_menu_update()'s own comment), not Fire-confirmed
// like every other row in this file -- Fire does nothing on either of them, same as it does nothing on
// a real select_option() row that isn't currently highlighted for confirm. The two toggle rows keep
// the exact same Fire-to-flip-in-place behavior they always had at the top level.
#define SOUND_MUSIC_NUM_ITEMS 4
static char s_sound_gain_label[40];
static char s_sound_toggle_label[24];
static char s_music_gain_label[40];
static char s_music_toggle_label[24];
static const char *s_sound_music_items[SOUND_MUSIC_NUM_ITEMS];
enum {
	SOUND_MUSIC_SOUND_GAIN = 0,
	SOUND_MUSIC_SOUND_TOGGLE = 1,
	SOUND_MUSIC_MUSIC_GAIN = 2,
	SOUND_MUSIC_MUSIC_TOGGLE = 3,
};

// New "Touch Options" submenu -- wootbeer, after seeing Descore's own port of this same feature: "we
// had also added a menu option with an adjustment to touch control scaling... so can we add a new
// sub-menu to the pause menu as well? the new entry can be called 'touch options'... and then it
// can have an option called 'scaling'... this slider will be used to adjust the scaling. and then
// we can also add a entry called 'opacity'... only make this entry appear though if touch controls
// are enabled... also it can save its options between sessions in that config.dat we setup
// earlier." Same "stays in the submenu, Left/Right-adjusted in place" shape as Sound/Music's own
// two gain rows just above -- Fire does nothing on either row here either (there's no third,
// Fire-toggled row the way Sound/Music has two), so unlike every other submenu in this file,
// dispatch_confirm() below has no MENU_TOUCH_OPTIONS branch at all -- there's simply nothing for a
// Fire press to do while this submenu is open.
#define TOUCH_OPTIONS_NUM_ITEMS 2
static char s_touch_scale_label[40];
static char s_touch_opacity_label[40];
static const char *s_touch_options_items[TOUCH_OPTIONS_NUM_ITEMS];
enum {
	TOUCH_OPTIONS_SCALING = 0,
	TOUCH_OPTIONS_OPACITY = 1,
};

// New "Remap Gamepad" submenu -- shares TOP_TOUCH_OPTIONS's own top-level row with Touch Options
// (see that row's own enum comment, above), shown instead of it whenever
// got_controls_gamepad_connected() is true. wootbeer: "I would like to add another similar menu feature
// to what we have in descore: the gamepad remapper... it can have all the same options, apply,
// cancel, default... the default controls will be what the default controls are of my retroid
// pocket 6 right now. if we need a place to save the controls it can also be in the config.dat" --
// then, after being shown Descore's own LATEST version of that screen (which decoupled physical
// A/B from remapping so they always confirm/cancel every menu, see KEY_CONFIRM's own #define
// comment): "should GoT's remapper get the same decoupled A/B-always-confirms-menus treatment
// Descore just got?" -- yes.
//
// GAMEPAD_NUM_ACTIONS/the action-index enum below must stay numerically in sync with
// got_controls.c's own GAMEPAD_NUM_ACTIONS/kGamepadRemapActions[] order (hand-audited, same policy
// this whole project already uses for every other cross-file constant -- e.g.
// TOUCH_SCALE_NUM_STEPS's own comment).
#define GAMEPAD_NUM_ACTIONS 3
#define GAMEPAD_UNBOUND (-1)
enum {
	GAMEPAD_ACTION_FIRE = 0,
	GAMEPAD_ACTION_MAGIC = 1,
	GAMEPAD_ACTION_SELECT = 2,
};

// Reset/Cancel/Apply, same three semantics as Descore's own remap screen (wootbeer's own explicit
// request: "the menu should have all the same options, apply, cancel, default"), appended after the
// GAMEPAD_NUM_ACTIONS action rows.
#define GAMEPAD_REMAP_NUM_ITEMS (GAMEPAD_NUM_ACTIONS + 3)
enum {
	GAMEPAD_REMAP_ROW_RESET = GAMEPAD_NUM_ACTIONS,
	GAMEPAD_REMAP_ROW_CANCEL,
	GAMEPAD_REMAP_ROW_APPLY,
};

// Staged bindings -- wootbeer's own explicit "apply, cancel, default" request means this screen doesn't
// take effect immediately the way every OTHER submenu in this file does (a genuinely new shape for
// this file): entering the screen seeds this from the live table (got_gamepad_remap_bound_keycode()
// per action), every row press/capture only ever edits THIS array, Cancel discards it outright, and
// only Apply actually commits it (got_gamepad_remap_set_bound_keycode() per action, then
// got_config_save()) -- matching Descore's own do_remap_gamepad_menu() staging[] shape exactly.
static int s_gp_staging[GAMEPAD_NUM_ACTIONS];
// Which action row opened the current MENU_GAMEPAD_CAPTURE step -- set right before entering that
// state, read back both to resolve a completed/cancelled capture and to build that screen's own
// "Rebind: <action>" title.
static int s_gp_capture_action = 0;
static char s_gp_action_row_labels[GAMEPAD_NUM_ACTIONS][40];
static const char *s_gamepad_remap_items[GAMEPAD_REMAP_NUM_ITEMS];
// Single-row "instructional" item for MENU_GAMEPAD_CAPTURE -- reuses got_menu_draw()'s own generic
// list machinery rather than a custom draw+input loop (unlike Descore's own do_remap_gamepad_menu(),
// which predates this port's per-frame got_menu_update()/got_menu_draw() split and has no such
// machinery to reuse) -- wootbeer: "of course it will be done in the style of all of our other god of
// thunder menus." The cursor icon ends up sitting next to the first line, a small cosmetic quirk
// accepted in exchange for not building a second, one-off menu renderer. Split across two lines
// (wootbeer, after playtesting: "the 'press a button for this action...' etc, text is too long, maybe
// make it two lines or more?") -- the original single 54-character line ran well past this port's
// fixed 320px-wide canvas at this font's real per-glyph width. Two array entries, not one string
// with an embedded line break -- got_xprint()/got_menu_draw() draw one item per row already (see
// every other multi-row submenu in this file), so this reuses that existing machinery rather than
// teaching the text primitive a line-break convention it's never needed before.
static const char *s_gamepad_capture_items[2] = {
	"Press a button for this action...",
	"(Start to cancel)",
};
// "Rebind: <action>" -- built fresh each time MENU_GAMEPAD_CAPTURE is entered (dispatch_confirm()'s
// own MENU_GAMEPAD_REMAP case, below), current_title()'s own MENU_GAMEPAD_CAPTURE case just returns
// this buffer.
static char s_gamepad_capture_title[48];

// 0-8, indexing got_controls.c's own 9-entry scale table -- not a float directly, same "step index,
// not a raw value" reasoning s_sound_gain/s_music_gain's own comment gives. Must stay numerically in
// sync with got_controls.c's own TOUCH_SCALE_NUM_STEPS/TOUCH_SCALE_DEFAULT_STEP (hand-audited, same
// policy this whole project already uses for every other cross-file constant) -- index 2 is that
// table's 1.00x entry, i.e. today's existing, pre-this-feature button size exactly, so a fresh
// install (or an existing CONFIG.DAT with no saved scale yet) looks identical to before this round
// shipped.
#define TOUCH_SCALE_NUM_STEPS 9
#define TOUCH_SCALE_DEFAULT_STEP 2
static int s_touch_scale_step = TOUCH_SCALE_DEFAULT_STEP;

// 0-SOUND_MUSIC_GAIN_STEPS(10), reusing the same 10-step granularity the Sound/Music gain sliders
// already use -- but unlike a gain slider, this doesn't run all the way down to invisible: wootbeer:
// "make sure the minimum opacity is what our previous default was so the user doesn't lose complete
// track of the controls." Step 0 maps to TOUCH_OPACITY_MIN_ALPHA (that previous default, see
// format_opacity_label()'s own comment below), not to alpha 0, and step 0 is also this option's own
// default -- an existing install's touch controls look exactly as they did before this feature
// shipped until the user actually raises this slider, same reasoning TOUCH_SCALE_DEFAULT_STEP's own
// comment gives for Scaling.
static int s_touch_opacity_step = 0;

// wootbeer's own "minimum opacity is what our previous default was" -- must stay numerically in sync
// with got_controls.c's own TOUCH_OPACITY_DEFAULT (hand-audited, same policy every other cross-file
// constant here already follows). Step 0 above maps to this alpha, not to 0.0f.
#define TOUCH_OPACITY_MIN_ALPHA 0.30f

// Forward declaration: implemented below, alongside refresh_cheats_labels() -- needed here because
// got_menu_set_skill_level()/got_menu_set_sound_enabled()/got_menu_set_music_enabled() just below
// call it (to keep the top-level menu's own labels in sync with a save-load-triggered change) but
// are defined before it for the same "getter/setter pairs stay next to each other" reason
// got_menu_skill_level() sits next to its own getter siblings rather than down by refresh_top_labels().
static void refresh_top_labels(void);
// Same forward-declaration reason as refresh_top_labels() just above: got_menu_cheat_disable_
// unlimited_jewels() below (kept next to its own getter siblings, same "getter/setter pairs stay
// together" reasoning) calls this, but it's defined further down alongside its own submenu.
static void refresh_cheats_labels(void);
// Same forward-declaration reason again -- dispatch_confirm()'s TOP_ENHANCEMENTS case needs this
// before its own definition, which sits down alongside the rest of the Enhancements submenu.
static void refresh_enhancements_labels(void);
// Same forward-declaration reason again -- dispatch_confirm()'s TOP_SOUND_MUSIC case needs this
// before its own definition, which sits down alongside the rest of the Sound/Music submenu.
static void refresh_sound_music_labels(void);
// Same forward-declaration reason again -- dispatch_confirm()'s TOP_TOUCH_OPTIONS case needs this
// before its own definition, which sits down alongside the rest of the Touch Options submenu.
static void refresh_touch_options_labels(void);
// Same forward-declaration reason again -- dispatch_confirm()'s TOP_TOUCH_OPTIONS/MENU_GAMEPAD_REMAP
// cases need this before its own definition, which sits down alongside the rest of the Remap
// Gamepad submenu.
static void refresh_gamepad_remap_labels(void);

// Mirror Mode's own exclude-rect support -- see got_menu_draw()'s own comment on where these are
// set, and got_menu_get_box_rect() just below for how they're read back. Stale/meaningless while
// s_state == MENU_CLOSED, exactly like every other per-frame-computed layout local in this file --
// the getter itself guards that, never returning them in that state.
static int s_menu_box_x1, s_menu_box_y1, s_menu_box_x2, s_menu_box_y2;

bool got_menu_sound_enabled(void) {
	return s_sound_enabled;
}

bool got_menu_music_enabled(void) {
	return s_music_enabled;
}

int got_menu_skill_level(void) {
	return s_skill_level;
}

void got_menu_set_skill_level(int level) {
	if (level < 0) {
		level = 0;
	} else if (level > 2) {
		level = 2;
	}
	s_skill_level = level;
	refresh_top_labels();
}

void got_menu_set_sound_enabled(bool enabled) {
	s_sound_enabled = enabled;
	refresh_top_labels();
}

void got_menu_set_music_enabled(bool enabled) {
	s_music_enabled = enabled;
	refresh_top_labels();
}

int got_menu_sound_gain(void) {
	return s_sound_gain;
}

int got_menu_music_gain(void) {
	return s_music_gain;
}

// See got_menu.h's own comment on these two -- the single place a gain value actually gets clamped,
// pushed to GotView.java, reflected in the submenu's own label text, and persisted, called both from
// got_menu_update()'s own Left/Right handling below and from got_main.c's own startup load
// (got_config_init()).
void got_menu_set_sound_gain(int gain) {
	if (gain < 0) {
		gain = 0;
	} else if (gain > SOUND_MUSIC_GAIN_STEPS) {
		gain = SOUND_MUSIC_GAIN_STEPS;
	}
	s_sound_gain = gain;
	got_sound_set_volume((float) gain / SOUND_MUSIC_GAIN_STEPS);
	refresh_sound_music_labels();
	got_config_save();
}

void got_menu_set_music_gain(int gain) {
	if (gain < 0) {
		gain = 0;
	} else if (gain > SOUND_MUSIC_GAIN_STEPS) {
		gain = SOUND_MUSIC_GAIN_STEPS;
	}
	s_music_gain = gain;
	got_music_set_volume((float) gain / SOUND_MUSIC_GAIN_STEPS);
	refresh_sound_music_labels();
	got_config_save();
}

int got_menu_touch_scale_step(void) {
	return s_touch_scale_step;
}

int got_menu_touch_opacity_step(void) {
	return s_touch_opacity_step;
}

// See got_menu.h's own comment on these two -- same "the single place a value actually gets
// clamped, pushed to its consumer, reflected in the submenu's own label text, and persisted" shape
// got_menu_set_sound_gain()/got_menu_set_music_gain() just above already establish, just pushing to
// got_controls.c (got_controls_set_scale_step()/got_controls_set_opacity()) instead of GotView.java.
void got_menu_set_touch_scale_step(int step) {
	if (step < 0) {
		step = 0;
	} else if (step >= TOUCH_SCALE_NUM_STEPS) {
		step = TOUCH_SCALE_NUM_STEPS - 1;
	}
	s_touch_scale_step = step;
	got_controls_set_scale_step(step);
	refresh_touch_options_labels();
	got_config_save();
}

void got_menu_set_touch_opacity_step(int step) {
	if (step < 0) {
		step = 0;
	} else if (step > SOUND_MUSIC_GAIN_STEPS) {
		step = SOUND_MUSIC_GAIN_STEPS;
	}
	s_touch_opacity_step = step;
	got_controls_set_opacity(TOUCH_OPACITY_MIN_ALPHA +
			(1.0f - TOUCH_OPACITY_MIN_ALPHA) * step / SOUND_MUSIC_GAIN_STEPS);
	refresh_touch_options_labels();
	got_config_save();
}

int got_menu_display_mode(void) {
	return s_display_mode;
}

void got_menu_set_display_mode(int mode) {
	if (mode < 0) {
		mode = 0;
	} else if (mode > 2) {
		mode = 2;
	}
	s_display_mode = mode;
	refresh_top_labels();
}

int got_menu_enhancement_hammer_color(void) {
	return s_enh_hammer_color;
}

int got_menu_enhancement_armor_color(void) {
	return s_enh_armor_color;
}

bool got_menu_enhancement_fast_enemy_shots_enabled(void) {
	return s_enhancement_fast_enemy_shots;
}

int got_menu_enhancement_boss_speed(void) {
	return s_enh_boss_speed;
}

int got_menu_enhancement_hammer_speed(void) {
	return s_enh_hammer_speed;
}

bool got_menu_enhancement_mirror_mode_enabled(void) {
	return s_enh_mirror_mode;
}

// Mirror Mode's own exclude-rect support -- see modex.h's own modex_set_mirror_exclude_rect()
// comment for the full design. Returns false (leaving the outputs untouched) whenever the pause
// menu isn't actually open, matching every other got_menu_is_open()-style query in this file.
bool got_menu_get_box_rect(int *x1, int *y1, int *x2, int *y2) {
	if (s_state == MENU_CLOSED) {
		return false;
	}
	if (x1) *x1 = s_menu_box_x1;
	if (y1) *y1 = s_menu_box_y1;
	if (x2) *x2 = s_menu_box_x2;
	if (y2) *y2 = s_menu_box_y2;
	return true;
}

bool got_menu_enhancement_tombstone_enabled(void) {
	return s_enh_tombstone;
}

bool got_menu_enhancement_hourglass_enabled(void) {
	return s_enh_hourglass;
}

bool got_menu_cheat_unlimited_keys_enabled(void) {
	return s_cheat_unlimited_keys;
}

bool got_menu_cheat_god_mode_enabled(void) {
	return s_cheat_god_mode;
}

bool got_menu_cheat_unlimited_jewels_enabled(void) {
	return s_cheat_unlimited_jewels;
}

bool got_menu_cheat_unlimited_magic_enabled(void) {
	return s_cheat_unlimited_magic;
}

// See this function's own got_menu.h comment.
void got_menu_cheat_disable_unlimited_jewels(void) {
	if (!s_cheat_unlimited_jewels) {
		return; // already off -- nothing to do, and no label to refresh
	}
	s_cheat_unlimited_jewels = false;
	refresh_cheats_labels();
}

// See this function's own got_menu.h comment.
void got_menu_cheat_disable_all_items(void) {
	if (!s_cheat_all_items) {
		return; // already off -- nothing to do, and no label to refresh
	}
	s_cheat_all_items = false;
	refresh_cheats_labels();
}

// Writes into s_top_items[] at each row's own TOP_X index -- every row is always visible now (see
// TOP_TOUCH_OPTIONS's own enum comment), so this is a plain 1:1 fill, no translation needed.
static void refresh_top_labels(void) {
	static const char *const skill_names[SKILL_NUM_ITEMS] = {"Easy", "Normal", "Tough"};
	static const char *const display_names[DISPLAY_NUM_ITEMS] = {"4:3", "Stretched", "Minimal HUD"};

	s_top_items[TOP_RESUME] = "Resume";
	s_top_items[TOP_SOUND_MUSIC] = "Sound/Music";
	// Same row, different content -- see TOP_TOUCH_OPTIONS's own enum comment above.
	s_top_items[TOP_TOUCH_OPTIONS] = got_controls_gamepad_connected() ? "Remap Gamepad" : "Touch Options";
	snprintf(s_display_top_label, sizeof(s_display_top_label), "Display Mode: %s",
			 display_names[s_display_mode]);
	s_top_items[TOP_DISPLAY] = s_display_top_label;
	snprintf(s_skill_top_label, sizeof(s_skill_top_label), "Skill Level: %s",
			 skill_names[s_skill_level]);
	s_top_items[TOP_SKILL] = s_skill_top_label;
	s_top_items[TOP_SAVE] = "Save Game";
	s_top_items[TOP_LOAD] = "Load Game";
	s_top_items[TOP_DIE] = "Die";
	s_top_items[TOP_HELP] = "Help";
	s_top_items[TOP_CHEATS] = "Cheat Codes";
	s_top_items[TOP_ENHANCEMENTS] = "Enhancements";
	s_top_items[TOP_QUIT] = "Quit Game";
}

// Builds a single text-embedded slider row, e.g. "Sound: [======----] 60%", straight out of the 0-
// SOUND_MUSIC_GAIN_STEPS int -- no new drawing primitive needed since got_font.h's real font already
// covers '[', ']', '=', '-', digits, and '%' (see this file's own Sound/Music submenu comment).
static void format_gain_label(char *buf, size_t buf_size, const char *prefix, int gain) {
	char bar[SOUND_MUSIC_GAIN_STEPS + 1];
	int i;

	for (i = 0; i < SOUND_MUSIC_GAIN_STEPS; i++) {
		bar[i] = (i < gain) ? '=' : '-';
	}
	bar[SOUND_MUSIC_GAIN_STEPS] = '\0';
	snprintf(buf, buf_size, "%s: [%s] %d%%", prefix, bar, gain * 100 / SOUND_MUSIC_GAIN_STEPS);
}

static void refresh_sound_music_labels(void) {
	format_gain_label(s_sound_gain_label, sizeof(s_sound_gain_label), "Sound", s_sound_gain);
	s_sound_music_items[SOUND_MUSIC_SOUND_GAIN] = s_sound_gain_label;
	snprintf(s_sound_toggle_label, sizeof(s_sound_toggle_label), "Sound Effects: %s",
			 s_sound_enabled ? "On" : "Off");
	s_sound_music_items[SOUND_MUSIC_SOUND_TOGGLE] = s_sound_toggle_label;
	format_gain_label(s_music_gain_label, sizeof(s_music_gain_label), "Music", s_music_gain);
	s_sound_music_items[SOUND_MUSIC_MUSIC_GAIN] = s_music_gain_label;
	snprintf(s_music_toggle_label, sizeof(s_music_toggle_label), "Music: %s",
			 s_music_enabled ? "On" : "Off");
	s_sound_music_items[SOUND_MUSIC_MUSIC_TOGGLE] = s_music_toggle_label;
}

// "Scaling: [----o----] 1.00x" -- same bracket-bar look format_gain_label() above already
// established, just a multiplier instead of a percentage (scaling isn't naturally a 0-100%
// quantity the way gain is) and a single 'o' marking the CURRENT step rather than a filled run
// from empty, since scale isn't naturally zero-based either (index 0 is 0.90x, not "nothing," so a
// filled-from-the-left bar like gain's own would misleadingly suggest "0 up to here" instead of
// "here, out of this whole range").
static void format_scale_label(char *buf, size_t buf_size, int step) {
	char bar[TOUCH_SCALE_NUM_STEPS + 1];
	int i;

	for (i = 0; i < TOUCH_SCALE_NUM_STEPS; i++) {
		bar[i] = (i == step) ? 'o' : '-';
	}
	bar[TOUCH_SCALE_NUM_STEPS] = '\0';
	snprintf(buf, buf_size, "Scaling: [%s] %.2fx", bar, (double) got_controls_touch_scale_value(step));
}

// Same bracket-bar shape as format_gain_label() above, but the displayed percentage reflects the
// ACTUAL alpha got_draw_touch_buttons() (got_controls.c) will render -- TOUCH_OPACITY_MIN_ALPHA's
// own 30% up to 100%, not a raw 0-10 step fraction -- so this can't just call format_gain_label()
// directly; that would print "0%" at the floor step, when the floor is actually a real, visible
// 30% opacity, not nothing.
static void format_opacity_label(char *buf, size_t buf_size, int step) {
	char bar[SOUND_MUSIC_GAIN_STEPS + 1];
	int i;
	float alpha;

	for (i = 0; i < SOUND_MUSIC_GAIN_STEPS; i++) {
		bar[i] = (i < step) ? '=' : '-';
	}
	bar[SOUND_MUSIC_GAIN_STEPS] = '\0';
	alpha = TOUCH_OPACITY_MIN_ALPHA + (1.0f - TOUCH_OPACITY_MIN_ALPHA) * step / SOUND_MUSIC_GAIN_STEPS;
	snprintf(buf, buf_size, "Opacity: [%s] %d%%", bar, (int) (alpha * 100.0f + 0.5f));
}

static void refresh_touch_options_labels(void) {
	format_scale_label(s_touch_scale_label, sizeof(s_touch_scale_label), s_touch_scale_step);
	s_touch_options_items[TOUCH_OPTIONS_SCALING] = s_touch_scale_label;
	format_opacity_label(s_touch_opacity_label, sizeof(s_touch_opacity_label), s_touch_opacity_step);
	s_touch_options_items[TOUCH_OPTIONS_OPACITY] = s_touch_opacity_label;
}

// Builds each action row's "Fire: A" style label from s_gp_staging[] (never the live table --
// this screen always displays what's STAGED, including mid-capture steals of another row's
// binding, until Apply or Cancel resolves it one way or the other) plus the fixed Reset/Cancel/
// Apply row text.
static void refresh_gamepad_remap_labels(void) {
	int i;
	for (i = 0; i < GAMEPAD_NUM_ACTIONS; i++) {
		snprintf(s_gp_action_row_labels[i], sizeof(s_gp_action_row_labels[i]), "%s: %s",
				 got_gamepad_remap_action_label(i), got_gamepad_remap_button_name(s_gp_staging[i]));
		s_gamepad_remap_items[i] = s_gp_action_row_labels[i];
	}
	s_gamepad_remap_items[GAMEPAD_REMAP_ROW_RESET] = "Reset to Defaults";
	s_gamepad_remap_items[GAMEPAD_REMAP_ROW_CANCEL] = "Cancel";
	s_gamepad_remap_items[GAMEPAD_REMAP_ROW_APPLY] = "Apply";
}

static void refresh_enhancements_labels(void) {
	snprintf(s_enh_hammer_color_label, sizeof(s_enh_hammer_color_label), "Hammer Color: %s",
			 s_enh_part_names[s_enh_hammer_color]);
	s_enhancements_items[ENH_HAMMER_COLOR] = s_enh_hammer_color_label;
	snprintf(s_enh_armor_color_label, sizeof(s_enh_armor_color_label), "Armor Color: %s",
			 s_enh_part_names[s_enh_armor_color]);
	s_enhancements_items[ENH_ARMOR_COLOR] = s_enh_armor_color_label;
	snprintf(s_enh_atk_speed_label, sizeof(s_enh_atk_speed_label), "Enemy Atk. Speed: %s",
			 s_enhancement_fast_enemy_shots ? "Default" : "Slow");
	s_enhancements_items[ENH_ATK_SPEED] = s_enh_atk_speed_label;
	snprintf(s_enh_boss_speed_label, sizeof(s_enh_boss_speed_label), "Boss Speed: %s",
			 s_enh_speed_names[s_enh_boss_speed]);
	s_enhancements_items[ENH_BOSS_SPEED] = s_enh_boss_speed_label;
	snprintf(s_enh_hammer_speed_label, sizeof(s_enh_hammer_speed_label), "Hammer Speed: %s",
			 s_enh_speed_names[s_enh_hammer_speed]);
	s_enhancements_items[ENH_HAMMER_SPEED] = s_enh_hammer_speed_label;
	snprintf(s_enh_tombstone_label, sizeof(s_enh_tombstone_label), "Tombstone: %s",
			 s_enh_tombstone ? "On" : "Off");
	s_enhancements_items[ENH_TOMBSTONE] = s_enh_tombstone_label;
	snprintf(s_enh_mirror_mode_label, sizeof(s_enh_mirror_mode_label), "Mirror Mode: %s",
			 s_enh_mirror_mode ? "On" : "Off");
	s_enhancements_items[ENH_MIRROR_MODE] = s_enh_mirror_mode_label;
	snprintf(s_enh_hourglass_label, sizeof(s_enh_hourglass_label), "Hourglass: %s",
			 s_enh_hourglass ? "On" : "Off");
	s_enhancements_items[ENH_HOURGLASS] = s_enh_hourglass_label;
}

static void refresh_cheats_labels(void) {
	snprintf(s_cheat_keys_label, sizeof(s_cheat_keys_label), "Unlimited Keys: %s",
			 s_cheat_unlimited_keys ? "On" : "Off");
	s_cheats_items[CHEATS_UNLIMITED_KEYS] = s_cheat_keys_label;
	// wootbeer: "in the cheats menu I want to change the text 'god mode (invincible)' to just 'god
	// mode'" -- cosmetic label change only, s_cheat_god_mode/CHEATS_GOD_MODE and every other
	// identifier stay exactly as they were.
	snprintf(s_cheat_god_label, sizeof(s_cheat_god_label), "God Mode: %s",
			 s_cheat_god_mode ? "On" : "Off");
	s_cheats_items[CHEATS_GOD_MODE] = s_cheat_god_label;
	snprintf(s_cheat_jewels_label, sizeof(s_cheat_jewels_label), "Unlimited Jewels: %s",
			 s_cheat_unlimited_jewels ? "On" : "Off");
	s_cheats_items[CHEATS_UNLIMITED_JEWELS] = s_cheat_jewels_label;
	snprintf(s_cheat_magic_label, sizeof(s_cheat_magic_label), "Unlimited Magic: %s",
			 s_cheat_unlimited_magic ? "On" : "Off");
	s_cheats_items[CHEATS_UNLIMITED_MAGIC] = s_cheat_magic_label;
	snprintf(s_cheat_all_items_label, sizeof(s_cheat_all_items_label), "All Items: %s",
			 s_cheat_all_items ? "On" : "Off");
	s_cheats_items[CHEATS_ALL_ITEMS] = s_cheat_all_items_label;
}

static int current_item_count(void) {
	switch (s_state) {
		case MENU_QUIT_CONFIRM: return QUIT_NUM_ITEMS;
		case MENU_QUIT_SAVE_CONFIRM: return YESNO_NUM_ITEMS;
		case MENU_SOUND_MUSIC: return SOUND_MUSIC_NUM_ITEMS;
		case MENU_TOUCH_OPTIONS: return TOUCH_OPTIONS_NUM_ITEMS;
		case MENU_GAMEPAD_REMAP: return GAMEPAD_REMAP_NUM_ITEMS;
		case MENU_GAMEPAD_CAPTURE: return 2;
		case MENU_SKILL: return SKILL_NUM_ITEMS;
		case MENU_DISPLAY: return DISPLAY_NUM_ITEMS;
		case MENU_ENHANCEMENTS: return ENHANCEMENTS_NUM_ITEMS;
		case MENU_CHEATS: return CHEATS_NUM_ITEMS;
		case MENU_SAVE_CONFIRM: return YESNO_NUM_ITEMS;
		case MENU_LOAD_CONFIRM: return YESNO_NUM_ITEMS;
		default: return TOP_NUM_ITEMS;
	}
}

static const char **current_items(void) {
	switch (s_state) {
		case MENU_QUIT_CONFIRM: return s_quit_items;
		case MENU_QUIT_SAVE_CONFIRM: return s_yesno_items;
		case MENU_SOUND_MUSIC: return s_sound_music_items;
		case MENU_TOUCH_OPTIONS: return s_touch_options_items;
		case MENU_GAMEPAD_REMAP: return s_gamepad_remap_items;
		case MENU_GAMEPAD_CAPTURE: return s_gamepad_capture_items;
		case MENU_SKILL: return s_skill_items;
		case MENU_DISPLAY: return s_display_items;
		case MENU_ENHANCEMENTS: return s_enhancements_items;
		case MENU_CHEATS: return s_cheats_items;
		case MENU_SAVE_CONFIRM: return s_yesno_items;
		case MENU_LOAD_CONFIRM: return s_yesno_items;
		default: return s_top_items;
	}
}

static const char *current_title(void) {
	switch (s_state) {
		case MENU_QUIT_CONFIRM: return "Quit Game?";
		case MENU_QUIT_SAVE_CONFIRM: return "Save Game?"; // wootbeer: "on the new sub-menu I'd like the
				// 'Save Before Quitting?' message to simply say 'Save Game?'" -- same title
				// MENU_SAVE_CONFIRM's own ordinary in-game Save Game row already uses (see that
				// case just below); the two states stay otherwise distinct (different s_state,
				// different consequences on "Yes"), this just makes their wording match.
		case MENU_SOUND_MUSIC: return "Sound/Music";
		case MENU_TOUCH_OPTIONS: return "Touch Options";
		case MENU_GAMEPAD_REMAP: return "Remap Gamepad";
		case MENU_GAMEPAD_CAPTURE: return s_gamepad_capture_title;
		case MENU_SKILL: return "Set Skill Level";
		case MENU_DISPLAY: return "Set Display Mode";
		case MENU_ENHANCEMENTS: return "Enhancements";
		case MENU_CHEATS: return "Cheat Codes";
		case MENU_SAVE_CONFIRM: return "Save Game?";
		case MENU_LOAD_CONFIRM: return "Load Game?";
		default: return "Options Menu";
	}
}

bool got_menu_is_open(void) {
	return s_state != MENU_CLOSED;
}

// Closes the menu entirely and restores the live game view -- GOT_PAGE2 is the room's own
// continuously-maintained clean background (tiles + any door/pickup state changes since the room
// loaded, see got_erase_door()/got_pick_up_object()'s own writes to it), so one full-frame copy
// back into GOT_PAGE0 wipes the menu box out completely and leaves exactly the correct current
// background -- the same restore mechanism the real game's own `restore_screen=1` flag triggers,
// just done immediately here instead of deferring to the next frame's redraw pass.
//
// Also force-clears key_flag[KEY_FIRE] here -- real select_option()'s own accepted confirm keys
// (ENTER/SPACE/key_fire/key_magic) don't cause this problem on real DOS hardware, where a keyboard
// tap is already released again long before the next frame; this port's touch FIRE button (and a
// held gamepad face button) can still legitimately read as "held" on the very next render call
// after Resume/Continue closes the menu, and got_move_thor()'s own hammer-throw check
// (`!s_hammer.used && key_flag[KEY_FIRE]`) isn't edge-triggered -- so without this, confirming with
// FIRE throws Thor's hammer the instant gameplay resumes, purely because the flag never got a
// chance to see a 0 in between. wootbeer caught this ("when I exit the menu the hammer throws at the
// same time").
static void got_menu_close(void) {
	s_state = MENU_CLOSED;
	key_flag[KEY_FIRE] = 0;
	xcopyd2d(0, 0, 320, 192, 0, 0, GOT_PAGE2, GOT_PAGE0, 320, 320);
}

// Re-syncs every edge-detected s_prev_* flag to whatever key_flag[] actually reads RIGHT NOW --
// called whenever MENU_GAMEPAD_CAPTURE hands control back to MENU_GAMEPAD_REMAP (both here, for the
// Start/ESC-cancels-capture path, and from got_menu_update()'s own capture-poll block for a
// successfully-resolved capture). While capturing, got_menu_update() returns before ever touching
// its own Up/Down/Fire/Cancel/Confirm/Left/Right s_prev_* pairs (that machinery isn't used at all
// during capture -- see that function's own comment), so those flags simply freeze at whatever they
// last were BEFORE capture began. wootbeer: "if I push 'b' when remapping a button to b, it closes the
// menu" -- exactly this: bind an action to B, and B is still physically held the instant the capture
// resolves; s_prev_cancel was last `false` (from before capture started), so the very next frame back
// in MENU_GAMEPAD_REMAP reads B's still-held KEY_CANCEL as a brand new press and immediately backs
// back out of the row list that was just used to bind it. Calling this once, right at the transition,
// keeps every prev-state flag truthful instead of stale, so a button that's simply still held from
// the capture itself can never misread as a fresh edge the moment normal navigation resumes.
static void resync_menu_edge_state(void) {
	s_prev_up = key_flag[KEY_UP] != 0;
	s_prev_down = key_flag[KEY_DOWN] != 0;
	s_prev_fire = key_flag[KEY_FIRE] != 0;
	s_prev_cancel = key_flag[KEY_CANCEL] != 0;
	s_prev_confirm = key_flag[KEY_CONFIRM] != 0;
	s_prev_left = key_flag[KEY_LEFT] != 0;
	s_prev_right = key_flag[KEY_RIGHT] != 0;
}

// Backs out of whichever submenu is open, back to the top-level menu, landing on the row that opened
// it -- matching real ESC-cancels-select_option() behavior. Shared by both cancel paths that can
// trigger it: ESC/Start (got_menu_poll_toggle_key() below, its own original home) and now B/
// KEY_CANCEL (got_menu_update() below, new this round -- see that function's own comment). Callers
// are each responsible for only calling this when a submenu is actually open (`s_state != MENU_TOP`,
// on top of the obvious `!= MENU_CLOSED`) -- there's no "back" from the top level itself here, ESC/
// Resume already close the whole menu from there instead.
static void got_menu_cancel_submenu(void) {
	MenuState from = s_state;
	// MENU_QUIT_SAVE_CONFIRM is the one exception to this function's usual "every submenu sits
	// exactly one level below MENU_TOP" assumption -- it sits one level below MENU_QUIT_CONFIRM
	// instead (see that state's own comment, above, on why it exists at all). Cancelling it backs out
	// only that one level, landing back on whichever "Quit to ..." row opened it
	// (s_pending_quit_action, set right before this state is ever entered) -- the same one-level-at-a-
	// time cancel shape real nested select_option() menus have, rather than jumping straight past
	// MENU_QUIT_CONFIRM back to MENU_TOP the way every other (single-level) submenu here does below.
	if (from == MENU_QUIT_SAVE_CONFIRM) {
		s_state = MENU_QUIT_CONFIRM;
		s_selected = s_pending_quit_action;
		return;
	}
	// MENU_GAMEPAD_CAPTURE is this function's other one-level-at-a-time exception, same shape as
	// MENU_QUIT_SAVE_CONFIRM just above -- ESC/Start while capturing (got_menu_poll_toggle_key()
	// routes here for any non-MENU_TOP/MENU_CLOSED state) backs out only to the Remap Gamepad row
	// list, landing back on whichever action row opened the capture (s_gp_capture_action), not all
	// the way out to MENU_TOP -- and clears the native capturing flag so a stray late press can't
	// still land a binding after the screen's moved on.
	if (from == MENU_GAMEPAD_CAPTURE) {
		s_state = MENU_GAMEPAD_REMAP;
		s_selected = s_gp_capture_action;
		got_gamepad_remap_cancel_capture();
		resync_menu_edge_state(); // see this function's own comment -- Start/ESC itself doesn't
								  // leave a stale edge, but whatever the user was mid-press
								  // capturing might still be held
		return;
	}
	s_state = MENU_TOP;
	switch (from) {
		case MENU_QUIT_CONFIRM: s_selected = TOP_QUIT; break;
		case MENU_SOUND_MUSIC: s_selected = TOP_SOUND_MUSIC; break;
		case MENU_TOUCH_OPTIONS: s_selected = TOP_TOUCH_OPTIONS; break;
		case MENU_GAMEPAD_REMAP: s_selected = TOP_TOUCH_OPTIONS; break; // staging discarded, see its
																		 // own comment
		case MENU_SKILL: s_selected = TOP_SKILL; break;
		case MENU_DISPLAY: s_selected = TOP_DISPLAY; break;
		case MENU_ENHANCEMENTS: s_selected = TOP_ENHANCEMENTS; break;
		case MENU_CHEATS: s_selected = TOP_CHEATS; break;
		case MENU_SAVE_CONFIRM: s_selected = TOP_SAVE; break;
		case MENU_LOAD_CONFIRM: s_selected = TOP_LOAD; break;
		default: s_selected = 0; break;
	}
}

// Actually performs one of the two quit rows -- split out of dispatch_confirm()'s own
// MENU_QUIT_CONFIRM case (below) so both the "No" (don't save) and "Yes" (save first) paths out of
// the new MENU_QUIT_SAVE_CONFIRM prompt can share it verbatim, rather than each duplicating this
// exact sequence.
static void perform_quit_action(int action) {
	if (action == QUIT_TO_DOS) { // real Quit-to-DOS behavior -- this port's actual process exit.
		exit(0);
	} else { // QUIT_TO_OPENING_SCREEN -- see the original MENU_QUIT_CONFIRM case's own long comment
			 // (still just below) for why this is a got_title_return_to_menu() hand-off instead of
			 // exit(0), and why it skips got_menu_close()'s own GOT_PAGE2 room-restore copy.
		s_state = MENU_CLOSED;
		key_flag[KEY_FIRE] = 0;
		got_title_return_to_menu();
	}
}

static void dispatch_confirm(void) {
	int i;

	if (s_state == MENU_TOP) {
		switch (s_selected) {
			case TOP_RESUME:
				got_menu_close();
				break;
			case TOP_SOUND_MUSIC: // Sound/Music -> submenu (Sound Effects/Music toggles moved in here,
								   // plus the two new gain sliders -- see MENU_SOUND_MUSIC's own
								   // declaration comment, above).
				s_state = MENU_SOUND_MUSIC;
				s_selected = 0;
				refresh_sound_music_labels();
				break;
			case TOP_TOUCH_OPTIONS: // Same row, two different destinations -- see that row's own
									 // enum comment, above. A connected gamepad means there's no
									 // on-screen D-pad to configure, so this goes to "Remap Gamepad"
									 // instead of "Touch Options" in that case.
				if (got_controls_gamepad_connected()) {
					s_state = MENU_GAMEPAD_REMAP;
					s_selected = 0;
					for (i = 0; i < GAMEPAD_NUM_ACTIONS; i++) {
						s_gp_staging[i] = got_gamepad_remap_bound_keycode(i);
					}
					refresh_gamepad_remap_labels();
				} else {
					s_state = MENU_TOUCH_OPTIONS;
					s_selected = 0;
					refresh_touch_options_labels();
				}
				break;
			case TOP_SKILL: // Skill Level -> submenu, pre-selected on the CURRENT skill (real
							 // select_skill(): `sel=setup.skill;`)
				s_state = MENU_SKILL;
				s_selected = s_skill_level;
				break;
			case TOP_DISPLAY: // Display Mode -> submenu, pre-selected on the CURRENT mode -- same
							   // "cursor position is the current value" shape as Skill Level above
							   // (no real select_*() to mirror -- see got_menu.h's own top comment).
				s_state = MENU_DISPLAY;
				s_selected = s_display_mode;
				break;
			case TOP_SAVE: // Save Game -> Yes/No confirm submenu, real: `select_option(options_yesno,
						   // "Save Game?",0)`
				s_state = MENU_SAVE_CONFIRM;
				s_selected = 0;
				break;
			case TOP_LOAD: // Load Game -> Yes/No confirm submenu, real: `select_option(options_yesno,
						   // "Load Game?",0)`
				s_state = MENU_LOAD_CONFIRM;
				s_selected = 0;
				break;
			case TOP_DIE: // Die -- immediate, no confirmation submenu (real: `else if(opt==5){ if
						  // (!game_over) thor_dies(); }` -- no game_over concept exists in this
						  // port yet, see got_menu_action_die()'s own comment, so no guard needed)
				got_menu_close();
				got_menu_action_die();
				break;
			case TOP_ENHANCEMENTS: // Enhancements -> submenu
				s_state = MENU_ENHANCEMENTS;
				s_selected = 0;
				refresh_enhancements_labels();
				break;
			case TOP_CHEATS: // Cheat Codes -> submenu
				s_state = MENU_CHEATS;
				s_selected = 0;
				refresh_cheats_labels();
				break;
			case TOP_HELP: // Help -- immediate, real: `else if(opt==7) help();`
				got_menu_close();
				got_menu_action_help();
				break;
			case TOP_QUIT: // Quit Game -> confirm submenu
				s_state = MENU_QUIT_CONFIRM;
				s_selected = 0;
				break;
			default:
				break;
		}
	} else if (s_state == MENU_QUIT_CONFIRM) {
		switch (s_selected) {
			case QUIT_CONTINUE: // back to the top-level menu, selection on "Quit Game"
				s_state = MENU_TOP;
				s_selected = TOP_QUIT;
				break;
			case QUIT_TO_OPENING_SCREEN: // wootbeer: "we do need to add the 'return to home screen'
					// option from the pause/options menu in game, it currently just closes the
					// app." It really did: this used to call exit(0) despite its own label already
					// saying "Home Screen" (a real Activity.finish() round-trip would have needed a
					// Java-side method this port hadn't added, so the process-exit net effect of
					// real "Quit to DOS" was used as a stand-in). Now that got_title.c has a real
					// main menu to land on (see that file's own GOT_APP_TITLE), this drops straight
					// back to it in-process instead -- same s_state=MENU_CLOSED/key_flag[KEY_FIRE]=0
					// bleed-through guard got_menu_close() below already uses (so the new main
					// menu's own FIRE edge-detect doesn't read this same still-held press as an
					// instant "Play Game"), just without the GOT_PAGE2 room-restore copy that
					// wouldn't mean anything once there's no room left to restore. (Renamed from
					// "Quit to Home Screen" to "Quit to Opening Screen" per wootbeer's own follow-up,
					// to match got_title.c's own terminology for that screen.)
					//
					// Follow-up -- wootbeer: "there should be a pop-up asking the player if they want to
					// save there game either when exiting to main screen or exiting to 'dos'." No
					// real precedent (see s_pending_quit_action's own comment, above, for the
					// ask_exit()/1_main.c research confirming that) -- this now detours through the
					// new MENU_QUIT_SAVE_CONFIRM prompt instead of performing the quit immediately;
					// perform_quit_action() (just above) holds the actual quit sequence quoted in
					// this comment, now shared with that prompt's own "Yes"/"No" handling below.
				s_pending_quit_action = QUIT_TO_OPENING_SCREEN;
				s_state = MENU_QUIT_SAVE_CONFIRM;
				s_selected = 0;
				break;
			case QUIT_TO_DOS: // wootbeer: "also we need to add the 'Quit to DOS' option, that will
					// close the game entirely" -- the real process-exit behavior "...Opening
					// Screen" used to have before this split, now its own explicit row instead of
					// being what "...Opening Screen" silently did. Same save-prompt follow-up as
					// QUIT_TO_OPENING_SCREEN just above.
				s_pending_quit_action = QUIT_TO_DOS;
				s_state = MENU_QUIT_SAVE_CONFIRM;
				s_selected = 0;
				break;
			default:
				break;
		}
	} else if (s_state == MENU_QUIT_SAVE_CONFIRM) {
		switch (s_selected) {
			case 0: // Yes -- save to the current slot (same got_menu_action_save() MENU_SAVE_CONFIRM's
					// own "Yes" case below already uses; it also queues Odin's own "your game has been
					// saved" dialogue, real save_game()'s own `odin_speaks(2009,0);`), same as an
					// ordinary Save Game does. wootbeer caught a bug in this feature's first version: it
					// called perform_quit_action() immediately after saving, right here, which tore the
					// game view down (or exited outright) before that dialogue was ever actually shown
					// -- and since neither quit path resets the script engine's own "a script is
					// queued" state either, it just sat there armed and got serviced anyway, the next
					// time this exact save was loaded and gameplay resumed ("odin pops up and says
					// 'your game has been saved.' right after loading and restarting play"). Fixed by
					// closing the menu here (got_menu_close(), same as MENU_SAVE_CONFIRM's own "Yes"
					// case -- restores the live game view so the dialogue has something to draw over)
					// and handing the actual quit off to got_menu_quit_after_save() (got_main.c) instead
					// of calling perform_quit_action() directly -- that function only performs it once
					// got_show_render_buffer()'s own per-frame dispatch sees the dialogue's script has
					// actually finished running, the same deferred shape that file's own
					// s_episode_complete_pending already established for the Episode-1 congratulations
					// dialogue.
				got_menu_close();
				got_menu_action_save();
				got_menu_quit_after_save(s_pending_quit_action == QUIT_TO_DOS);
				break;
			case 1: // No -- no save, so no dialogue to wait for -- perform the pending quit action
					// immediately, same as before.
				perform_quit_action(s_pending_quit_action);
				break;
			default:
				break;
		}
	} else if (s_state == MENU_SOUND_MUSIC) {
		// Fire only does something on the two toggle rows -- the two gain rows are Left/Right-adjusted
		// in place instead (got_menu_update()'s own comment), same as a real select_option() row that
		// isn't currently confirm-able does nothing on Fire. Stays in the submenu either way, same
		// in-place-toggle shape as Enhancements/Cheats above.
		switch (s_selected) {
			case SOUND_MUSIC_SOUND_TOGGLE:
				s_sound_enabled = !s_sound_enabled;
				break;
			case SOUND_MUSIC_MUSIC_TOGGLE:
				s_music_enabled = !s_music_enabled;
				got_music_set_enabled_from_menu(s_music_enabled);
				break;
			default:
				break;
		}
		refresh_sound_music_labels();
	} else if (s_state == MENU_GAMEPAD_REMAP) {
		// Reset/Cancel/Apply, matching wootbeer's own explicit request ("the menu should have all
		// the same options, apply, cancel, default") -- see s_gp_staging[]'s own comment for why
		// this screen edits a staged copy instead of applying in place like every other submenu
		// here. Any other row is one of the GAMEPAD_NUM_ACTIONS action rows -- enter the capture
		// step for it.
		if (s_selected == GAMEPAD_REMAP_ROW_RESET) {
			for (i = 0; i < GAMEPAD_NUM_ACTIONS; i++) {
				s_gp_staging[i] = got_gamepad_remap_default_keycode(i);
			}
			refresh_gamepad_remap_labels();
		} else if (s_selected == GAMEPAD_REMAP_ROW_CANCEL) {
			s_state = MENU_TOP; // staging simply discarded -- never touched the live table
			s_selected = TOP_TOUCH_OPTIONS;
		} else if (s_selected == GAMEPAD_REMAP_ROW_APPLY) {
			for (i = 0; i < GAMEPAD_NUM_ACTIONS; i++) {
				got_gamepad_remap_set_bound_keycode(i, s_gp_staging[i]);
			}
			got_config_save();
			s_state = MENU_TOP;
			s_selected = TOP_TOUCH_OPTIONS;
		} else {
			s_gp_capture_action = s_selected;
			snprintf(s_gamepad_capture_title, sizeof(s_gamepad_capture_title), "Rebind: %s",
					 got_gamepad_remap_action_label(s_gp_capture_action));
			s_state = MENU_GAMEPAD_CAPTURE;
			// s_gp_capture_action (just captured above) remembers WHICH action row this is for;
			// s_selected itself resets to 0 so the cursor consistently lands on this screen's own
			// first line regardless of which action row (Fire/Magic/Select Item -- index 0/1/2)
			// opened it, rather than silently not matching either of this screen's own 2 rows
			// whenever a non-zero action index carried straight over.
			s_selected = 0;
			got_gamepad_remap_start_capture();
		}
	} else if (s_state == MENU_SKILL) {
		// Any of the three rows: apply and return to the top menu, selection back on "Skill
		// Level" -- matching real select_skill()'s own `if(ret) setup.skill=ret-1;` then falling
		// straight back out to option_menu()'s own caller (no separate confirm step).
		s_skill_level = s_selected;
		s_state = MENU_TOP;
		s_selected = TOP_SKILL;
		refresh_top_labels(); // the top row's own inline "Skill Level: <name>" needs refreshing
	} else if (s_state == MENU_DISPLAY) {
		// Any of the three rows: apply and return to the top menu, same immediate-apply/no-confirm
		// shape as MENU_SKILL just above -- got_main.c's render loop picks this up the very next
		// frame (it reads got_menu_display_mode() fresh every frame, see that function's own
		// header comment), so there's nothing further to notify here.
		s_display_mode = s_selected;
		s_state = MENU_TOP;
		s_selected = TOP_DISPLAY;
		refresh_top_labels(); // the top row's own inline "Display Mode: <name>" needs refreshing
	} else if (s_state == MENU_ENHANCEMENTS) {
		switch (s_selected) {
			case ENH_HAMMER_COLOR:
				s_enh_hammer_color = (s_enh_hammer_color + 1) % 4; // Default -> I -> II -> III -> Default
				got_refresh_player_sprite_colors(); // takes effect immediately, not just on next load
				break;
			case ENH_ARMOR_COLOR:
				s_enh_armor_color = (s_enh_armor_color + 1) % 4;
				got_refresh_player_sprite_colors();
				break;
			case ENH_ATK_SPEED:
				s_enhancement_fast_enemy_shots = !s_enhancement_fast_enemy_shots;
				break;
			case ENH_BOSS_SPEED:
				s_enh_boss_speed = (s_enh_boss_speed + 1) % 3; // Default -> Slow -> Fast -> Default
				break;
			case ENH_HAMMER_SPEED:
				s_enh_hammer_speed = (s_enh_hammer_speed + 1) % 3;
				break;
			case ENH_TOMBSTONE:
				s_enh_tombstone = !s_enh_tombstone;
				break;
			case ENH_MIRROR_MODE:
				s_enh_mirror_mode = !s_enh_mirror_mode;
				break;
			case ENH_HOURGLASS:
				s_enh_hourglass = !s_enh_hourglass;
				break;
			default:
				break;
		}
		refresh_enhancements_labels(); // stay in the submenu -- same in-place-toggle shape as Sound
										// Effects/Music at the top level and the Cheat Codes submenu
	} else if (s_state == MENU_CHEATS) {
		switch (s_selected) {
			case CHEATS_UNLIMITED_KEYS:
				s_cheat_unlimited_keys = !s_cheat_unlimited_keys;
				break;
			case CHEATS_GOD_MODE:
				s_cheat_god_mode = !s_cheat_god_mode;
				break;
			case CHEATS_UNLIMITED_JEWELS:
				s_cheat_unlimited_jewels = !s_cheat_unlimited_jewels;
				break;
			case CHEATS_UNLIMITED_MAGIC:
				s_cheat_unlimited_magic = !s_cheat_unlimited_magic;
				break;
			case CHEATS_ALL_ITEMS:
				s_cheat_all_items = !s_cheat_all_items;
				got_cheat_set_all_items(s_cheat_all_items); // one-shot grant/revoke, see that
															 // function's own comment
				break;
			default:
				break;
		}
		refresh_cheats_labels(); // stay in the submenu -- all five rows toggle in place, same shape
								  // as Sound Effects/Music at the top level
	} else if (s_state == MENU_SAVE_CONFIRM) {
		switch (s_selected) {
			case 0: // Yes -- real: `if(select_option(options_yesno,"Save Game?",0)!=1){ ...return; }`
					// (index 0 == "Yes" == real return value 1, see s_yesno_items' own comment)
				got_menu_close();
				got_menu_action_save();
				break;
			case 1: // No -- back to the top-level menu, selection on "Save Game"
				s_state = MENU_TOP;
				s_selected = TOP_SAVE;
				break;
			default:
				break;
		}
	} else if (s_state == MENU_LOAD_CONFIRM) {
		switch (s_selected) {
			case 0: // Yes -- real: `if(select_option(options_yesno,"Load Game?",0)!=1){ ...return 0; }`
				got_menu_close();
				got_menu_action_load();
				break;
			case 1: // No -- back to the top-level menu, selection on "Load Game"
				s_state = MENU_TOP;
				s_selected = TOP_LOAD;
				break;
			default:
				break;
		}
	}
}

void got_menu_poll_toggle_key(void) {
	bool esc = key_flag[KEY_ESC] != 0;
	if (esc && !s_prev_esc) {
		if (s_state == MENU_CLOSED) {
			s_state = MENU_TOP;
			s_selected = 0;
			refresh_top_labels();
		} else if (s_state == MENU_TOP) {
			got_menu_close();
		} else {
			got_menu_cancel_submenu();
		}
		key_flag[KEY_ESC] = 0; // real: `key_flag[ESC]=0;` right after being read
	}
	s_prev_esc = esc;
}

void got_menu_update(void) {
	bool up, down, fire, cancel, confirm, left, right;
	int count;

	if (s_state == MENU_CLOSED) {
		return;
	}
	// Re-derive the top-level list fresh every frame the menu sits at MENU_TOP -- TOP_TOUCH_OPTIONS'
	// own CONTENT (got_controls_gamepad_connected()) can change the instant a gamepad connects/
	// disconnects while the player is just sitting on the paused top-level menu (got_controls.c's
	// own D-pad overlay already appears/disappears live the same way) -- without this, that row's
	// own label text could go briefly stale. Cheap: only a handful of snprintf() calls, and only
	// while actually paused at the top level, never during gameplay.
	if (s_state == MENU_TOP) {
		refresh_top_labels();
	}
	// MENU_GAMEPAD_CAPTURE doesn't use any of the generic Up/Down/Fire/Cancel/Left/Right machinery
	// below at all -- got_controls.c's own gamepadButtonRaw() (fed by GotView.java's raw forwarding
	// for the 7 capturable buttons) is this screen's entire input path, polled here once per frame.
	// A resolved capture (a real button, not GAMEPAD_UNBOUND) stages the new binding, "steals" it
	// away from any other action currently holding it (same as Descore's own capture step), and
	// drops back to the row list on the action just rebound; an unresolved capture just waits
	// another frame. Cancelling out of a capture (Start/ESC) is handled separately, by
	// got_menu_poll_toggle_key()'s own existing MENU_CLOSED/MENU_TOP fallthrough into
	// got_menu_cancel_submenu() -- see that function's own new MENU_GAMEPAD_CAPTURE case.
	if (s_state == MENU_GAMEPAD_CAPTURE) {
		int captured = got_gamepad_remap_poll_capture();
		if (captured != GAMEPAD_UNBOUND) {
			int i;
			s_gp_staging[s_gp_capture_action] = captured;
			for (i = 0; i < GAMEPAD_NUM_ACTIONS; i++) {
				if (i != s_gp_capture_action && s_gp_staging[i] == captured) {
					s_gp_staging[i] = GAMEPAD_UNBOUND;
				}
			}
			got_gamepad_remap_cancel_capture(); // clears the native capturing flag; poll already
												 // consumed the one-shot captured value itself
			refresh_gamepad_remap_labels();
			s_state = MENU_GAMEPAD_REMAP;
			s_selected = s_gp_capture_action;
			resync_menu_edge_state(); // see that function's own comment -- wootbeer's own "press B to
									  // bind B" report, fixed here
		}
		return;
	}
	up = key_flag[KEY_UP] != 0;
	down = key_flag[KEY_DOWN] != 0;
	fire = key_flag[KEY_FIRE] != 0;
	cancel = key_flag[KEY_CANCEL] != 0;
	// A face button's own dedicated, never-remapped confirm signal -- see KEY_CONFIRM's own #define
	// comment for why this needs to exist alongside `fire` above (which still ALSO confirms, driven
	// by whatever's currently bound to Fire -- this is additive, not a replacement).
	confirm = key_flag[KEY_CONFIRM] != 0;
	left = key_flag[KEY_LEFT] != 0;
	right = key_flag[KEY_RIGHT] != 0;
	count = current_item_count();

	if (up && !s_prev_up) {
		s_selected = (s_selected - 1 + count) % count;
	}
	if (down && !s_prev_down) {
		s_selected = (s_selected + 1) % count;
	}
	// Sound/Music submenu's two gain sliders -- wootbeer: "each one will be an option with a slider similar
	// to descore to set the audio/gain level" -- adjusted in place with Left/Right rather than Fire (see
	// dispatch_confirm()'s own MENU_SOUND_MUSIC comment), same edge-triggered shape as Up/Down above so
	// a held key doesn't repeat every single frame. Read unconditionally like every other s_prev_* pair
	// in this function, gated on state/row only where the value actually changes. The actual clamp/
	// apply/refresh/persist all live in got_menu_set_sound_gain()/got_menu_set_music_gain() (got_menu.h)
	// now, not here -- this just decides WHETHER a change happened and by how much, same division of
	// labor got_menu_set_skill_level() etc. already established; only called when the row's own current
	// value can actually move, so a held key at either end doesn't spam got_config_save() every frame
	// with the same value.
	if (s_state == MENU_SOUND_MUSIC &&
		(s_selected == SOUND_MUSIC_SOUND_GAIN || s_selected == SOUND_MUSIC_MUSIC_GAIN)) {
		bool is_sound = (s_selected == SOUND_MUSIC_SOUND_GAIN);
		int gain = is_sound ? s_sound_gain : s_music_gain;

		if (left && !s_prev_left && gain > 0) {
			if (is_sound) {
				got_menu_set_sound_gain(gain - 1);
			} else {
				got_menu_set_music_gain(gain - 1);
			}
		} else if (right && !s_prev_right && gain < SOUND_MUSIC_GAIN_STEPS) {
			if (is_sound) {
				got_menu_set_sound_gain(gain + 1);
			} else {
				got_menu_set_music_gain(gain + 1);
			}
		}
	}
	// Touch Options submenu's own two sliders -- same Left/Right-in-place shape as Sound/Music's
	// gain rows just above (see that block's own comment), just against got_menu_set_touch_scale_
	// step()/got_menu_set_touch_opacity_step() (got_menu.h) instead.
	if (s_state == MENU_TOUCH_OPTIONS &&
		(s_selected == TOUCH_OPTIONS_SCALING || s_selected == TOUCH_OPTIONS_OPACITY)) {
		bool is_scale = (s_selected == TOUCH_OPTIONS_SCALING);
		int value = is_scale ? s_touch_scale_step : s_touch_opacity_step;
		int max_value = is_scale ? (TOUCH_SCALE_NUM_STEPS - 1) : SOUND_MUSIC_GAIN_STEPS;

		if (left && !s_prev_left && value > 0) {
			if (is_scale) {
				got_menu_set_touch_scale_step(value - 1);
			} else {
				got_menu_set_touch_opacity_step(value - 1);
			}
		} else if (right && !s_prev_right && value < max_value) {
			if (is_scale) {
				got_menu_set_touch_scale_step(value + 1);
			} else {
				got_menu_set_touch_opacity_step(value + 1);
			}
		}
	}
	// wootbeer: "need 'b' button to back out of cheat code menu, not toggle, need for all menus." Root
	// cause: GotView.java's gotScancode()/gotExtraScancode() send BOTH key_flag[KEY_FIRE] and
	// key_flag[KEY_CANCEL] the instant B is pressed, and this function only ever read KEY_FIRE, so a
	// B press always ran dispatch_confirm() exactly as if A had been pressed -- toggling a cheat,
	// entering Skill/Display, etc., instead of backing out. Fixed the same way got_title.c's own
	// GOT_APP_PLAYER_SELECT/GOT_APP_DELETE_CONFIRM cases already fixed the identical ambiguity there:
	// check cancel FIRST and let it win outright, so a single B press can't also fall through into
	// whatever action FIRE would have triggered the same frame.
	//
	// Follow-up -- wootbeer: "except for on the pause/options menu itself, the 'b' button still selects
	// options, instead of backing out to the game." Right -- the first pass only handled B below the
	// top level (there being nothing an actual submenu to "back out" TO from MENU_TOP itself), on the
	// assumption A and B should keep behaving identically at the top level the way they always had.
	// But "back out" at the top level of THIS menu means back out to the game, exactly what Resume/
	// ESC/Start already do from here -- so B backing out one level further, same as it does from every
	// submenu, is exactly the consistent behavior wootbeer's original "need for all menus" ask meant, this
	// screen included. B at MENU_TOP now calls got_menu_close() (same real function Resume/ESC/Start
	// already use to leave the menu entirely) instead of falling through to dispatch_confirm().
	if (cancel && !s_prev_cancel) {
		if (s_state == MENU_TOP) {
			got_menu_close();
		} else {
			got_menu_cancel_submenu();
		}
	} else if ((fire && !s_prev_fire) || (confirm && !s_prev_confirm)) {
		// Either edge -- whatever's currently bound to Fire (unaffected by this round's gamepad
		// remapper) or A's own always-on KEY_CONFIRM (new this round, see that #define's own
		// comment) -- confirms the highlighted row. Redundant when Fire happens to still be bound to
		// A, harmless either way.
		dispatch_confirm();
	}
	s_prev_up = up;
	s_prev_down = down;
	s_prev_fire = fire;
	s_prev_cancel = cancel;
	s_prev_confirm = confirm;
	s_prev_left = left;
	s_prev_right = right;
}

// Direct port of real select_option()'s own box-sizing/border/text layout math (1_panel.c), just
// run once per frame instead of once per blocking-loop entry, and with a ">" cursor next to the
// selected item standing in for the real animated hammer-icon cursor (hampic[4]) -- that sprite
// resource hasn't been loaded/verified for this port, and a simple text cursor built on the font
// this file already has is a smaller, self-contained substitute, in the same spirit as got_hud.c's
// own digit renderer standing in for the real status panel.
void got_menu_draw(void) {
	const char **items;
	const char *title;
	const unsigned char *bpics;
	int count, i;
	int w, h, x1, y1, x2, y2, s;
	int tw;
	int visible, scroll;

	if (s_state == MENU_CLOSED) {
		return;
	}
	++s_cursor_frame_counter;

	// Restore the room's clean background UNDER the whole box before laying anything out this
	// frame -- wootbeer: "when changing [Turbo Mode's Scroll Rooms toggle] from no to yes the size of
	// the dialogue box changes, but the game is continuing to show the draw of the older larger
	// dialogue box 'underneath', this creates a tessellated look." (Turbo Mode itself was later
	// removed entirely -- see got_menu.h's own comment on why -- but this fix is general, not
	// specific to that one toggle, and still protects every in-place toggle row this menu has left,
	// e.g. Sound Effects/Music and the Cheat Codes submenu.) Root cause: this function's own
	// box-sizing math below measures whichever label text is CURRENTLY in `items[]` -- for an
	// in-place toggle row that's literally the current on-screen text, not a fixed set of
	// real-source strings the way real select_option()'s own box math effectively is (every real
	// submenu's own option array holds every choice's full text at once, e.g. real options_slow[]'s
	// two full strings side by side, so real box width is stable for that submenu's whole lifetime).
	// Flipping "Scroll Rooms: No" (18 chars) to "Scroll Rooms: Yes" (19) -- or Sound/Music's own
	// On<->Off -- can shrink or grow the box between one frame and the next, and this function only
	// ever drew the
	// box/border/text fresh on top of whatever GOT_PAGE0 already held, never erasing the wider
	// previous frame's own edges first -- exactly the same GOT_PAGE2-is-the-clean-backdrop restore
	// got_menu_close() already does once, on exit, just run every frame the menu is open instead of
	// only at the end, so a still-open, still-resizing submenu can never show two overlapping box
	// outlines at once.
	//
	// wootbeer (episode-2 round, following up on the item picker's own identical bug -- see
	// got_item_menu_draw()'s own comment): the pause menu has the same latent issue, just less
	// obviously -- this restore erases every sprite drawn on top of the clean backdrop (Thor, the
	// hammer, every enemy, and any actor-based scenery like signs/pushable blocks/the destructible
	// bush), and nothing here ever redraws them while the menu stays open (got_advance_game(), which
	// normally does that every tick, is frozen the whole time the menu is open -- see this file's own
	// got_menu_is_open() gate in got_main.c). Real select_option() doesn't have this problem (it
	// redraws the real actor list itself, same as real d_restore() does for dialogue boxes -- see
	// got_dialogue.c's own restore_page(), the same real pattern, already correctly ported there).
	// Fixed the same way: redraw every sprite immediately after the backdrop restore, every frame.
	xcopyd2d(0, 0, 320, 192, 0, 0, GOT_PAGE2, GOT_PAGE0, 320, 320);
	got_draw_all_sprites();

	title = current_title();
	items = current_items();
	count = current_item_count();

	// Cap the box's own row count to whatever MENU_MAX_VISIBLE_ITEMS (above) says actually fits the
	// fixed low-res canvas, and scroll instead of letting the box grow past it -- wootbeer: "the
	// pause/options menu is too big to fit on the screen now" once Save Game/Load Game brought the
	// top-level list to 10 rows. Recomputed fresh every frame from s_selected rather than kept as
	// its own persistent state, so there's no separate scroll position to fall out of sync with the
	// selection -- same reasoning refresh_top_labels()/refresh_cheats_labels() already lean on
	// elsewhere in this file (derive display state from the real state each frame, don't cache it).
	visible = count;
	if (visible > MENU_MAX_VISIBLE_ITEMS) {
		visible = MENU_MAX_VISIBLE_ITEMS;
	}
	scroll = 0;
	if (count > visible) {
		// Keep the current selection roughly centered in the visible window, clamped to the list's
		// own ends.
		scroll = s_selected - visible / 2;
		if (scroll < 0) {
			scroll = 0;
		}
		if (scroll > count - visible) {
			scroll = count - visible;
		}
	}

	w = got_text_width(title) / 8;
	for (i = 0; i < count; ++i) {
		// Full list, not just the visible slice -- keeps the box's own WIDTH stable while scrolling
		// instead of resizing it, same "don't let the box change shape out from under itself" reason
		// this function's own GOT_PAGE2-restore comment above exists for (a width that shrank or grew
		// as you scrolled past a longer/shorter label would reintroduce that exact bug sideways).
		int iw = got_text_width(items[i]) / 8;
		if (iw > w) {
			w = iw;
		}
	}
	if (w & 1) {
		++w;
	}
	w = w * 8 + 32;
	s = w / 16;
	h = visible * 16 + 32;
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

	// Mirror Mode (wootbeer: "the brief un-mirror is pretty noticeable... is it too hard to work
	// around this?") -- captures this frame's own actual outer box extent (the 16px border tiles
	// drawn just below push the real visible edge out to x1-16/y1-16/x2+16/y2+16) so got_main.c's
	// render loop can hand it to modex_set_mirror_exclude_rect() and keep the world mirrored right
	// up to this box's own edge instead of un-mirroring the whole screen while the menu is open --
	// see that function's own modex.h comment for the full design. got_menu_get_box_rect() below is
	// what reads these back.
	s_menu_box_x1 = x1 - 16;
	s_menu_box_y1 = y1 - 16;
	s_menu_box_x2 = x2 + 16;
	s_menu_box_y2 = y2 + 16;

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
		for (i = 0; i < visible + 2; ++i) {
			xfput(x1 - 16, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 198L * 262));
			xfput(x2, y1 + i * 16, GOT_PAGE0, (char *) (bpics + 199L * 262));
		}
	}

	tw = got_text_width(title);
	got_xprint((320 - tw) / 2, y1 + 4, title, GOT_PAGE0, TITLE_COLOR);

	// Small "more above"/"more below" markers in the title row and the box's own bottom border row
	// -- the only on-screen hint a scrolled list exists at all, now that the box itself can no
	// longer just grow tall enough to show every row at once.
	if (scroll > 0) {
		got_xprint(x2 - 24, y1 + 4, "^", GOT_PAGE0, ITEM_COLOR);
	}
	if (scroll + visible < count) {
		got_xprint(x2 - 24, y2 - 12, "v", GOT_PAGE0, ITEM_COLOR);
	}

	// Cursor: the real animated HAMPIC hammer, not a plain ">" glyph -- wootbeer: "all menu cursors
	// should be the spinning hammer like the main menu uses... this should be consistent
	// everywhere." Same `iy - 3` vertical-centering offset got_title_draw_main_menu() already
	// established for this 16px-tall sprite against a ~9px text row.
	for (i = 0; i < visible; ++i) {
		int item_index = scroll + i;
		int iy = (y1 + 28) + i * 16;
		got_xprint(x1 + 32, iy, items[item_index], GOT_PAGE0, ITEM_COLOR);
		if (item_index == s_selected) {
			const unsigned char *hampic = got_get_hampic();
			if (hampic) {
				int frame = (s_cursor_frame_counter / 7) % 4;
				xfput(x1 + 8, iy - 3, GOT_PAGE0, (char *) (hampic + frame * 262));
			} else {
				// Same fallback got_title_draw_main_menu() already uses when real art isn't
				// available.
				got_xprint(x1 + 8, iy, ">", GOT_PAGE0, ITEM_COLOR);
			}
		}
	}
}
