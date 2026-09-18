// Real GoT dialogue/scripting engine -- see got_script.h for the overall design. A direct port of
// 1_script.c's execute_script()/read_script_file()/get_command()/calc_value()/calc_string()/
// get_internal_variable()/exec_command() and every real cmd_*() handler, run against the real
// SPEAK1 resource (loaded once and cached, like this port's other resource tables).
//
// Faithfully ported, fully functional: all real control flow (END/GOTO/GOSUB/RETURN/FOR/NEXT/IF/
// ELSE/RUN), every real numeric/string variable and the real "+"-concatenation/arithmetic
// expression grammar, every real "@" internal variable, SAY/ASK/TEXT/ITEMSAY (via got_dialogue.c),
// ADDJEWELS/ADDHEALTH/ADDMAGIC/ADDKEYS/ADDSCORE (this port's own got_thor_add_*()), SOUND (this
// port's own got_play_sound()), SETFLAG/the real "@FLAG" test (a real, working 64-flag bit array --
// see s_setup_flags below), ITEMGIVE/ITEMTAKE/the real "@ITEM" (this port's own single-quest-item-
// slot mechanic -- got_thor_give_item()/got_thor_take_item()/got_thor_get_carried_item() in
// got_main.c, the same three THOR_INFO fields got_pick_up_object()'s own quest-item cases read and
// write directly), LTOA, PAUSE, and RANDOM.
//
// Faithfully PARSED but a documented stub: PLACETILE (reads/validates exactly like real
// place_tile(), writes into this port's own room-tile buffers -- see got_script_place_tile()), and
// VISIBLE (real actor_visible() indexes the real actor[MAX_ACTORS] slot array directly; this port's
// sprites are a compact per-room list with no stable slot-index lookup by real actor_num -- would
// need that lookup built first). VISIBLE never throws off a script's own control flow or any later
// command in the same script when it's hit -- it parses its real argument exactly like the real
// command does; it just skips the actual game-side effect, logging that it was hit.
//
// EXEC (real scr_func1-5, each hardcoded to a specific real level's mechanics) is now FULLY built:
// scr_func1 (Lokisburg red-guard arrest -> jail teleport, wootbeer: "when being touched by the red
// guards in lokisburg they should arrest the player... and they should teleport the player to a
// jail board"), scr_func2 (the same arrest's random "you got arrested for X / reason Y" excuse
// pair), scr_func3 (the Shovel's real "dig here" mechanic, got_thor_dig() in got_main.c), and
// scr_func5 (the troll's own real "step aside" trigger, got_troll_shrub_slide() in got_main.c) are
// all built -- see cmd_exec()'s own comment for scr_func1/2's real source and the SPEAK3 script that
// calls them. scr_func4 (a thunder-flash flag) targets no mechanic any currently-ported room needs
// and remains a documented no-op, logged the same way.

#include "got_script.h"
#include "got_dialogue.h"
#include "res_man.h"

#include <android/log.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "GotScript"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ---- Bridges into got_main.c (see each site's own comment for why it's exposed non-static) ----
extern void got_play_sound(int sound_index);
extern void got_thor_add_jewels(int n);
extern void got_thor_add_magic(int n);
extern void got_thor_add_health(int n);
extern void got_thor_add_keys(int n);
extern void got_thor_add_score(int n);
extern int got_thor_get_jewels(void);
extern int got_thor_get_health(void);
extern int got_thor_get_magic(void);
extern int got_thor_get_score(void);
extern int got_thor_get_keys(void);
extern void got_thor_give_item(int object_id);
extern void got_thor_take_item(void);
extern int got_thor_get_carried_item(void);
extern void got_troll_shrub_slide(void);
extern bool got_thor_dig(void);
extern int got_get_current_level(void);
extern int got_get_thor_tile(void);
extern int got_get_thor_pos(void);
extern int got_script_place_tile(int level_index, int grid_x, int grid_y, int tile);
// Bridge for scr_func1 (EXEC's Lokisburg red-guard arrest -> jail teleport) -- see cmd_exec()'s own
// comment and got_script_warp_thor()'s definition in got_main.c (a thin non-static wrapper around
// that file's own otherwise-private got_warp_thor()) for the full real mechanic this powers.
extern void got_script_warp_thor(int new_level, int new_x, int new_y);
extern void got_demo_pace_reset(void);

// ---------------------------------------------------------------------------------------------
// Real scr_command[]/internal_variable[] tables (1_script.c) -- ported verbatim, including the
// real leading "!@#$%" placeholder at index 0 (never matched -- real get_command()'s loop just
// needs SOME non-NULL sentinel there since it indexes by the loop counter, not a real command).
// ---------------------------------------------------------------------------------------------
static const char *const s_scr_command[] = {
	"!@#$%", "END", "GOTO", "GOSUB", "RETURN", "FOR", "NEXT", "IF", "ELSE", "RUN",
	"ADDJEWELS", "ADDHEALTH", "ADDMAGIC", "ADDKEYS", "ADDSCORE", "SAY", "ASK", "SOUND",
	"PLACETILE", "ITEMGIVE", "ITEMTAKE", "ITEMSAY", "SETFLAG", "LTOA", "PAUSE", "TEXT", "EXEC",
	"VISIBLE", "RANDOM", NULL,
};

static const char *const s_internal_variable[] = {
	"@JEWELS", "@HEALTH", "@MAGIC", "@SCORE", "@SCREEN", "@KEYS", "@OW", "@GULP", "@SWISH",
	"@YAH", "@ELECTRIC", "@THUNDER", "@DOOR", "@FALL", "@ANGEL", "@WOOP", "@DEAD", "@BRAAPP",
	"@WIND", "@PUNCH", "@CLANG", "@EXPLODE", "@FLAG", "@ITEM", "@THORTILE", "@THORPOS", NULL,
};

// ---------------------------------------------------------------------------------------------
// Interpreter state -- direct equivalents of 1_script.c's own real globals. One script runs at a
// time (matching real execute_script()'s own lack of reentrancy), so plain statics are fine.
// ---------------------------------------------------------------------------------------------
#define SCR_BUFF_SIZE 5000
#define TMP_SIZE 5800
#define MAX_LABELS 32

static long s_num_var[26];
static char s_str_var[26][81];
static char s_line_label[MAX_LABELS][9];
static char *s_line_ptr[MAX_LABELS];
static int s_num_labels;

static char s_buffer[SCR_BUFF_SIZE];
static char *s_buff_ptr;
static char *s_buff_end;
static char *s_new_ptr; // real `new_ptr` -- set by cmd_goto(), consumed by its caller

static char *s_gosub_stack[MAX_LABELS];
static int s_gosub_ptr;
static char *s_for_stack[11];
static long s_for_val[11];
static int s_for_var[11];
static int s_for_ptr;

static long s_scr_index;
static const unsigned char *s_scr_pic;
static long s_lvalue;
static long s_ltemp;
static char s_temps[255];
static char s_tmp_buff[TMP_SIZE]; // real global `tmp_buff` -- built by cmd_say(), read by got_dialogue.c

// Real SETUP.fN bit flags (SETFLAG / the real "@FLAG" internal variable, case 22) -- SETFLAG/@FLAG
// are real, working code (not stubbed), and this array is now persisted across saves too (added for
// the troll-slide round's own save-persistence follow-up, wootbeer: "yes let's fix that now" -- see
// got_script_get_flags()/got_script_set_flags() below and GotSaveHeader's own `setup_flags` field in
// got_main.c) -- previously an in-session-only array that silently reset to all-zero on every save/
// reload, which for a flag like the troll's own @flag3 (see got_troll_shrub_slide()'s own comment)
// meant a save made after giving the shrub, then reloaded, could theoretically let that one-time
// interaction be re-triggered.
static unsigned char s_setup_flags[9]; // ceil(64/8), 1-based bit indices 1..64 as real code uses

// Every episode's own real SPEAKn resource, cached once each (see got_script_init()) -- indexed
// 0-based by episode-1 (episode 1 at [0], episode 2 at [1], episode 3 at [2]). s_speak_data/
// s_speak_len below are the *active* episode's own entry from these two arrays, repointed by
// got_script_set_episode() -- every other function in this file (read_script_file() etc.) keeps
// reading s_speak_data/s_speak_len exactly as before, unaware there's more than one episode cached
// behind them.
static unsigned char *s_speak_data_by_episode[GOT_NUM_EPISODES] = {NULL, NULL, NULL};
static long s_speak_len_by_episode[GOT_NUM_EPISODES] = {0, 0, 0};

// Real SPEAK1 (or whichever episode is currently active) resource -- read_script_file()'s real
// per-call res_read("SPEAK1",...) is replaced with a search over this cached copy, matching this
// port's own established "load the archive entry once, reuse it" policy (s_sdat/s_bpics/the TEXT
// font all do the same instead of the real per-call resource reads their own real callers use).
static unsigned char *s_speak_data;
static long s_speak_len;

typedef enum {
	SCRIPT_IDLE = 0,
	SCRIPT_RUNNING,
	SCRIPT_WAIT_DIALOGUE,
	SCRIPT_WAIT_ASK,
	SCRIPT_WAIT_PAUSE,
} ScriptRunState;

static ScriptRunState s_run_state = SCRIPT_IDLE;
static int s_pending_ask_var; // which A-Z num_var[] slot the open ASK box's result goes into

// Bugfix (wootbeer: a demo recording made entirely under got_main.c's got_demo_tick_paced() real-time
// fix STILL threw its timing off the instant it hit a dialogue box, exactly like the two takes made
// before that fix existed. wootbeer's own diagnosis, verbatim: "in theory if our port is an exact port
// from the original then the original demo should have ran fine with no hiccups" -- correct, and the
// reason it doesn't here is this very variable, below). Root cause: cmd_pause()'s own comment (right
// below) already flagged this as "an approximation, not a real timing conversion" -- s_pause_frames_left
// used to be decremented once per got_script_update() call, i.e. once per RENDER call, the exact same
// render-rate-tied timing bug got_dialogue.c's own REVEAL_STEP_MS comment documents fixing for the
// character-reveal typewriter effect (see that constant's own writeup -- this is literally the same
// class of bug, just in a second, separate piece of script-side timing code that was never converted
// when the first one was). PAUSE commands appear inside plenty of real scripts around SAY/ASK boxes
// (e.g. SPEAK3's own |101 Lokisburg arrest script: `pause 120/exec 2/.../SAY .../exec 1`, see
// got_main.c's func_num==11 dispatch comment) -- while got_main.c's own got_demo_tick_paced() now
// correctly paces demo/record ticks at a fixed real-time 60Hz regardless of render rate, a PAUSE
// command elsewhere in the very same script was still counting down at raw RENDER-call rate instead --
// two different "clocks" advancing the same blocked dialogue sequence at two different, independently
// render-rate-dependent rates. Whenever the device's render rate during a PAUSE differed between the
// recording session and the playback session (device thermal state, GC pause, refresh-rate switching --
// see got_demo_tick_paced()'s own comment for the same non-reproducibility argument), the PAUSE would
// take a different real-world duration to finish each time, even though got_demo_tick_paced() itself
// kept advancing at the identical fixed rate throughout -- desyncing the recorded input stream from
// the script's own progress by however much the PAUSE's real-world duration differed, permanently
// shifting every tick-indexed event after it. Converted below (see PAUSE_STEP_MS and
// SCRIPT_WAIT_PAUSE's own got_script_update() case) to the same wall-clock real-time pacing
// REVEAL_STEP_MS already established, removing the render-rate dependency entirely.
static double s_pause_deadline_ms; // real elapsed ms (since s_pause_start_time) PAUSE is waiting for
static struct timespec s_pause_start_time;
static bool s_pause_time_inited;

// Forward declarations -- real 1_script.c has the same ordering issue (calc_value() and friends
// are mutually referenced before their own definitions) and solves it the same way, via the
// function-declarations block at its own top of file.
static bool calc_value(void);
static bool calc_string(int mode);
static void get_str(void);
static bool get_next_val(void);
static bool get_internal_variable(void);

// A cmd_*() handler returns this instead of a real error code (0=success, 1-11=real script error
// index) to signal "I opened a dialogue/ask/pause wait -- stop the command loop here for now, but
// this isn't the real script ending". Chosen well outside the real error-code range (1-11) and the
// real RUN sentinel (-100) so none of exec_command()'s existing real return-value checks collide
// with it.
#define CMD_YIELD (-200)

// ---------------------------------------------------------------------------------------------
// Real get_line() (1_script.c) -- extracts one CRLF-terminated line from `src` into `dst` (LF
// bytes silently dropped, matching real; see got_script.c's own research notes on why this is
// exactly right for a real CRLF-authored SPEAK resource), returning the number of source bytes
// consumed. `src_end` bounds the search (the real DOS version trusted a real "|EOF" sentinel line
// to always be found first; this port bounds it defensively too since s_speak_data is a fixed-
// length decompressed buffer, not a real open-ended DOS file read).
// ---------------------------------------------------------------------------------------------
static int get_line(const unsigned char *src, const unsigned char *src_end, char *dst) {
	const unsigned char *start = src;
	int t = 0;
	while (src < src_end && *src != 13) {
		if (*src != 10) {
			dst[t++] = (char) *src;
		}
		++src;
	}
	dst[t] = 0;
	if (src < src_end) {
		++src; // consume the CR itself, matching real `cnt++; src++;` after the loop
	}
	return (int) (src - start);
}

// ---------------------------------------------------------------------------------------------
// Real read_script_file() -- finds the "|<index>" label in the cached SPEAK1 text and decodes
// everything up to (not including) the matching "|STOP" into s_buffer, exactly like real code:
// blank lines become null command-separators, "NAME:"-style short lines become jump-target labels
// (recorded in s_line_label/s_line_ptr, consuming no executable buffer text of their own), quoted
// text is preserved case-exactly, everything else is uppercased, and a real apostrophe/backtick
// outside quotes truncates the rest of that one line (a real end-of-line comment marker).
// Returns 0 on success, or a real script_error() code (2=label/STOP not found, 1=buffer overflow,
// 3=too many labels).
// ---------------------------------------------------------------------------------------------
static int read_script_file(void) {
	char tmps[255];
	char temp_buff[255];
	char target[24];
	const unsigned char *sb = s_speak_data;
	const unsigned char *send = s_speak_data + s_speak_len;
	int cnt;

	if (!s_speak_data) {
		return 6; // real: res_read() failure code
	}
	s_buff_ptr = s_buffer;
	memset(s_buffer, 0, sizeof(s_buffer));

	snprintf(target, sizeof(target), "|%ld", s_scr_index);

	for (;;) {
		cnt = get_line(sb, send, tmps);
		sb += cnt;
		if (!strcmp(tmps, "|EOF") || cnt == 0) {
			return 2; // real: label never found
		}
		if (!strcmp(tmps, target)) {
			break;
		}
	}

	s_num_labels = 0;
	for (;;) {
		cnt = get_line(sb, send, tmps);
		if (!strcmp(tmps, "|STOP")) {
			if (s_buff_ptr != s_buffer) {
				s_buff_end = s_buff_ptr;
				return 0;
			}
			return 2;
		}
		sb += cnt;
		{
			int len = (int) strlen(tmps);
			bool quote_flag = false;
			int p = 0;
			int i;
			if (len < 2) {
				*s_buff_ptr++ = 0;
				continue;
			}
			for (i = 0; i < len; ++i) {
				char ch = tmps[i];
				if (ch == '"') {
					quote_flag = !quote_flag;
				} else if (ch == '\'' && !quote_flag) {
					break; // real end-of-line comment marker (checks 39 or 96; DOS source only
				} else if (ch == '`' && !quote_flag) { // ever seems to author with ', kept both)
					break;
				}
				if (!quote_flag) {
					ch = (char) toupper((unsigned char) ch);
				}
				if (quote_flag || (unsigned char) ch > 32) {
					temp_buff[p++] = ch;
				}
			}
			temp_buff[p] = 0;
			len = (int) strlen(temp_buff);
			if (len > 0 && len < 10 && temp_buff[len - 1] == ':') {
				temp_buff[len - 1] = 0;
				if (s_num_labels >= MAX_LABELS) {
					return 3;
				}
				s_line_ptr[s_num_labels] = s_buff_ptr;
				snprintf(s_line_label[s_num_labels], 9, "%s", temp_buff);
				++s_num_labels;
				*s_buff_ptr++ = 0;
				continue;
			}
			if (s_buff_ptr + len + 2 >= s_buffer + SCR_BUFF_SIZE) {
				return 1;
			}
			strcpy(s_buff_ptr, temp_buff);
			s_buff_ptr += len;
			*s_buff_ptr++ = 0;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Real skip_colon()/get_command() -- unchanged control-flow port.
// ---------------------------------------------------------------------------------------------
static bool skip_colon(void) {
	while (*s_buff_ptr == 0 || *s_buff_ptr == ':') {
		++s_buff_ptr;
		if (s_buff_ptr > s_buff_end) {
			return false;
		}
	}
	return true;
}

// Returns: -1 = real buffer end (ignore, "no END" is tolerated same as real), -2 = syntax error,
// 0 = a numeric/string variable assignment was just executed inline (caller does NOT call
// exec_command for this), >0 = a real command index for the caller to run via exec_command().
static int get_command(void) {
	int i, len;

	if (!skip_colon()) {
		return -1;
	}
	for (i = 0; s_scr_command[i]; ++i) {
		len = (int) strlen(s_scr_command[i]);
		if (!strncmp(s_buff_ptr, s_scr_command[i], (size_t) len)) {
			s_buff_ptr += len;
			return i;
		}
	}
	if (isalpha((unsigned char) *s_buff_ptr)) {
		if (*(s_buff_ptr + 1) == '=') {
			int idx = *s_buff_ptr - 'A';
			s_buff_ptr += 2;
			if (!calc_value()) {
				return -2;
			}
			s_num_var[idx] = s_lvalue;
			return 0;
		}
		if (*(s_buff_ptr + 1) == '$' && *(s_buff_ptr + 2) == '=') {
			int idx = *s_buff_ptr - 'A';
			int ret;
			s_buff_ptr += 3;
			ret = calc_string(0);
			if (ret == 0) {
				return -2;
			}
	if (strlen(s_temps) > 80) {
				return -2;
			}
			strcpy(s_str_var[idx], s_temps);
			return 0;
		}
	}
	return -2;
}

// ---------------------------------------------------------------------------------------------
// Real calc_value()/get_next_val()/get_internal_variable() -- the real numeric-expression grammar
// (left-to-right, no operator precedence, exactly as real GoT scripts are written and real
// calc_value() evaluates them) and the real "@" internal-variable table.
// ---------------------------------------------------------------------------------------------
static bool get_internal_variable(void) {
	int i, len;

	for (i = 0; s_internal_variable[i]; ++i) {
		len = (int) strlen(s_internal_variable[i]);
		if (!strncmp(s_buff_ptr, s_internal_variable[i], (size_t) len)) {
			s_buff_ptr += len;
			break;
		}
	}
	if (!s_internal_variable[i]) {
		return false;
	}
	switch (i) {
		case 0: s_ltemp = got_thor_get_jewels(); break;
		case 1: s_ltemp = got_thor_get_health(); break;
		case 2: s_ltemp = got_thor_get_magic(); break;
		case 3: s_ltemp = got_thor_get_score(); break;
		case 4: s_ltemp = got_get_current_level(); break;
		case 5: s_ltemp = got_thor_get_keys(); break;
		case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
		case 16: case 17: case 18: case 19: case 20: case 21:
			// Real: `ltemp=(long)(i-5l);` -- the real numeric SOUND-command constant @OW..
			// @EXPLODE resolve to (this port's own SOUND_OW=0..SOUND_EXPLODE=15, 1-based here).
			s_ltemp = i - 5;
			break;
		case 22: { // @FLAG(n)
			int n, byte_idx, bit;
			if (!calc_value()) {
				return false;
			}
			n = (int) s_lvalue;
			if (n < 1 || n > 64) {
				return false;
			}
			byte_idx = n / 8;
			bit = n % 8;
			s_ltemp = (s_setup_flags[byte_idx] & (1 << bit)) ? 1 : 0;
			break;
		}
		case 23: // @ITEM -- real: `if(thor_info.inventory & 64) ltemp=thor_info.object; else ltemp=0;`
			s_ltemp = got_thor_get_carried_item();
			break;
		case 24:
			s_ltemp = got_get_thor_tile();
			break;
		case 25:
			s_ltemp = got_get_thor_pos();
			break;
		default:
			return false;
	}
	return true;
}

static bool get_next_val(void) {
	char ch = *s_buff_ptr;
	char tmpstr[25];
	int t;

	if (ch == 0 || ch == ':') {
		return false;
	}
	if (ch == '@') {
		return get_internal_variable();
	}
	if (isalpha((unsigned char) ch)) {
		++s_buff_ptr;
		s_ltemp = s_num_var[ch - 'A'];
		return true;
	}
	if (strchr("0123456789-", ch)) {
		t = 0;
		tmpstr[t++] = ch;
		++s_buff_ptr;
		while (strchr("0123456789", *s_buff_ptr) && *s_buff_ptr != 0 && t < 24) {
			tmpstr[t++] = *s_buff_ptr++;
		}
		tmpstr[t] = 0;
		if (t > 10) {
			return false;
		}
		s_ltemp = atol(tmpstr);
		return true;
	}
	return false;
}

static bool calc_value(void) {
	long acc = 0;
	int exptype = 1;
	char ch;

	for (;;) {
		if (!get_next_val()) {
			return false;
		}
		switch (exptype) {
			case 0: acc = acc * s_ltemp; break;
			case 1: acc = acc + s_ltemp; break;
			case 2: acc = acc - s_ltemp; break;
			case 3: if (s_ltemp != 0) { acc = acc / s_ltemp; } break;
			default: break;
		}
		ch = *s_buff_ptr;
		switch (ch) {
			case '*': exptype = 0; break;
			case '+': exptype = 1; break;
			case '-': exptype = 2; break;
			case '/': exptype = 3; break;
			default:
				s_lvalue = acc;
				return true;
		}
		++s_buff_ptr;
	}
}

// Real get_str() -- copies a double-quoted literal (buff_ptr already sitting on the opening
// quote) into s_temps.
static void get_str(void) {
	int t = 0;
	++s_buff_ptr;
	for (;;) {
		if (*s_buff_ptr == '"' || *s_buff_ptr == 0) {
			s_temps[t] = 0;
			if (*s_buff_ptr == '"') {
				++s_buff_ptr;
			}
			return;
		}
		s_temps[t++] = *s_buff_ptr++;
	}
}

// Real calc_string(mode) -- concatenates one or more quoted literals / string-variable reads
// (joined with real "+") into s_temps; mode==1 also stops at a comma (real ASK's own option-list
// use). Returns false when buff_ptr isn't sitting on a string term at all (real cmd_say()'s own
// `while(calc_string(0))` loop uses this as its natural "no more SAY lines" end condition).
static bool calc_string(int mode) {
	char acc[255];
	acc[0] = 0;

	if (!skip_colon()) {
		return false;
	}
	for (;;) {
		if (*s_buff_ptr == '"') {
			get_str();
			if (strlen(acc) + strlen(s_temps) < 255) {
				strcat(acc, s_temps);
			}
		} else if (isalpha((unsigned char) *s_buff_ptr) && *(s_buff_ptr + 1) == '$') {
			int idx = *s_buff_ptr - 'A';
			if (strlen(acc) + strlen(s_str_var[idx]) < 255) {
				strcat(acc, s_str_var[idx]);
			}
			s_buff_ptr += 2;
		} else {
			return false;
		}

		if (*s_buff_ptr == 0 || *s_buff_ptr == ':') {
			++s_buff_ptr;
			break;
		}
		if (*s_buff_ptr == ',' && mode == 1) {
			break;
		}
		if (*s_buff_ptr == '+') {
			++s_buff_ptr;
			continue;
		}
		return false;
	}
	if (strlen(acc) > 255) {
		return false;
	}
	strcpy(s_temps, acc);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Real cmd_*() command handlers -- each returns 0 on success, a real script_error() index (1-11)
// on failure, or CMD_YIELD for the three that pause the whole interpreter on player input
// (SAY/TEXT/ITEMSAY, ASK, PAUSE).
// ---------------------------------------------------------------------------------------------

static int cmd_goto(void) {
	char s[255];
	char *p;
	int i, len;

	strcpy(s, s_buff_ptr);
	p = strchr(s, ':');
	if (p) {
		*p = 0;
	}
	len = (int) strlen(s);
	if (len == 0) {
		return 8;
	}
	for (i = 0; i < s_num_labels; ++i) {
		if (!strcmp(s, s_line_label[i])) {
			s_new_ptr = s_line_ptr[i];
			s_buff_ptr += len;
			return 0;
		}
	}
	return 8;
}

static int cmd_if(void) {
	long v1, v2;
	int exptype;
	char ch;

	if (!calc_value()) {
		return 5;
	}
	v1 = s_lvalue;
	exptype = (unsigned char) *s_buff_ptr;
	++s_buff_ptr;

	ch = *s_buff_ptr;
	if (ch == '<' || ch == '=' || ch == '>') {
		if (exptype == *s_buff_ptr) {
			return 5;
		}
		exptype += (unsigned char) *s_buff_ptr;
		++s_buff_ptr;
	}
	if (!calc_value()) {
		return 5;
	}
	v2 = s_lvalue;
	s_buff_ptr += 4; // real: skip past " THEN" (matching real's own fixed 4-byte skip)

	{
		bool result;
		switch (exptype) {
			case '<': result = v1 < v2; break;
			case '=': result = v1 == v2; break;
			case '>': result = v1 > v2; break;
			case '<' + '=': result = v1 <= v2; break;
			case '<' + '>': result = v1 != v2; break;
			case '>' + '=': result = v1 >= v2; break;
			default: return 5;
		}
		if (!result) {
			while (*s_buff_ptr != 0) { ++s_buff_ptr; }
			while (*s_buff_ptr == 0) { ++s_buff_ptr; }
			if (!strncmp(s_buff_ptr, "ELSE", 4)) {
				s_buff_ptr += 4;
			}
		}
	}
	return 0;
}

static int cmd_run(void) {
	if (!calc_value()) {
		return 5;
	}
	++s_buff_ptr;
	s_scr_index = s_lvalue;
	return -100;
}

static int cmd_addjewels(void) {
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	got_thor_add_jewels((int) s_lvalue);
	return 0;
}
static int cmd_addhealth(void) {
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	got_thor_add_health((int) s_lvalue);
	return 0;
}
static int cmd_addmagic(void) {
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	got_thor_add_magic((int) s_lvalue);
	return 0;
}
static int cmd_addkeys(void) {
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	got_thor_add_keys((int) s_lvalue);
	return 0;
}
static int cmd_addscore(void) {
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	got_thor_add_score((int) s_lvalue);
	return 0;
}

// Real cmd_say(mode,type) -- mode selects real ITEMSAY's leading item-number argument, type is real
// display_speech()'s own `tf` sound-on-reveal flag. Builds the same real newline-joined tmp_buff real
// cmd_say() does, then opens the dialogue box instead of calling display_speech() synchronously --
// see got_script_update()'s own comment for how the interpreter loop unwinds around this. The
// ITEMSAY item argument's own icon overlay is now drawn too (Jormangund boss-fight overhaul, wootbeer:
// "there should be an icon for the armor and hammer on that popup next to odin's face icon") -- real
// `obj=(int)lvalue; if(obj<0||obj>32) return 6; if(obj) obj+=10;` computed here exactly as real code
// does, then threaded through to got_dialogue_open()'s own `icon_obj` parameter (see that function's
// header comment, got_dialogue.h, for the resulting 0-based-OBJECTS-index convention).
static int cmd_say(int mode, int type) {
	char *p = s_tmp_buff;
	int obj = 0;

	if (mode) {
		if (!calc_value()) { return 5; }
		++s_buff_ptr;
		obj = (int) s_lvalue;
		if (obj < 0 || obj > 32) { return 6; }
		if (obj) { obj += 10; }
	}

	memset(s_tmp_buff, 0, sizeof(s_tmp_buff));
	while (calc_string(0)) {
		size_t len = strlen(s_temps);
		if ((p - s_tmp_buff) + (long) len + 2 >= TMP_SIZE) {
			break; // defensive -- real TMP_SIZE=5800 is generous, this just avoids overflow
		}
		strcpy(p, s_temps);
		p += len;
		*p++ = '\n';
	}
	if (p > s_tmp_buff) {
		*(p - 1) = 0;
	} else {
		*p = 0;
	}
	got_dialogue_open(s_tmp_buff, s_scr_pic, type != 0, obj);
	return CMD_YIELD;
}

// Real cmd_ask() -- parses "A,"title",opt1,opt2,..." exactly like real code, then opens the ASK
// box instead of calling select_option() synchronously; got_script_update() reads the result back
// into num_var[v] once the box closes (see its own comment).
static int cmd_ask(void) {
	int v, p, i;
	static char title[41];
	static char opts[10][41];
	static const char *op[10];

	memset(opts, 0, sizeof(opts));

	if (!skip_colon()) { return 5; }
	if (!isalpha((unsigned char) *s_buff_ptr)) { return 5; }
	v = *s_buff_ptr - 'A';
	++s_buff_ptr;
	if (*s_buff_ptr != ',') { return 5; }
	++s_buff_ptr;

	if (!calc_string(1)) { return 5; }
	snprintf(title, sizeof(title), "%s", s_temps);

	if (*s_buff_ptr != ',') { return 5; }
	++s_buff_ptr;
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	p = (int) s_lvalue;

	i = 0;
	while (calc_string(0)) {
		snprintf(opts[i], sizeof(opts[i]), "%s", s_temps);
		op[i] = opts[i];
		++i;
		if (i > 9) { return 3; }
	}
	if (p > i) { p = 0; }
	(void) p; // real's own default-selected-option index (`select_option(...,p-1)`) -- not wired
			  // up yet, got_ask_open() always starts on option 0; parsed anyway for correctness.

	s_pending_ask_var = v;
	got_ask_open(title, op, i);
	return CMD_YIELD;
}

static int cmd_sound(void) {
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	if (s_lvalue < 1 || s_lvalue > 16) { return 6; }
	got_play_sound((int) s_lvalue - 1);
	return 0;
}

// Real cmd_settile() -- PLACETILE screen,pos,tile. Fully wired to this port's own room-tile
// buffers via got_script_place_tile() (got_main.c) -- see that function's own comment for exactly
// what it does for the current room vs. a different, not-currently-loaded one.
static int cmd_settile(void) {
	int screen, pos, tile;

	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	screen = (int) s_lvalue;
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	pos = (int) s_lvalue;
	if (!calc_value()) { return 5; }
	tile = (int) s_lvalue;
	if (screen < 0 || screen > 119) { return 6; }
	if (pos < 0 || pos > 239) { return 6; }
	if (tile < 0 || tile > 230) { return 6; }
	got_script_place_tile(screen, pos % 20, pos / 20, tile);
	return 0;
}

// Real cmd_itemgive(i): `thor_info.inventory|=64; thor_info.item=7; thor_info.object=i;
// display_item(); thor_info.object_name=object_names[thor_info.object-1];` -- the display_item()/
// object_name half is real status-panel bookkeeping this port's got_hud.c doesn't need (see
// ThorState's own comment in got_main.c); got_thor_give_item() covers the three fields that matter.
static int cmd_itemgive(void) {
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	if (s_lvalue < 1 || s_lvalue > 15) { return 6; }
	got_thor_give_item((int) s_lvalue);
	return 0;
}

// Real cmd_itemtake() just calls real delete_object() -- see got_thor_take_item()'s own comment.
static int cmd_itemtake(void) {
	got_thor_take_item();
	return 0;
}

// Non-static twin of cmd_setflag()'s own bit-set, factored out for got_main.c (see got_script.h's
// own comment: got_special_tile_thor()'s case 203, Episode 2/3's real Electric-Saw dialogue tile,
// needs to set real SETUP flag 10 directly -- the same @FLAG/SETFLAG mechanism the script language
// already exposes, just from outside a script). Same 1-64 bounds guard as cmd_setflag() itself;
// silently no-ops out of range rather than asserting, matching every other defensive bounds check in
// this file.
void got_script_set_flag(int n) {
	int byte_idx, bit;
	if (n < 1 || n > 64) {
		return;
	}
	byte_idx = n / 8;
	bit = n % 8;
	s_setup_flags[byte_idx] |= (unsigned char) (1 << bit);
}

static int cmd_setflag(void) {
	int n;
	if (!calc_value()) { return 5; }
	n = (int) s_lvalue;
	if (n < 1 || n > 64) { return 6; }
	got_script_set_flag(n);
	return 0;
}

// Non-static twin of get_internal_variable()'s own case 22 (@FLAG) bit test -- same s_setup_flags
// array, same byte/bit arithmetic, exposed for got_main.c (see got_script.h's own comment on why:
// got_pick_up_object()'s real HERMIT_HAS_DOLL guard is just real SETUP.f04, i.e. this same flag 4).
bool got_script_test_flag(int n) {
	int byte_idx, bit;
	if (n < 1 || n > 64) {
		return false;
	}
	byte_idx = n / 8;
	bit = n % 8;
	return (s_setup_flags[byte_idx] & (1 << bit)) != 0;
}

// Raw copy in/out of the whole s_setup_flags array, for got_main.c's own save/load
// (got_save_write_slot()/got_save_read_slot_full()) -- see that struct field's own comment. Plain
// byte-for-byte copies (not per-bit accessors like got_script_test_flag()/cmd_setflag() above) since
// the save format just wants the same 9 raw bytes back verbatim; sized explicitly (not `sizeof`
// against a caller-side array) so a mismatched buffer size is a compile error, not a silent
// over/under-copy.
void got_script_get_flags(unsigned char out[9]) {
	memcpy(out, s_setup_flags, sizeof(s_setup_flags));
}
void got_script_set_flags(const unsigned char in[9]) {
	memcpy(s_setup_flags, in, sizeof(s_setup_flags));
}

// Resets every real SETUP flag to unset -- a brand new game starts with none of them set, regardless
// of whatever a previously active slot/episode this same app session last left them at (this array
// is a single, per-process global, not per-slot -- see s_boss_dead_flag[]'s/s_thor_armor_override's
// own identical reset in got_spawn_new_thor(), got_main.c, added for the exact same reason and called
// alongside this one). Real setup_player()/init_setup() (1_init.c) zero the whole real SETUP struct
// for the same reason on a real new game.
void got_script_clear_flags(void) {
	memset(s_setup_flags, 0, sizeof(s_setup_flags));
}

static int cmd_ltoa(void) {
	int sv;
	char str[21];

	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	if (!isalpha((unsigned char) *s_buff_ptr) || *(s_buff_ptr + 1) != '$') { return 5; }
	sv = *s_buff_ptr - 'A';
	s_buff_ptr += 2;
	snprintf(str, sizeof(str), "%ld", s_lvalue);
	strcpy(s_str_var[sv], str);
	return 0;
}

// Real cmd_pause(n) -- real pause() spins on a real DOS timer tick counter (`while(timer_cnt<
// delay) rotate_pal();`), confirmed (got_dialogue.c's own REVEAL_STEP_MS comment) to tick at ~120Hz,
// not DOS's stock ~18.2Hz INT 8 rate -- so `n` real timer_cnt ticks is `n * PAUSE_STEP_MS` of real
// elapsed time. Paced against wall-clock time (see SCRIPT_WAIT_PAUSE's own got_script_update() case)
// instead of a per-render-call frame count -- see s_pause_deadline_ms's own comment, above, for why a
// frame count was wrong (the exact same render-rate-dependent bug REVEAL_STEP_MS already fixed
// elsewhere, just never converted here too until now).
#define PAUSE_STEP_MS (1000.0 / 120.0)
static int cmd_pause(void) {
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	if (s_lvalue < 1 || s_lvalue > 65535L) { return 6; }
	s_pause_deadline_ms = (double) s_lvalue * PAUSE_STEP_MS;
	s_pause_time_inited = false; // SCRIPT_WAIT_PAUSE's own first call starts the real-time clock
	return CMD_YIELD;
}

// Real Episode 3 scr_func1()/scr_func2() (3_script.c) -- the Lokisburg red-guard arrest. SPEAK3's
// own |101 script (run by every REDGD's |<level*1000+actor_num> dialogue label -- see
// got_check_move0()'s func_num==11 dispatch in got_main.c) is:
//     pause 120 / exec 2 / c$="~1"+a$+"~0" / SAY "You are under arrest for:" c$ "" b$ / exec 1
// i.e. a short pause, then `exec 2` (scr_func2 below) fills the offense/reason pair into a$/b$
// (str_var[0]/str_var[1] -- s_str_var[0]/s_str_var[1] here, real get_command()'s own
// `str_var[(*buff_ptr)-65]` mapping a$..z$ onto str_var[0..25]), then the SAY box actually displays
// them (this is wootbeer's "not the full dialogue" -- with scr_func2 an unbuilt no-op, a$/b$ were always
// empty, so the box read "You are under arrest for:\n\n" with nothing filled in), then `exec 1`
// (scr_func1 below) does the actual jail teleport. Real scr_func1(): `play_sound(FALL,1);
// if(key_flag[_FOUR]) return; new_level=109; new_level_tile=215; thor->x=(new_level_tile%20)*16;
// thor->y=((new_level_tile/20)*16)-2; ...; thor->show=2;` -- `key_flag[_FOUR]` is a real debug/
// cheat-key guard this port has no equivalent of (and every real player-facing call path leaves it
// unset, so it's simply skipped rather than ported); the teleport itself goes through
// got_script_warp_thor() (got_main.c's own got_warp_thor(), the same primitive that already powers
// warp holes and the Episode 3 finale's own level cut) rather than reimplementing it here. Real
// scr_func2() (`offense[]`/`reason[]` below, copied verbatim from 3_script.c) picks one random
// matched pair and writes it straight into str_var[0]/str_var[1] -- no return value, no side effect
// beyond that.
static const char *const s_arrest_offense[] = {
	"Cussing", "Rebellion", "Kissing Your Mother Goodbye", "Being a Thunder God",
	"Door-to-Door Sales", "Carrying a Concealed Hammer",
};
static const char *const s_arrest_reason[] = {
	"We heard you say 'Booger'.", "You look kind of rebellious.", "Your mother turned you in.",
	"We don't want you here.", "Nobody wants your sweepers.", "That's a dangerous weapon.",
};

// Real cmd_exec() -- dispatches to real scr_func1-5, each hardcoded to a specific real level's
// mechanics (the Lokisburg arrest's jail teleport, that same arrest's random excuse pair, the
// Shovel's real "dig here" mechanic, a thunder-flash flag, and the troll's own "step aside" position
// nudge). scr_func1/2/3/5 are all built; scr_func4 targets a mechanic no currently-loaded room needs,
// so this validates real's own argument range and logs it was hit rather than silently no-op'ing an
// out-of-range value. scr_func3 is IDENTICAL byte-for-byte between Episode 1's and Episode 2's own
// real script.c (Episode 3 has no scr_func3 at all -- its own scr_func[] table only holds scr_func1/
// scr_func2), so got_thor_dig() needs no episode gate of its own -- the same reasoning that already
// lets got_troll_shrub_slide() run un-gated for scr_func5 (Episode 1 only, but nothing outside
// Episode 1's own SPEAK1 script ever calls `exec 5`, and nothing outside Episode 3's own SPEAK3
// script ever calls `exec 1`/`exec 2`). Real scr_func3() communicates success back to its caller by
// setting `num_var[0]` ("a" in the SPEAK2 script text) to 1 -- see got_thor_dig()'s own comment for
// why that's ported as a plain bool return instead, applied to `s_num_var[0]` right here so
// got_main.c stays free of any got_script.c-internal script-variable state.
static int cmd_exec(void) {
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	if (s_lvalue < 1 || s_lvalue > 10) { return 6; }
	if (s_lvalue == 5) {
		got_troll_shrub_slide();
	} else if (s_lvalue == 3) {
		if (got_thor_dig()) {
			s_num_var[0] = 1; // real: num_var[0]=1; (variable "a") -- left untouched on failure,
							   // matching real scr_func3() itself never writing it on that path either
		}
	} else if (s_lvalue == 1) {
		got_play_sound(7); // real: play_sound(FALL,1); -- @FALL resolves to this same sound index
						    // (see get_internal_variable()'s own case 13, `i-5`) minus cmd_sound()'s
						    // own 1-based-to-0-based `-1`, i.e. (13-5)-1=7
		got_script_warp_thor(109, (215 % 20) * 16, (215 / 20) * 16 - 2); // real new_level/
			// new_level_tile=109/215, thor->x/y formula, LEVEL_COLS=20 -- see this function's own
			// top comment
	} else if (s_lvalue == 2) {
		int r = rand() % 6;
		strcpy(s_str_var[0], s_arrest_offense[r]); // real: str_var[0]=offense[r]; ("a$")
		strcpy(s_str_var[1], s_arrest_reason[r]);  // real: str_var[1]=reason[r];  ("b$")
	} else {
		LOGI("cmd_exec: scr_func%ld -- not built yet, see got_script.c's top comment", s_lvalue);
	}
	return 0;
}

// Real cmd_visible() -- real actor_visible() indexes the real actor[MAX_ACTORS] slot array
// directly by real actor_num; this port's sprites are a compact per-room list with no such stable
// lookup yet (see got_script.c's own top comment). Parses and validates exactly like real code.
static int cmd_visible(void) {
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	if (s_lvalue < 1 || s_lvalue > 16) { return 6; }
	LOGI("cmd_visible: actor slot %ld -- no actor_num lookup built yet, see got_script.c's top "
		 "comment", s_lvalue);
	return 0;
}

static int cmd_random(void) {
	int v, r;
	if (!isalpha((unsigned char) *s_buff_ptr)) { return 5; }
	v = *s_buff_ptr - 'A';
	++s_buff_ptr;
	if (*s_buff_ptr != ',') { return 5; }
	++s_buff_ptr;
	if (!calc_value()) { return 5; }
	++s_buff_ptr;
	r = (int) s_lvalue;
	if (r < 1 || r > 1000) { return 6; }
	s_num_var[v] = rand() % r;
	return 0;
}

// ---------------------------------------------------------------------------------------------
// Real exec_command(num) -- dispatches to the cmd_*() above by real scr_command[] index, then
// wraps each one's return the same real way: >0 is a real script_error() code (script stops),
// 0 continues, and (this port's own addition) CMD_YIELD passes straight through unwrapped so the
// driver loop below can tell "stop for now, but not the real end" apart from both of those.
// ---------------------------------------------------------------------------------------------
static int exec_command(int num) {
	int ret = 0;

	switch (num) {
		case 1: return 0; // END
		case 2: // GOTO
			ret = cmd_goto();
			if (!ret) { s_buff_ptr = s_new_ptr; }
			break;
		case 3: // GOSUB
			ret = cmd_goto();
			if (!ret) {
				if (s_gosub_ptr >= MAX_LABELS - 1) { ret = 10; break; }
				s_gosub_stack[++s_gosub_ptr] = s_buff_ptr;
				s_buff_ptr = s_new_ptr;
			}
			break;
		case 4: // RETURN
			if (!s_gosub_ptr) { ret = 9; break; }
			s_buff_ptr = s_gosub_stack[s_gosub_ptr--];
			break;
		case 5: { // FOR
			char ch;
			++s_for_ptr;
			if (s_for_ptr > 10) { ret = 10; break; }
			ch = *s_buff_ptr;
			if (!isalpha((unsigned char) ch)) { ret = 5; break; }
			s_for_var[s_for_ptr] = ch - 'A';
			s_buff_ptr += 2;
			if (!calc_value()) { ret = 5; break; }
			s_num_var[s_for_var[s_for_ptr]] = s_lvalue;
			s_buff_ptr += 2;
			if (!calc_value()) { ret = 5; break; }
			s_for_val[s_for_ptr] = s_lvalue;
			s_for_stack[s_for_ptr] = s_buff_ptr;
			break;
		}
		case 6: // NEXT
			if (!s_for_ptr) { ret = 11; break; }
			s_num_var[s_for_var[s_for_ptr]] = s_num_var[s_for_var[s_for_ptr]] + 1;
			if (s_num_var[s_for_var[s_for_ptr]] <= s_for_val[s_for_ptr]) {
				s_buff_ptr = s_for_stack[s_for_ptr];
			} else {
				--s_for_ptr;
			}
			break;
		case 7: ret = cmd_if(); break;
		case 8: // ELSE (real: skip to end of this command line, i.e. the true-branch just ran)
			while (*s_buff_ptr != 0) { ++s_buff_ptr; }
			break;
		case 9: return cmd_run(); // -100 (RUN) passes straight through
		case 10: ret = cmd_addjewels(); break;
		case 11: ret = cmd_addhealth(); break;
		case 12: ret = cmd_addmagic(); break;
		case 13: ret = cmd_addkeys(); break;
		case 14: ret = cmd_addscore(); break;
		case 15: return cmd_say(0, 1); // SAY
		case 16: return cmd_ask();     // ASK
		case 17: ret = cmd_sound(); break;
		case 18: ret = cmd_settile(); break;
		case 19: ret = cmd_itemgive(); break;
		case 20: ret = cmd_itemtake(); break;
		case 21: return cmd_say(1, 1); // ITEMSAY
		case 22: ret = cmd_setflag(); break;
		case 23: ret = cmd_ltoa(); break;
		case 24: return cmd_pause();
		case 25: return cmd_say(0, 0); // TEXT
		case 26: ret = cmd_exec(); break;
		case 27: ret = cmd_visible(); break;
		case 28: ret = cmd_random(); break;
		default: ret = 5; break;
	}
	if (ret > 0) {
		LOGE("got_script: error %d at scr_index=%ld", ret, s_scr_index);
		return 0;
	}
	return 1;
}

// ---------------------------------------------------------------------------------------------
// Real execute_script(index,pic) / the real while(1) command loop -- see got_script.h's own
// comment for how this is split across got_script_execute() (the real one-time setup: reset vars,
// find+decode the target label) and got_script_update() (the real while(1) loop itself, run once
// per frame up to the next real wait point instead of all the way through in one shot).
// ---------------------------------------------------------------------------------------------

void got_script_init(void) {
	int ep;

	for (ep = 1; ep <= GOT_NUM_EPISODES; ++ep) {
		char res_name[16];
		unsigned char *data = NULL;
		long len;

		snprintf(res_name, sizeof(res_name), "SPEAK%d", ep);
		len = res_read(res_name, &data);
		if (len < 0 || !data) {
			LOGE("got_script_init: %s read failed -- episode %d dialogue will do nothing", res_name,
				 ep);
			if (data) {
				free(data);
			}
			continue;
		}
		s_speak_data_by_episode[ep - 1] = data;
		s_speak_len_by_episode[ep - 1] = len;
		LOGI("got_script_init: real %s loaded (%ld bytes)", res_name, len);
	}

	// Episode 1 active by default -- matches got_main.c's own s_current_episode default, and keeps
	// every pre-existing call site correct even before any game has actually started/resumed.
	got_script_set_episode(1);
}

void got_script_set_episode(int episode) {
	if (episode < 1 || episode > GOT_NUM_EPISODES) {
		LOGE("got_script_set_episode: episode %d out of range, ignoring", episode);
		return;
	}
	s_speak_data = s_speak_data_by_episode[episode - 1];
	s_speak_len = s_speak_len_by_episode[episode - 1];
	LOGI("got_script_set_episode: active dialogue episode now %d (%ld bytes cached)", episode,
		 s_speak_len);
}

bool got_script_is_running(void) {
	return s_run_state != SCRIPT_IDLE;
}

static bool start_script(void) {
	memset(s_num_var, 0, sizeof(s_num_var));
	memset(s_str_var, 0, sizeof(s_str_var));
	s_num_labels = 0;
	memset(s_line_label, 0, sizeof(s_line_label));
	memset(s_line_ptr, 0, sizeof(s_line_ptr));
	memset(s_gosub_stack, 0, sizeof(s_gosub_stack));
	s_gosub_ptr = 0;
	memset(s_for_stack, 0, sizeof(s_for_stack));
	memset(s_for_val, 0, sizeof(s_for_val));
	memset(s_for_var, 0, sizeof(s_for_var));
	s_for_ptr = 0;

	if (read_script_file() != 0) {
		LOGE("got_script: read_script_file failed for scr_index=%ld", s_scr_index);
		return false;
	}
	s_buff_ptr = s_buffer;
	return true;
}

void got_script_execute(long index, const unsigned char *pic) {
	if (s_run_state != SCRIPT_IDLE || !s_speak_data) {
		return;
	}
	s_scr_index = index;
	s_scr_pic = pic;
	if (!start_script()) {
		return;
	}
	s_run_state = SCRIPT_RUNNING;
	// got_main.c's own got_demo_tick_paced() dedicated real-time accumulator -- reset it right here,
	// the single unambiguous point a script/dialogue transitions from idle to actually running, so it
	// always starts this fresh dialogue at "zero backlog, clock starts now" instead of fast-forwarding
	// through however much ordinary (non-script) gameplay time passed since whatever dialogue last
	// closed. See that function's own comment for the full writeup.
	got_demo_pace_reset();
	LOGI("got_script_execute: running |%ld", index);
}

// The real while(1) command loop, run up to the next real wait point (or script end) rather than
// straight through -- see got_script.h's own comment. Real RUN (case 9, `goto run_script;` in the
// real source) is ported as re-running start_script() with the new s_scr_index and looping again,
// matching real's own re-entry into the same while(1) without returning to the caller.
static void run_command_loop(void) {
	for (;;) {
		int cmd = get_command();
		if (cmd == -1) { // real: "ignore NO END error" -- ran off the end cleanly
			s_run_state = SCRIPT_IDLE;
			return;
		}
		if (cmd == -2) {
			LOGE("got_script: syntax error at scr_index=%ld", s_scr_index);
			s_run_state = SCRIPT_IDLE;
			return;
		}
		if (cmd == 0) {
			continue; // a numeric/string var assignment already ran inline in get_command()
		}
		{
			int ret = exec_command(cmd);
			if (ret == -100) { // RUN
				if (!start_script()) {
					s_run_state = SCRIPT_IDLE;
					return;
				}
				continue;
			}
			if (ret == CMD_YIELD) {
				if (cmd == 24) { // PAUSE
					s_run_state = SCRIPT_WAIT_PAUSE;
				} else if (cmd == 16) { // ASK
					s_run_state = SCRIPT_WAIT_ASK;
				} else { // SAY / ITEMSAY / TEXT
					s_run_state = SCRIPT_WAIT_DIALOGUE;
				}
				return;
			}
			if (!ret) {
				s_run_state = SCRIPT_IDLE;
				return;
			}
		}
	}
}

void got_script_update(void) {
	switch (s_run_state) {
		case SCRIPT_IDLE:
			return;
		case SCRIPT_RUNNING:
			run_command_loop();
			return;
		case SCRIPT_WAIT_DIALOGUE:
			if (got_dialogue_is_open()) {
				return;
			}
			// Bugfix (wootbeer, Lokisburg red-guard arrest round: "if i push my device's start button to
			// skip the whole dialogue, instead of pressing 'a' to cycle through the pages, the guard
			// doesn't finalize arresting / teleporting me"). This used to check
			// got_dialogue_was_cancelled() here and abort the WHOLE script (s_run_state=SCRIPT_IDLE)
			// the instant Thor's "start" button (this port's own KEY_ESC binding -- see
			// got_dialogue_update()'s own `esc`/close_dialogue(true) handling) skipped the SAY box
			// early, on the assumption that real ESC-out-of-a-SAY-box ends the whole real script too.
			// Checked that assumption directly against the real source and it's simply wrong: real
			// cmd_say() (1_script.c/3_script.c) calls `display_speech(obj,(char *)scr_pic,type);
			// d_restore(); return 0;` -- it never even LOOKS at display_speech()'s own return value
			// (0 if the player ESC'd out, 1 if they read the whole thing), unconditionally returning 0
			// ("continue the script normally") either way. Real cmd_ask() is the same shape:
			// `num_var[v]=select_option(op,title,p-1); d_restore(); return 0;`, ignoring whatever
			// select_option() itself did with an ESC press. So in real GoT, skipping a dialogue box
			// early never aborts the script that opened it -- for the Lokisburg arrest specifically
			// (SPEAK3's own `|101`: `pause 120/exec 2/.../SAY .../exec 1`, see cmd_exec()'s own
			// comment), that meant this port's own extra abort-on-cancel branch was the ONLY thing
			// standing between a skipped dialogue and the `exec 1` jail teleport that's supposed to
			// run right after it -- pressing "A" to advance normally (never triggering close_dialogue's
			// own `cancelled=true` path) reached exec 1 fine, while "start" to skip reliably ate it.
			// Simply falling through to SCRIPT_RUNNING regardless of how the box closed, matching real
			// cmd_say()/cmd_ask()'s own unconditional `return 0`, fixes this for every script that
			// opens a SAY/ASK box, not just this one -- got_dialogue_was_cancelled() is left in place
			// (got_dialogue.h's own public API, still tracked by close_dialogue()) in case some other,
			// more-real-faithful use for it turns up later, just no longer consulted here.
			s_run_state = SCRIPT_RUNNING;
			run_command_loop();
			return;
		case SCRIPT_WAIT_ASK:
			if (got_ask_is_open()) {
				return;
			}
			s_num_var[s_pending_ask_var] = got_ask_result();
			s_run_state = SCRIPT_RUNNING;
			run_command_loop();
			return;
		case SCRIPT_WAIT_PAUSE: {
			// Real-time deadline check, not a per-render-call frame count -- see s_pause_deadline_ms's
			// own comment (above s_run_state) for the render-rate-dependent bug this replaces. First
			// call after cmd_pause() just starts the clock (matching got_advance_game()'s own
			// s_tick_time_inited first-call behavior); every call after that compares total real
			// elapsed time against the deadline computed once, up front, when the PAUSE began -- an
			// absolute target rather than an incremental accumulator, since nothing needs to fire
			// partway through a PAUSE the way REVEAL_STEP_MS's own per-character reveal does.
			struct timespec now;
			double elapsed_ms;

			clock_gettime(CLOCK_MONOTONIC, &now);
			if (!s_pause_time_inited) {
				s_pause_start_time = now;
				s_pause_time_inited = true;
				return;
			}
			elapsed_ms = (double) (now.tv_sec - s_pause_start_time.tv_sec) * 1000.0
					+ (double) (now.tv_nsec - s_pause_start_time.tv_nsec) / 1.0e6;
			if (elapsed_ms < s_pause_deadline_ms) {
				return;
			}
			s_run_state = SCRIPT_RUNNING;
			run_command_loop();
			return;
		}
	}
}
