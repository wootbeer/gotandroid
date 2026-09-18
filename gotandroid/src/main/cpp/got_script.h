#ifndef GOT_SCRIPT_H_
#define GOT_SCRIPT_H_

#include <stdbool.h>

// Number of episodes GOTRES.DAT actually ships (confirmed directly against the real Steam release's
// own GOTRES.DAT, byte-level: every one of SDAT1/2/3, BPICS1/2/3, SPEAK1/2/3 etc. is present in the
// single archive this project has used from the start -- see got-android-port-notes_1.md's own
// episode-availability writeup). Shared between got_script.c's own per-episode SPEAK cache and
// got_main.c's own per-episode BPICS/SDAT caches (got_main.c's got_set_active_episode()) so both
// files agree on the same episode count from one place rather than two separately-maintained `3`s.
#define GOT_NUM_EPISODES 3

// Real GoT dialogue/scripting engine -- a direct port of 1_script.c's execute_script() and its
// full command interpreter (SPEAKn resource scripts: SAY/ASK/TEXT/ITEMSAY, IF/GOTO/GOSUB/RETURN/
// FOR/NEXT/RUN, ADDJEWELS/ADDHEALTH/ADDMAGIC/ADDKEYS/ADDSCORE, SOUND/PLACETILE/ITEMGIVE/ITEMTAKE/
// SETFLAG/LTOA/PAUSE/EXEC/VISIBLE/RANDOM, and all ~26 real "@" internal variables). This is the
// same real engine every real speaking actor in the game uses (GLOBE's own execute_script() call
// is just one caller among many real ones -- odin_speaks()/actor_speaks() are the others) -- see
// got_script.c's own top-of-file comment for exactly what's faithfully ported vs. stubbed (EXEC and
// VISIBLE reach into subsystems -- hardcoded per-level mechanics, a stable actor-slot lookup -- this
// port hasn't built yet; those two parse correctly so a script's control flow is never thrown off
// by them, but their actual game effect is a documented no-op until those subsystems exist).
//
// Adapted from the real blocking while(1) shape to this port's per-frame render loop, the same way
// got_menu.c already adapted select_option() and got_advance_game() already adapted the real main
// loop -- see got_script_update()'s own comment for exactly how.

// Loads all GOT_NUM_EPISODES episodes' own real SPEAKn resources (SPEAK1/SPEAK2/SPEAK3) up front
// and caches each, same "load once, keep for the app's lifetime" policy as s_bpics/s_sdat/the TEXT
// font -- matching got_main.c's own got_load_real_resources() preloading every episode's BPICS/SDAT
// at the same time, rather than only whichever episode the player happens to pick (so switching
// episodes at the title screen, got_script_set_episode() below, never re-touches the resource
// archive mid-game). Defaults the *active* episode to 1 (see got_script_set_episode()) so every
// pre-existing single-episode call site keeps working unchanged until something actually switches
// it. Call once during startup resource loading alongside got_font_init(). Not fatal if any one
// episode's SPEAKn fails to load -- got_script_execute() just does nothing while that episode is
// active, same tolerance every other optional resource in this port already gets.
void got_script_init(void);

// Repoints the *active* SPEAK buffer got_script_execute()/read_script_file() actually read from at
// whichever episode's own cached copy got_script_init() above loaded -- called by got_main.c's own
// got_set_active_episode() every time a game actually starts (a brand new game) or resumes (a
// loaded save, using that save's own recorded episode), never mid-script (got_script_is_running()
// is always false at both of those call sites -- no dialogue can be "in progress" before a game has
// even started/resumed). `episode` is 1-based (1/2/3, matching GotSaveHeader.episode and this
// port's episode numbering everywhere else); out-of-range values are ignored (logged, active buffer
// left unchanged) rather than corrupting the pointer.
void got_script_set_episode(int episode);

// True once a script is actively running -- including while it's paused waiting on a dialogue box
// the player hasn't dismissed yet, or an ASK prompt, or a PAUSE countdown. got_main.c's render
// loop uses this the same way it already uses got_menu_is_open(), to gate normal gameplay
// advancement while a script has control (matching real execute_script() blocking the whole real
// game loop for its own duration).
bool got_script_is_running(void);

// Starts running the script labelled "|<index>" in the loaded SPEAK1 resource -- direct port of
// real execute_script(index, pic). `pic` is the real 4-frame, 262-bytes-per-frame portrait sheet
// (e.g. ODINPIC) SAY/ASK boxes draw next to the text; pass NULL for none (TEXT command boxes use
// no portrait for real, matching this). No-op if a script is already running -- real
// execute_script() has no reentrancy either; every real caller only triggers one at a time.
void got_script_execute(long index, const unsigned char *pic);

// Advances the running script by however much it can do without waiting on the player -- direct
// control-flow port of execute_script()'s own while(1) command loop, run up to the next real
// "wait" point (a SAY/ASK/TEXT box opening, or a PAUSE countdown ticking down) or to script end,
// once per frame. Must be called every frame got_script_is_running() is true, before
// got_dialogue_update()/got_dialogue_draw() (see got_main.c's render loop) -- it's what actually
// opens/advances the dialogue box in the first place, and what resumes the script once a box the
// player was reading closes.
void got_script_update(void);

// True if real SETUP.fN flag `n` (1-64) is currently set -- the same real, working 64-bit array
// SETFLAG/the real "@FLAG" internal variable already read/write (see got_script.c's own
// s_setup_flags comment), exposed read-only for got_main.c's got_pick_up_object() to check real
// HERMIT_HAS_DOLL (real SETUP.f04, confirmed to map exactly onto flag 4 -- see that call site's own
// comment) before granting the "Child's Doll" quest item a second time. Returns false for an
// out-of-range `n`, matching @FLAG's own real bounds check.
bool got_script_test_flag(int n);

// Sets real SETUP.fN flag `n` (1-64) directly -- the same bit-set the SETFLAG script opcode itself
// performs (got_script.c's own cmd_setflag()), exposed for got_main.c's got_special_tile_thor() case
// 203 (Episodes 2/3's real Electric-Saw dialogue tile, real SETUP.f10) to set it without going
// through a script. Silently no-ops for an out-of-range `n`, matching got_script_test_flag()'s own
// bounds handling.
void got_script_set_flag(int n);

// Raw byte-for-byte copy of the entire real SETUP flags array (9 bytes, bit indices 1-64 -- see
// got_script.c's own s_setup_flags comment) in/out of got_main.c's own save format
// (GotSaveHeader.setup_flags) -- added so a flag like the troll's own @flag3 (got_troll_shrub_slide())
// survives a save/reload instead of silently resetting, matching every other piece of real game state
// this port already saves. Call got_script_get_flags() while building a save (got_save_write_slot())
// and got_script_set_flags() right after reading one back (got_save_read_slot_full()), same timing as
// every other piece of restored state there -- never mid-script, matching got_script_execute()'s own
// lack of reentrancy (a save/load never happens while a dialogue box is open).
void got_script_get_flags(unsigned char out[9]);
void got_script_set_flags(const unsigned char in[9]);

// Resets every real SETUP flag to unset -- call once when starting a brand new game (got_main.c's
// got_spawn_new_thor()), the same "this app-process-lifetime global needs an explicit fresh-game
// reset" reasoning already established there for s_boss_dead_flag[]/s_thor_armor_override.
void got_script_clear_flags(void);

#endif
