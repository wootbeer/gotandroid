// Real authentic status panel (Display Modes A/B) -- see got_panel.h for the overall design and
// scope.

#include "got_panel.h"
#include "got_font.h"
#include "modex.h"
#include "res_man.h"

#include <android/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "GotPanel"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Real STAT_COLOR (1_define.h: `#define STAT_COLOR 206`) -- the panel's own copper/marbled
// backdrop color, used to erase a digit/bar box back to bare background before redrawing it.
#define PANEL_STAT_COLOR 206

// Loaded once by got_panel_init(), never freed afterward -- this port's established policy for
// every other GOTRES.DAT resource loaded once at startup (BPICS1/SDAT1/OBJECTS/PALETTE/TEXT).
//
// Byte format verified directly against a real GOTRES.DAT: real xfarput()'s own 6-byte header
// (little-endian width-in-mode-X-"addresses" = 80, little-endian height = 48, 2 unused bytes) plus
// 4 sequential (80*48=3840)-byte bitplanes decoded to a 320x48 image and rendered to a contact
// sheet -- produced a clean, immediately recognizable panel background with legible "Health"/
// "Magic"/"Jewels"/"Keys"/"Score"/"Item" labels over their own bar/box areas, not noise (see
// got-android-port-notes_1.md for the full write-up, same verification approach already used for
// BPICS1/ACTOR/TEXT data in earlier rounds). This port's own GOTRES.DAT reports this resource as
// 16000 bytes -- comfortably more than the 15366 (6 + 4*3840) real xfarput() actually reads; the
// extra bytes are trailing slack in the archive real code never touches either, not a sign the
// decode above is wrong.
//
// The header size was originally (wrongly) implemented as 8 bytes, not 6 -- wootbeer caught this
// on-device ("the wood graphic part that is the background of it, is shifted a little bit... needs
// to go to the right a few pixels") after the background art's own drawn bar troughs came out
// visibly offset from display_health()'s/display_magic()'s real (unshifted) drawing coordinates.
// See modexgl.c's own xfarput() comment for the full root-cause writeup and byte-level re-
// verification against the real assembly.
static unsigned char *s_status_bytes = NULL;
static bool s_status_ready = false;

void got_panel_init(void) {
	long len = res_read("STATUS", &s_status_bytes);
	if (len < 8 || s_status_bytes == NULL) {
		LOGI("got_panel_init: STATUS read failed (%ld) -- Display Modes A/B will draw a flat "
			 "backdrop instead of the real panel art", len);
		if (s_status_bytes) {
			free(s_status_bytes);
		}
		s_status_bytes = NULL;
		s_status_ready = false;
		return;
	}
	s_status_ready = true;
	LOGI("got_panel_init: loaded real STATUS panel background (%ld bytes)", len);
}

bool got_panel_ready(void) {
	return s_status_ready;
}

void got_panel_draw(int health, int magic, int jewels, int keys, int score, int active_item,
					 int active_object, const unsigned char *objects_buf,
					 const unsigned char *hourglass_icon) {
	char s[24];
	int b, x, l;

	// Background -- redrawn fresh every frame (see this file's own header comment on why), rather
	// than ported real init_status_panel()'s "draw once, then let the dynamic bits below draw over
	// stale background" model. Falls back to a flat dark fill if the real art never loaded.
	if (s_status_ready) {
		xfarput(0, 0, MODEX_PANEL_PAGE, (char *) s_status_bytes);
	} else {
		xfillrectangle(0, 0, 320, 48, MODEX_PANEL_PAGE, PANEL_STAT_COLOR);
	}

	// display_health(): real `b=59+thor->health; xfillrectangle(59,8,b,12,PAGES,32);
	// xfillrectangle(b,8,209,12,PAGES,STAT_COLOR);` -- clamped here the same way got_draw_hud()'s
	// own doc comment says it clamps health/magic internally.
	if (health < 0) health = 0;
	if (health > 150) health = 150;
	b = 59 + health;
	xfillrectangle(59, 8, b, 12, MODEX_PANEL_PAGE, 32);
	xfillrectangle(b, 8, 209, 12, MODEX_PANEL_PAGE, PANEL_STAT_COLOR);

	// display_magic(): real `b=59+thor_info.magic; xfillrectangle(59,20,b,24,PAGES,96);
	// xfillrectangle(b,20,209,24,PAGES,STAT_COLOR);`
	if (magic < 0) magic = 0;
	if (magic > 150) magic = 150;
	b = 59 + magic;
	xfillrectangle(59, 20, b, 24, MODEX_PANEL_PAGE, 96);
	xfillrectangle(b, 20, 209, 24, MODEX_PANEL_PAGE, PANEL_STAT_COLOR);

	// display_jewels(): real itoa()+strlen()-based x position (70/66/62 for 1/2/3 digits), erase
	// box (59,32)-(85,42), draw with got_xprint() (this port's own real font, standing in for real
	// xprint()) in color 14.
	snprintf(s, sizeof(s), "%d", jewels);
	l = (int) strlen(s);
	x = (l == 1) ? 70 : (l == 2) ? 66 : 62;
	xfillrectangle(59, 32, 85, 42, MODEX_PANEL_PAGE, PANEL_STAT_COLOR);
	got_xprint(x, 32, s, MODEX_PANEL_PAGE, 14);

	// display_keys(): real x position 150/146/142 for 1/2/3 digits, erase box (139,32)-(165,42).
	snprintf(s, sizeof(s), "%d", keys);
	l = (int) strlen(s);
	x = (l == 1) ? 150 : (l == 2) ? 146 : 142;
	xfillrectangle(139, 32, 165, 42, MODEX_PANEL_PAGE, PANEL_STAT_COLOR);
	got_xprint(x, 32, s, MODEX_PANEL_PAGE, 14);

	// display_score(): real `x=276-(l*8);`, erase box (223,32)-(279,42).
	snprintf(s, sizeof(s), "%d", score);
	l = (int) strlen(s);
	x = 276 - (l * 8);
	xfillrectangle(223, 32, 279, 42, MODEX_PANEL_PAGE, PANEL_STAT_COLOR);
	got_xprint(x, 32, s, MODEX_PANEL_PAGE, 14);

	// display_item(): erase box (280,8)-(296,24); real:
	//   if(thor_info.item){
	//     if(thor_info.item==7) xfput(282,8,PAGES,(char far*)objects[thor_info.object+10]);
	//     else xfput(282,8,PAGES,(char far*)objects[thor_info.item+25]);
	//   }
	// Bugfix (wootbeer: "selecting an item other than the shrub isn't displaying it's icon in the status
	// panel"): an earlier round's own got_thor_get_displayed_item() wrongly gated this WHOLE block on
	// item==7 -- real code shows an icon whenever ANY item is active (1-7), just with two different
	// index formulas depending on which. `active_item`/`active_object` now arrive as the raw real
	// thor_info.item/thor_info.object values (got_main.c's own render-loop call site), so both real
	// branches are reproduced here directly. `objects[N]` is a plain 0-based real C array; this port's
	// own flat objects_buf mirrors it entry-for-entry (262 bytes per entry, same xfput-format 16x16
	// tile every other OBJECTS use in this port already assumes -- see got_main.c's own s_objects
	// comment), so `object+10`/`item+25` carry over as buffer offsets with no further adjustment --
	// confirmed consistent with got_pick_up_object()'s own real "case 27: thor_info.item=object_map[p]
	// -26" magic-item numbering (item 1=apple ... 6=thunder maps to objects[26..31], i.e. object
	// index 27-32 the way that pickup switch's own real object_map values already run).
	//
	// X position bugfix (wootbeer: "the graphic for items is not centered in status panel, it needs to
	// shift left a few pixels"): real code's own literal 282 sits 2px in from the erase box's own
	// left edge (280) -- since xfput() always draws a full 16x16 icon (see that function's own
	// comment) and the erase box is itself exactly 16px wide (280 to 296), drawing at 282 pushes the
	// icon's own right edge to 298, 2px PAST the box's right edge (296) it's supposed to stay inside,
	// while leaving an uneven 2px gap on the left -- genuinely off-center, not just a rounding
	// artifact of eyeballing it. Shifted to 280 (flush with the box's own left edge) so the icon
	// exactly fills its erase box edge-to-edge with no overflow on either side -- a deliberate, minor
	// departure from the real source's own literal coordinate, not a real-source mismatch.
	xfillrectangle(280, 8, 296, 24, MODEX_PANEL_PAGE, PANEL_STAT_COLOR);
	if (active_item == 8 && hourglass_icon != NULL) {
		// Item 8 ("Hourglass") -- see got_menu.h's own "Hourglass:" comment. Not a real item value at
		// all (real thor_info.item only ever runs 1-7), so it can't reuse either real formula above --
		// `item+25` would reach objects[33], past the real 32-entry OBJECTS resource entirely, and no
		// true hourglass shape exists anywhere in that resource regardless (all 32 entries decoded and
		// visually inspected). wootbeer supplied his own hand-made 16x16 hourglass.png instead (same
		// "draw custom wootbeer-made art directly, not from objects_buf" shape as the Tombstone
		// Enhancement -- see got_main.c's own s_custom_hourglass_tile comment for the conversion).
		xfput(280, 8, MODEX_PANEL_PAGE, (char *) hourglass_icon);
	} else if (active_item > 0 && objects_buf != NULL) {
		long idx = (active_item == 7) ? ((long) active_object + 10) : ((long) active_item + 25);
		xfput(280, 8, MODEX_PANEL_PAGE, (char *) (objects_buf + idx * 262));
	}
}
