#ifndef GOT_MENU_H_
#define GOT_MENU_H_

#include <stdbool.h>

// Pause / options menu -- a port of real 1_panel.c's select_option()/option_menu()/ask_exit(),
// adapted from their blocking-inner-while(1) shape to this port's per-frame render-loop structure
// the same way got_advance_game() already adapts the real per-tick movement loop (see that
// function's own comment). Draws into the real low-res page buffer with got_font.c/modex.h
// primitives (xfillrectangle/xfput/got_xprint), not a GL screen-space overlay -- the real menu is
// part of GoT's own in-game rendering, not a native-UI layer on top of it, and this follows suit.
//
// Scope: real options_menu[]/select_option() (1_panel.c) covers Sound/Music, Skill Level, Save
// Game, Load Game, Die, Turbo Mode, Help, and Quit -- seven of those eight are here, real order
// preserved (Save Game/Load Game sit between Skill Level and Die, matching real options_menu[]'s
// own ordering). Save Game/Load Game act on whichever slot got_title.c's own player-selection
// screen chose -- see got_main.c's own s_active_slot/got_menu_action_save()/got_menu_action_load()
// comments -- ported straight from real save_game()/load_game() (1_file.c), unlike the
// title/player-select screen itself (got_title.h's own scope note: there's no real source for
// that). No separate real "Digital/MIDI" toggle -- wootbeer: "since we are not supporting midi we
// should make a note to not have the midi/digital audio toggle in the options menu" (this port's
// music is pre-rendered .mp3 either way, see got_main.c's own MUSIC_* comment -- there's no second
// playback method for a toggle to choose between). One row beyond real options_menu[]'s own eight:
// "Display Mode", this port's own new three-way status-panel-mode picker (got_menu_display_mode()
// below), placed right after Skill Level -- there's no real equivalent (the real game only ever
// had one fixed panel), added here per wootbeer's own "let's return to the idea of Status panel / GUI,
// having 3 modes" round.
//
// Turbo Mode -- real options_menu[]'s eighth row -- was ported once (a "Fast Mode"/"Scroll Rooms"
// submenu, two in-place toggle rows) and then removed: wootbeer: "let's remove the Turbo Mode option
// since it's not used or needed." At the time it was presented-but-inert on this port (see the
// git/notes history for the original writeup) -- real slow_mode compensates for actual slow 1990s
// DOS hardware via a vertical-blank counter, which this port has no equivalent of (got_advance_game()
// already paces movement by real elapsed time, independent of device speed) and still doesn't need
// one for, but real scroll_flag's own job -- picking a scrolled room transition over an instant cut --
// is now real, built content: got_transition_room() (got_main.c) always plays the scroll slide
// unconditionally, matching real scroll_flag's own default-on value, since wootbeer already chose to
// remove this row rather than keep a settings toggle for it. If a way to turn it back off is ever
// wanted, got_transition_room()'s own got_start_scroll_transition() call is the one gate to add it to.

// True whenever the menu (top level or the quit-confirm submenu) is open. got_main.c's per-frame
// loop checks this to gate got_advance_game() (the real game world freezes while the menu is open,
// same as the real select_option()'s own blocking loop halting everything else) and to know
// whether to call got_menu_draw() this frame.
bool got_menu_is_open(void);

// Call exactly once per frame, unconditionally (whether or not the menu is currently open) --
// edge-detects the pause/menu button (key_flag[KEY_ESC], see got_controls.c's new BTN_PAUSE) and
// opens or closes the menu on a fresh press, the same key real 1_main.c's own
// `if(key_flag[ESC]){ key_flag[ESC]=0; opt=option_menu(); }` check uses to bring up option_menu(),
// simplified here to a single toggle button rather than replicating that call site's exact
// dispatch chain (which leans on several not-yet-ported systems). Pressing it while a submenu is
// open backs out one level, matching real ESC-cancels-out-of-select_option() behavior; pressing it
// at the top level closes the menu entirely and restores the game view.
void got_menu_poll_toggle_key(void);

// Advances the menu's own input handling by one frame -- UP/DOWN navigation (reusing
// key_flag[KEY_UP]/key_flag[KEY_DOWN], the exact same shared global the real select_option() itself
// reads, and this port's own D-pad touch buttons already write to) and confirm (key_flag[KEY_FIRE],
// one of the real accepted confirm keys alongside ENTER/SPACE/key_magic). Call every frame instead
// of got_advance_game() while got_menu_is_open() is true.
void got_menu_update(void);

// Draws the currently open menu (border box using the real bg_pics corner/edge decorative tiles,
// title, item list, and a simple ">" cursor next to the selected item -- see this file's own
// comment on why that replaces the real animated hammer-icon cursor) into GOT_PAGE0. Call after
// got_menu_update(), before modex_present_frame(), only while got_menu_is_open().
void got_menu_draw(void);

// True if sound effects should play -- toggled by the menu's "Sound Effects" item, defaults to
// true. got_main.c's got_play_sound() checks this before dispatching to GotView.playSound().
bool got_menu_sound_enabled(void);

// True if music should play -- toggled by the menu's "Music" item, defaults to true. got_main.c's
// got_play_music() checks this before dispatching to GotView.playMusic(), same pattern as
// got_menu_sound_enabled() above.
bool got_menu_music_enabled(void);

// Current skill level: 0=Easy, 1=Normal, 2=Tough -- matches real setup.skill's own 0-2 range and
// real options_skill[]="Easy Enemies"/"Normal Enemies"/"Tough Enemies" exactly. Toggled by the
// menu's own "Skill Level" submenu (three rows, not an on/off toggle like Sound/Music), defaults
// to 1 (Normal), matching real 1_init.c's own `setup.skill=1;`. got_main.c's got_actor_damaged()/
// got_thor_damaged() call this to scale combat damage exactly like real actor_damaged()/
// thor_damaged() do (1_move.c) -- see either function's own comment for the exact real formula.
int got_menu_skill_level(void);

// Setters for the three getters above -- added for the save/load feature (got_main.c's own
// got_save_read_slot_full()/got_menu_action_load()) so a loaded save's own skill/sound/music
// settings actually take effect, not just its stats/position. Each also refreshes the top-level
// menu's own label text (matching what toggling the row by hand already does), so the pause menu
// reads correctly the next time it's opened without waiting for its own dispatch to run first.
// got_menu_set_skill_level() clamps to the real 0-2 range, same range got_menu_skill_level() itself
// documents.
void got_menu_set_skill_level(int level);
void got_menu_set_sound_enabled(bool enabled);
void got_menu_set_music_enabled(bool enabled);

// Sound/Music submenu's own gain sliders -- wootbeer: "each one will be an option with a slider similar
// to descore to set the audio/gain level of the sound, and music channels," then, once shipped:
// "yes do add a way to save these two settings between app sessions." 0-10 (10 steps of 10% each),
// same range the submenu's own SOUND_MUSIC_GAIN_STEPS constant (got_menu.c) uses, both defaulting to
// the top (100%). Unlike got_menu_skill_level()/got_menu_sound_enabled()/got_menu_music_enabled()
// above -- which only ever restore when a specific save slot is loaded (GotSaveHeader) -- these two
// are account-wide, not per-slot: got_main.c's got_config_init()/got_config_save() persist them to
// their own small CONFIG.DAT file (same "account-wide, its own file" precedent as
// got_highscore_init()/HISCORE.DAT; named generically -- wootbeer's own request -- so any future app-wide
// setting can land in that same file too, not just these two), read once at startup before any save
// slot is ever touched, and
// written every time either setter below actually changes the value. Each setter clamps to 0-10,
// pushes the resulting 0.0-1.0 float to GotView.java via got_sound_set_volume()/
// got_music_set_volume() (got_main.c), refreshes the submenu's own label text, and persists the new
// value -- the single place all three of those things happen, called both by got_menu_update()'s own
// Left/Right handling and by got_main.c's own startup load.
int got_menu_sound_gain(void);
int got_menu_music_gain(void);
void got_menu_set_sound_gain(int gain);
void got_menu_set_music_gain(int gain);

// New "Touch Options" submenu, same shape and same account-wide CONFIG.DAT persistence as the
// Sound/Music gain sliders just above -- wootbeer, after seeing Descore's own touch-scaling menu
// option: "so can we add a new sub-menu to the pause menu as well? the new entry can be called
// 'touch options'... it can have an option called 'scaling'... this slider will be used to adjust
// the scaling. and then we can also add an entry called 'opacity'... only make this entry appear
// though if touch controls are enabled... also it can save its options between sessions in that
// config.dat we setup earlier." got_menu_touch_scale_step() is a 0-8 index into got_controls.c's
// own 9-entry Touch_scale_values-equivalent table (see that file's own comment for the exact
// range); got_menu_touch_opacity_step() is 0-SOUND_MUSIC_GAIN_STEPS(10), same granularity as the
// Sound/Music gain sliders, but mapped to an alpha floored at TOUCH_OPACITY_MIN_ALPHA (got_menu.c)
// rather than running down to fully invisible -- "make sure the minimum opacity is what our
// previous default was so the user doesn't lose complete track of the controls." Each setter
// clamps, pushes the result to got_controls.c (got_controls_set_scale_step()/
// got_controls_set_opacity()), refreshes this submenu's own label text, and persists via
// got_config_save(), same division of labor got_menu_set_sound_gain()/got_menu_set_music_gain()
// already established -- called both by got_menu_update()'s own Left/Right handling and by
// got_main.c's own startup load (got_config_init()).
int got_menu_touch_scale_step(void);
int got_menu_touch_opacity_step(void);
void got_menu_set_touch_scale_step(int step);
void got_menu_set_touch_opacity_step(int step);

// Real STATUS-panel display mode: 0 = "A" (Force 4:3, pillarboxed), 1 = "B" (authentic panel,
// full-bleed stretch), 2 = "C" (this port's own minimal translucent overlay, got_hud.c) -- see
// got-android-port-notes_1.md's own three-way design writeup and modex.h's own
// modex_set_display_mode()/MODEX_PANEL_PAGE comments for what the three modes actually do at the
// presentation layer, and got_panel.h for Modes A/B's own content. Defaults to 2 (Mode C) -- this
// port's only real behavior before this round, so an existing player's experience doesn't change
// just because this feature shipped. Toggled by the menu's own new "Display Mode" row (a three-way
// cycle submenu, same shape as Skill Level's own three-row submenu, not an on/off toggle like
// Sound/Music). got_main.c's render loop reads this once every frame during actual gameplay
// (matching this port's established "read state fresh each frame, no change-notification plumbing"
// convention already used for got_draw_hud()'s own inputs) and forwards it to
// modex_set_display_mode() -- outside gameplay (the title/player-select screens) it forces Mode C
// regardless of this setting, see modex_set_display_mode()'s own comment for why.
int got_menu_display_mode(void);
void got_menu_set_display_mode(int mode);

// Enhancements submenu, "Hammer Color:"/"Armor Color:" -- wootbeer: "add two new options above the other
// items in the Enhancements menu, an option for 'Hammer Color:' and one under it for 'Armor Color:'
// the user will be able to toggle the options for both between 'Default' (uses the colors they should
// be originally...), 'Part I', 'Part II', and 'Part III'." Placed above every other Enhancements row
// per that request. Each independently overrides which of this port's three real Thor/hammer sprite
// tiers gets loaded -- Part I/II/III are real armor values 0/1/10 (see got_main.c's own
// s_enh_color_part_tier for the exact ACTORnnn/real-name mapping); Default means no override, i.e.
// whatever the game itself would show (episode + boss progress, same value either row already showed
// before this feature existed). A real fourth tier (armor 2) exists in the resource data but is
// deliberately not offered here -- it's a one-off "Thor turns into a black eyeless-except-for-two-
// glowing-eyes silhouette" story effect for one specific Episode 2 room, not a normal recolor, and
// this port doesn't otherwise model that room yet (flagged to wootbeer, not silently dropped -- see
// s_episode_thor_armor's own comment in got_main.c). wootbeer, in the same message: "odin says the armor
// will help us take less damage and such, is there a mechanic of making your actual armor or hammer
// damage better?; if this is the case then make sure our color override options we add to the
// Enhancement menu only override the color and not any effect." Checked directly against real
// resource bytes: Thor's own armor is purely cosmetic (thor_info.armor is never read anywhere in
// real source except to pick a sprite -- Odin's dialogue is flavor text), but the HAMMER's own
// `strength` (real damage dealt per hit) genuinely climbs with its tier -- 10/13/17 across the three
// tiers offered here -- so got_main.c's own got_load_player_sprite_color_safe() loads the chosen
// color for its pixels only and restores every gameplay-affecting field (strength included) from
// whatever the real, uncosmetic tier would have given, every time these two rows are used. Both
// return 0=Default/1=Part I/2=Part II/3=Part III, cycled in that order on confirm, same in-place
// shape as every other Enhancements row. Session-only, not part of the save file, same reasoning as
// every other Enhancements row below.
int got_menu_enhancement_hammer_color(void);
int got_menu_enhancement_armor_color(void);

// Enhancements submenu -- wootbeer: "in our pause/options menu I would like to add a new option for a
// new sub-menu called 'Enhancements', this option will be located above the 'Cheat Codes' sub-menu
// option." Unlike Cheat Codes just below (each row a deliberate, player-chosen unfair advantage),
// this new section holds genuine gameplay-feel tuning options -- the first is "Enemy Atk. Speed:",
// an in-place two-way toggle (same "push confirm, the row's own text cycles in place" shape as
// Sound Effects/Music at the top level, per wootbeer's own description: "just have the player push the
// 'a' button... and the text 'Default' will change to 'Slow', as they scroll through the available
// selections") between "Default" (this port's own corrected, faster WORMY/SPIDER shot speed -- see
// got_main.c's own got_actor_spawn_shot() comment for the fix this option switches between) and
// "Slow" (this port's previous, un-multiplied shot speed, kept selectable for side-by-side
// comparison rather than deleted outright, matching wootbeer's own explicit request to keep both). More
// rows will join this submenu later (wootbeer: "we will add other options to this new Enhancements menu
// later"). Session-only, same as Cheat Codes below -- defaults to "Default" (the fast/new behavior)
// on every fresh launch; not part of the save file (this is a bugfix/tuning preference, not
// progression state, so there's nothing a save slot needs to remember about it).
bool got_menu_enhancement_fast_enemy_shots_enabled(void);

// Enhancements submenu, continued -- wootbeer: "add an option for 'Boss Speed:' and have it toggle
// between 'Default' 'Slow' and 'Fast' (slow being half speed, fast being 2x speed). Also add an
// option for 'Hammer Speed' and have it toggle between 'Default' 'Slow' and 'Fast' (slow being half
// speed, fast being 2x speed)." Same in-place single-row cycling shape as "Enemy Atk. Speed:" above,
// just three states in a row instead of two, cycled in the order wootbeer named them (Default, Slow,
// Fast) rather than built as a separate three-row "pick one" submenu like Skill Level/Display Mode.
// Returned as 0=Default/1=Slow/2=Fast -- see got_main.c's own got_scale_speed_tick() for exactly how
// each value scales the affected sprite's own real speed_tick-gated pacing (doubling the interval
// for Slow, roughly halving it for Fast, both applied on TOP of whatever that sprite's own baseline
// speed already is -- e.g. Jormangund's own already-real 2x speed-up stays intact under "Default"
// here and doubles again under "Fast"). "Boss Speed" scales every ported boss's own driver-segment
// movement dispatch (got_step_sprite()'s case 20/26/27 -- Jormangund, Loki, the Episode 2 Skull
// boss); "Hammer Speed" scales the thrown hammer's own flight/return speed (got_step_hammer()). Both
// default to Default (0) on every fresh launch -- session-only, not part of the save file, same
// reasoning as "Enemy Atk. Speed:" above.
int got_menu_enhancement_boss_speed(void);
int got_menu_enhancement_hammer_speed(void);

// Enhancements submenu, continued -- wootbeer: "add an option for 'Tombstone', with 'On' and 'Off', when
// turned 'On' this option will use the unused Tombstone sprite we found earlier, the Tombstone will
// be placed on the board where on the spot where Thor dies (if he dies) and will disappear from the
// board on next death, and placed in the spot of the newest death." A new, wootbeer-invented feature, not
// a real-fidelity restoration. NOT real thor_dies()'s own `objects[10]` flash -- wootbeer confirmed by
// eye that objects[10] actually depicts Thor's own corpse lying down, not a tombstone (an earlier,
// unverified assumption here was wrong). This option draws `objects[13]`: real G1's own
// `object_names[]` array (1_back.c) labels thor_info.object==3 "UNUSED" (never assigned to any
// level's object_map, so never actually collectible in the real game) -- decoding the real OBJECTS
// resource this port already loads as s_objects confirmed index 13 (thor_info.object+10) is a small
// burial-mound bitmap with Thor's hammer resting on it, closer to a tombstone than objects[10] but
// still not convincing once seen in-game, so wootbeer drew this port's own tombstone art by hand instead
// -- patched directly over the real index-13 bitmap in got_main.c's own s_objects copy at load time
// (see s_custom_tombstone_tile there for the conversion/patch). Real thor_dies() draws its own
// (different, corpse) sprite at Thor's death spot as a fixed one-second flash immediately erased by
// the room reload that follows -- real code never lets anything persist on the board the way this
// option asks for. This port's own version tracks
// only the single most recent death (level + exact pixel position, got_main.c's own
// s_tombstone_level/s_tombstone_x/s_tombstone_y, captured the instant a death starts) and bakes it
// onto that one room's own persistent GOT_PAGE2 backdrop every time that room loads, for as long as
// it remains the most recent death -- a fresh death anywhere else immediately replaces it, matching
// wootbeer's own "disappear... on next death, and placed in the spot of the newest death". In-place
// on/off toggle, same shape as the Cheat Codes rows below; defaults to Off (this is an added,
// non-canonical visual feature, not a corrected default the way "Enemy Atk. Speed:"'s own "Default"
// is) on every fresh launch -- session-only, not part of the save file, but the underlying death-spot
// itself is captured unconditionally regardless of this toggle's state, so switching it on
// mid-session immediately shows the most recent death that already happened rather than waiting for
// a fresh one.
bool got_menu_enhancement_tombstone_enabled(void);

// Enhancements submenu, continued -- "Hourglass:", On/Off, added after wootbeer asked whether the
// Hourglass magic item (found early on, flagged out-of-scope around round 100) actually does
// anything in the real game. It does: real `use_hourglass()` (1_object.c, identical across all
// three episodes) is a fully real, functional "Freeze Time" spell -- costs 30 magic, freezes every
// ordinary enemy for up to ~11.5 real-game seconds (escalating WOOP sound cue, music paused for the
// duration then resumed), gated by a real per-actor `HOURGLASS_MAGIC` immunity bit real bosses/Thor/
// the hammer/the shield companion are excluded from -- but it's genuinely orphaned: real `use_item()`
// (the item-picker's own dispatch switch) has no case for it at all, and nothing else in the real
// source calls it either, so it was never reachable through ordinary play despite being fully
// working code. wootbeer, given that finding: "sort of like [a separate toggle], put it in the
// enhancements menu though. title it Hourglass:" -- rejecting bundling this into the All Items
// cheat, same in-place on/off toggle shape as Tombstone/Mirror Mode just above. Turning this On adds
// a new, always-castable 8th item ("Freeze Time") to the item picker (got_item.c's own
// rebuild_list() comment) independent of the real 6-bit inventory scheme every other item uses,
// since this isn't a real pickup -- there's nowhere in the real game it's ever actually granted.
// Defaults to Off, session-only, not part of the save file, same reasoning as every other
// Enhancements row.
//
// Boss exemption: wootbeer, asked whether Freeze Time should affect boss fights, picked the recommended
// "Exempt bosses" option -- got_main.c's own enemy-step loop skips the freeze check entirely for any
// actor already identified as a boss driver segment (same boss-detection this port's own Boss Speed
// enhancement already relies on), matching real `HOURGLASS_MAGIC`'s own spirit (an immunity bit
// real content author intent clearly reserved for "things too important to freeze") even though the
// three real bosses in this port never actually carried that bit themselves (they didn't need to --
// the spell was unreachable in real play to begin with).
//
// Icon: no true hourglass-shaped art exists anywhere in the real OBJECTS resource (all 32 entries
// decoded and visually inspected) -- wootbeer, asked directly, first guessed a fallback ("isn't there an
// icon that kinda looks like it? or an X inside a square?") that turned out to be exactly right
// (real object_map indices 24/25, array indices 23/24, both within the real, never-assigned
// "FUTURE"/unused range 15-26, are a plain "X filling a bordered square" placeholder with no other
// real meaning) -- but then supplied real art instead: "ok I actually added a hourglass.png to the
// gotfiles folder we can use as an icon." Same hand-drawn-art pipeline as the Tombstone
// Enhancement's own `objects[13]` replacement (a 16x16 RGBA PNG, converted offline into this port's
// 262-byte tile format, each opaque pixel remapped to its nearest real-palette entry -- see
// got_main.c's own `s_custom_hourglass_tile` comment for the exact conversion), not drawn from
// objects_buf/the OBJECTS resource at all. got_panel.c's `got_panel_draw()` takes this tile as its
// own `hourglass_icon` parameter and special-cases item value 8 to draw it directly, rather than
// either real `objects[thor_info.item+25]`/`objects[thor_info.object+10]` formula (which couldn't
// reach a non-real item value this high anyway). got_hud.c's own minimal-HUD (Display Mode C) has no
// icon swatches for any item (wootbeer had those removed earlier), so this only matters for Modes A/B's
// full status panel; Mode C shows item 8 as plain text instead -- wootbeer: "don't forget to add a tag in
// the minimal ui for the hourglass as well, it can just be HOURGLASS" (see got_hud.c's own
// ITEM_LABELS comment).
bool got_menu_enhancement_hourglass_enabled(void);

// Enhancements submenu, continued -- wootbeer: "how hard would it be to add a new feature, an option in
// our Enhancement sub-menu, called 'Mirror Mode'?: it would work like a mirror mode in other games
// traditionally; so everything is flipped on the Y-axis, left is right, right is left... maybe just
// flip the axis things are rendered on?... but would have to make sure to keep all text, dialogue,
// menus, pop-ups, icons, all versions of the GUI, all still un-flipped. and of course the player
// would still have to be able to walk / control in the correct directions, and when entering/leaving
// boards the corresponding paths still need to be in place." A new, wootbeer-invented feature, not a
// real-fidelity restoration -- no such option exists anywhere in the real game.
//
// Implemented purely at presentation time -- see modex.h's own modex_set_mirror_mode() comment for
// the full design and why it's read-only against the world model (nothing about tile placement,
// collision, or room-transition logic is touched, so board connectivity when entering/leaving rooms is
// automatically preserved exactly as wootbeer asked). got_main.c's render loop reads this getter fresh
// every frame (this port's established convention for HUD-adjacent state) and only calls
// modex_set_mirror_mode(true) while this is on AND the frame holds pure live gameplay content -- see
// that call site's own comment for why it's forced off whenever the pause menu/item picker/a dialogue
// box is open, keeping every GUI element un-flipped as required.
//
// Controls: got_move_thor() (got_main.c) swaps which physical direction key (Left/Right, including
// both diagonal combinations) is read as which movement branch while this is on, confined entirely to
// that one function -- menu/dialogue/item-picker navigation never reads Left/Right at all, so they're
// unaffected by construction, and the swap doesn't need its own separate "which way is Thor now
// facing" fix either: each movement branch already ties its position delta and its sprite-facing `dir`
// together, so swapping which physical key reaches which existing branch keeps both correct after the
// screen itself is mirrored. In-place on/off toggle, same shape as Tombstone just above; defaults to
// Off, session-only, not part of the save file.
bool got_menu_enhancement_mirror_mode_enabled(void);

// Mirror Mode follow-up -- wootbeer, after playtesting the first round: "the mirror mode works as
// described, but the brief un-mirror is pretty noticeable, not really during pausing, but it
// happens during dialogue and other pop-ups too I assume. is it too hard to work around this?"
// Root cause: the first round simply turned mirroring off entirely whenever the pause menu/item
// picker/a dialogue box was open, since all three draw directly into the same page buffer as the
// world with no separate compositing layer -- see modex.h's own modex_set_mirror_mode() comment.
// Fixed properly instead of working around it: this getter reports the pause menu's own current
// outer box rect (screen pixel coordinates, valid only while the menu is actually open -- returns
// false otherwise) so got_main.c's render loop can pass it to the new
// modex_set_mirror_exclude_rect() (modex.h) and keep the world mirrored everywhere EXCEPT that one
// rect, instead of giving up on mirroring the whole screen. got_item_menu_get_box_rect()
// (got_item.h) and got_dialogue_get_box_rect()/got_ask_get_box_rect() (got_dialogue.h) are this
// same pattern's siblings for the item picker, dialogue box, and ASK prompt respectively.
bool got_menu_get_box_rect(int *x1, int *y1, int *x2, int *y2);

// Cheat Codes submenu -- wootbeer: "is there a way we could also add a 'cheat code' for 'unlimited'
// keys. I would also like to add a Cheat Code sub-menu under the options/pause menu, and have
// toggles for the 'Unlimited Keys', 'God Mode' (invincible), and 'Unlimited Jewels', that we added
// earlier," then a follow-up round adding a fourth: "can we add a new 'cheat code' a toggle for
// unlimited magic?" These started as got_main.c's own compile-time `#define CHEAT_GOD_MODE`/
// `CHEAT_UNLIMITED_JEWELS` flags (one always on, one needing a source edit + rebuild to flip) --
// now real in-game toggles, an "in-place toggle row, stays in the submenu after confirming" shape
// (not a three-way "pick one" submenu like Skill Level/Display Mode above, since all four are each
// independently on/off, not mutually exclusive choices).
// All five (a fifth, "All Items:", added in a later round -- see below) default OFF and are
// session-only: got_main.c's own save/load (got_save_read_slot_full()) never reads or writes them,
// so they always come back off on a fresh app launch OR a loaded save, matching a "cheat code" being
// something a player deliberately re-enables for the current session rather than part of actual game
// progress.
bool got_menu_cheat_unlimited_keys_enabled(void);
bool got_menu_cheat_god_mode_enabled(void);
bool got_menu_cheat_unlimited_jewels_enabled(void);
bool got_menu_cheat_unlimited_magic_enabled(void);

// "All Items:" -- wootbeer: "add a new option in the 'Cheat Code' menu, that will be for 'All Items',
// and you can toggle it on or off like the others. it will add to inventory all the useful usable
// items, so Enchanted Apple, Lightning Power, Winged Boots, Wind Power, Amulet of Protection, and
// Thunder Power." Unlike the four rows above (each a live per-tick gate got_main.c's own call sites
// check), this one is a direct, one-shot grant/revoke of `s_thor.inventory` -- see got_main.c's own
// got_cheat_set_all_items() for the real inventory bit scheme and how toggling back off only removes
// what the cheat itself granted, never an item picked up legitimately. No separate getter: got_menu.c
// calls got_cheat_set_all_items() directly from dispatch_confirm() at the moment the row toggles,
// rather than got_main.c polling a boolean every tick the way the other four cheats work.
//
// wootbeer's own follow-up, also addressed in this same round: "did notice one thing with the armor and
// hammer override enhancement we added, it doesn't take effect unless we save and reload the game,
// is it easy to make it happen 'instantly'?" -- see got_main.c's own got_refresh_player_sprite_
// colors() (called from this file's own dispatch_confirm(), ENH_HAMMER_COLOR/ENH_ARMOR_COLOR cases).

// Jormangund boss-fight overhaul follow-up (wootbeer: "could we have the unlimited jewels cheat turn
// itself off before counting the jewels, it shouldn't hurt anything but just to be safe"). The
// jewel-drain-into-score sequence (got_main.c's own got_boss1_start_jewel_drain()/got_boss1_step_
// jewel_drain()) already can't hang with this cheat left on -- it counts down a separately-captured
// total rather than looping on the live jewel count itself, see that field's own comment -- but with
// the cheat still active, got_thor_add_jewels(-1) is a no-op every step (see that function's own
// cheat gate), so the jewel count visibly never reaches 0 even once the drain's finished, just score
// climbing on its own. Called once, right before got_boss1_start_jewel_drain() itself, so the drain's
// own got_thor_add_jewels(-1) calls actually take effect and the jewel count really does hit 0 like
// the rest of the closing sequence expects -- same one-way "turn it off" shape as a real developer
// cheat you wouldn't want quietly fighting the game's own win-sequence bookkeeping. Session-only, same
// as every other cheat here -- doesn't touch the save file, and does nothing if the cheat wasn't on to
// begin with. Refreshes the Cheats submenu's own "Unlimited Jewels: Off" label in case the pause menu
// happens to already be sitting on that screen (defeating the boss doesn't itself close the menu).
void got_menu_cheat_disable_unlimited_jewels(void);

// "All Items:" follow-up bug -- wootbeer: "when reloading the save it still says 'All Items: On' even
// though it's technically off." Root cause: unlike the other four cheats (each a live gate that's
// harmless to leave flagged "on" across a load -- it just keeps applying), "All Items:" grants real,
// savable state, and got_save_write_slot() (see that function's own comment) always excludes
// whatever it granted from the file, so the inventory a load restores can never actually contain a
// cheat-only item -- meaning the cheat is functionally off again the instant a save is loaded (or a
// new game starts), even though this file's own `s_cheat_all_items` boolean, with nothing to reset
// it, kept reading "on." got_main.c now calls this once, right after restoring `s_thor.inventory` on
// BOTH a load (got_save_read_slot_full()) and a brand new game (got_spawn_new_thor()), to keep the
// row's own displayed state truthful. Same "no-op, no label refresh, if it wasn't on to begin with"
// shape as got_menu_cheat_disable_unlimited_jewels() just above.
void got_menu_cheat_disable_all_items(void);

#endif
