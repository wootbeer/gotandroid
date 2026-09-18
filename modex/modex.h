#ifndef GOT_MODEX_H_
#define GOT_MODEX_H_

// GL-backed reimplementation of God of Thunder's VGA Mode X drawing primitives. The original
// declarations only ever existed as this header in every source release found -- the .c/.asm
// implementations were never present anywhere (see got-android-port-notes_1.md section 6a) --
// so this is a from-scratch implementation behind the same signatures, following the exact
// pattern Descore's own Android port used for its 2d/ogles/ backend: same function names and
// call sites as whatever ported game code eventually calls xpset/xshowpage/etc, zero changes
// needed there.
//
// Adapted from the original declarations in joncloud/got's src/utility/modex.h: dropped the
// DOS-era `far` pointer qualifier (meaningless on Android/ARM), and MaskedImage is now an
// opaque, port-defined type (below) rather than the original's VGA-alignment-optimized struct
// -- the masked-sprite primitives that use it are stubbed (see modexgl.c) until real ACTOR
// image data has actually been inspected and a masking representation designed to match it.

#include <stdbool.h>
#include <stddef.h>

// Opaque -- see the comment above. Nothing dereferences this yet.
typedef struct MaskedImage MaskedImage;

void xsetmode(void);
void xshowpage(unsigned int page);
void xfput(int x, int y, unsigned int pagebase, char *buff);
void xcopyd2dmasked(int SourceStartX, int SourceStartY, int SourceEndX, int SourceEndY,
					 int DestStartX, int DestStartY, MaskedImage *Source,
					 unsigned int DestPageBase, int DestBitmapWidth);
void xcopyd2dmasked2(int SourceEndX, int SourceEndY, int DestStartX, int DestStartY,
					  MaskedImage *Source, unsigned int DestPageBase);
void xcopys2d(int SourceStartX, int SourceStartY, int SourceEndX, int SourceEndY,
			  int DestStartX, int DestStartY, char *SourcePtr, unsigned int DestPageBase,
			  int SourceBitmapWidth, int DestBitmapWidth);
void xcopyd2d(int SourceStartX, int SourceStartY, int SourceEndX, int SourceEndY,
			  int DestStartX, int DestStartY, unsigned int SourcePageBase,
			  unsigned int DestPageBase, int SourceBitmapWidth, int DestBitmapWidth);
void xline(int x0, int y0, int x1, int y1, int color, int page);
unsigned int xcreatmaskimage(MaskedImage *ImageToSet, unsigned int DispMemStart, char *Image,
							  int ImageWidth, int ImageHeight, char *Mask);
void xpset(int X, int Y, unsigned int PageBase, int Color);
void xget(int x1, int y1, int x2, int y2, unsigned int pagebase, char *buff, int invis);
void xput(int x, int y, unsigned int pagebase, char *buff);
void xtext(int x, int y, unsigned int pagebase, char *buff, int color);
void xfillrectangle(int StartX, int StartY, int EndX, int EndY, unsigned int PageBase,
					 int Color);
unsigned int xcreatemaskimage(MaskedImage *ImageToSet, unsigned int DispMemStart, char *Image,
							   int ImageWidth, int ImageHeight, char *Mask);
unsigned int xcreatmaskimage2(MaskedImage *ImageToSet, unsigned int DispMemStart, char *Image,
							   int ImageWidth, int ImageHeight, char *Mask);
unsigned int xpoint(int X, int Y, unsigned int PageBase);

// Direct generalization of xfput() (hardcoded to 16x16 tiles) to the arbitrary width/height real
// xfarput() blits full background images with -- ported from utility/g_asm.asm's own
// xfarput/xfarput_plane: a 6-byte header (little-endian width-in-mode-X-"addresses", little-endian
// height, then 2 unused/reserved bytes real code advances past without reading -- the exact same
// 3-little-endian-16-bit-word shape xfput()'s own header already uses), followed by 4 sequential
// VGA mode-X bitplanes of (width_addr * height) bytes each -- the same per-pixel plane/column
// addressing xfput() already uses, generalized to this image's own width/height instead of xfput's
// hardcoded 4 addresses / 16 rows. Format verified directly against the real GOTRES.DAT "STATUS"
// resource (see modexgl.c's own comment above this function's definition for the byte-level
// verification writeup, including a real on-device bug this exact header-size question caused):
// decoding it this way produced a clean, immediately recognizable status-panel background (legible
// "Health"/"Magic"/"Jewels"/"Keys"/"Score"/"Item" labels and their bar/box areas lining up exactly
// with display_health()'s/etc.'s own real drawing coordinates), not noise and not merely legible-
// but-offset. Unlike xfput()/xput() (sprite/tile blits, where palette index 0 or 15 means
// "see-through"), this is a plain unconditional copy with no transparency at all -- matches real
// xfarput_plane's own assembly exactly (`mov al,[bx]; mov es:[di],al`, no comparison whatsoever),
// appropriate for a full opaque background image meant to completely cover its target.
void xfarput(int x, int y, unsigned int pagebase, char *buff);

// --- Port-specific additions -- not part of the original DOS API ---------------------------

// Sets the current 256-color palette used to convert page buffers to RGB when presented. Takes
// 256 packed {r,g,b} byte triples, each channel already 0-255 -- NOT the VGA DAC's native
// 6-bit-per-channel range. Scale up first (e.g. val*255/63) once real palette data is loaded
// from a GOTRES.DAT PALETTE resource -- see notes section 6.5.
void modex_set_palette(const unsigned char rgb[256][3]);

// Uploads whatever page xshowpage() last selected to the screen and presents it. Must be
// called once per frame from the render loop (got_main.c) -- xshowpage() itself only records
// *which* page is now the display page (matching the real VGA hardware's "just flip the CRTC
// start register" cost), so the actual pixels shown reflect whatever that page's buffer holds
// *right now*, same as the display continuously scanning out the current page on real hardware.
void modex_present_frame(void);

// wootbeer: "when minimizing then restoring the game window, or locking the device then restoring,
// the view of the game will go solid white, but music will continue playing." Call this once,
// right after got_main.c's own got_show_render_buffer() tears down and recreates the EGL
// context/surface following a destroyed Android Surface (see that function's own
// Surface_was_destroyed branch) -- BEFORE the next modex_present_frame() call. modex_present_frame()
// lazily creates its one GL texture object on first use (see modexgl.c's own ensure_texture()) and
// then reuses that same numeric texture name forever, on the assumption that the GL context it was
// created in is still current -- true for the whole rest of a normal run, but not across a
// destroyed-and-recreated EGL context: destroying the context destroys every GL object that
// belonged to it, texture included, and the new context doesn't recognize that same numeric name.
// Binding a name the current context never created doesn't fail loudly -- it silently produces an
// incomplete texture with no image data, which is exactly the solid-white/solid-black screen wootbeer
// saw (music kept playing because MediaPlayer lives entirely outside this GL state and was never
// touched). This function just resets that cached name back to 0, so ensure_texture()'s own
// "already created" check correctly falls through and creates a fresh texture in the new context
// next frame, instead of reusing a defunct one.
void modex_reset_gl_state(void);

// Draws a `width`x`height` chunky sprite frame (one byte per pixel, a palette index, row-major --
// exactly the format real ACTOR pic[]/shot[] frames decode to, see got-android-port-notes_1.md
// section 2a) at (x,y) on the given page, treating palette indices 0 AND 15 as transparent
// (skipped, not written) -- the same dual convention already found for BPICS background tiles
// (xfput), confirmed to apply to ACTOR sprite art too after a real white box showed up around a
// second test actor whose own frame data uses 15, not 0, as its background (see modexgl.c's
// comment on this function for the real pixel-data evidence). This is the port's replacement for
// the original's xdisplay_actors()/xerase_actors():
// those used a VGA-latch/bit-mask-register hardware trick (four precomputed 4-pixel-aligned
// copies of each frame) purely to dodge a mode-X addressing constraint that doesn't exist on
// GL -- a modern CPU/GPU can composite an arbitrarily-positioned sprite directly, so there's
// nothing to port there, just this. Not part of the original DOS API -- like modex_set_palette()
// and modex_present_frame(), a port-specific addition. Writes straight into the page's indexed
// pixel buffer (the same one xpset/xfput/etc. write), so it's automatically subject to whatever
// modex_present_frame() does with the current palette that frame -- palette effects (flashes,
// fades) apply to sprites exactly like they do to backgrounds, with no separate code path.
void modex_draw_sprite_frame(int x, int y, unsigned int pagebase, const unsigned char *chunky,
							  int width, int height);

// Reserved pagebase value for the real status panel's own background art (STATUS resource) and
// its dynamic overlays (health/magic bars, jewels/keys/score digits, the carried-item icon) -- see
// got_panel.c. Deliberately a value no real DOS pagebase constant (PAGE0-PAGE3: 3840/19280/34720/
// 50160) or anything else in this port could ever collide with, so every existing xfillrectangle/
// xfput/xfarput/xget/xput/xpset/xtext call site keeps working completely unchanged -- this is
// simply one more addressable "page" alongside them, sized identically to every other page (see
// modexgl.c), even though only its own top 48 rows (the real STATUS panel's height) are ever
// actually drawn into or read back out of it.
#define MODEX_PANEL_PAGE 90210u

// The three real status-panel display modes wootbeer chose to build (see got-android-port-notes_1.md's
// own design writeup) -- what got_main.c's render loop passes to modex_set_display_mode() below,
// driven by got_menu.c's own new "Display Mode" options-menu row (got_menu_display_mode()).
//   A_PILLARBOX: the real authentic panel (got_panel.c), at its true 320x240 4:3 aspect --
//                pillarboxed (black bars left/right) rather than stretched on a wider screen.
//   B_STRETCH:   the same real authentic panel, stretched together with the playfield to fill the
//                screen edge to edge (no black bars) -- the panel keeps its real 48/240 (20%)
//                share of the combined image's height, same as it would on a real 4:3 CRT, just
//                with the whole picture stretched wider to fill a 16:9 screen.
//   C_MINIMAL:   this port's own from-scratch minimal HUD (got_hud.c) instead of the real panel --
//                drops the fixed panel reserve entirely so the playfield alone fills virtually the
//                whole screen, with a slim translucent overlay drawn on top afterward.
// Defaults to C_MINIMAL if modex_set_display_mode() is never called (this port's only behavior
// before this round) -- got_main.c calls it fresh every frame during actual gameplay regardless,
// matching this port's established "read state fresh each frame" convention, so there's no
// initialization-order dependency on that default in practice.
typedef enum {
	MODEX_DISPLAY_MODE_A_PILLARBOX = 0,
	MODEX_DISPLAY_MODE_B_STRETCH = 1,
	MODEX_DISPLAY_MODE_C_MINIMAL = 2,
} ModexDisplayMode;

// Selects which of the three modes above modex_present_frame() composites/presents this frame.
// Cheap enough to call unconditionally every frame (a single stored enum, see modexgl.c) --
// got_main.c does exactly that during real gameplay, and forces C_MINIMAL outside it (the title/
// player-select screens were laid out for the plain full-stretch 320x192 canvas C_MINIMAL already
// gives them, long before this feature existed -- forcing it there avoids needing a second, title-
// screen-specific version of the real panel just to avoid showing stale/blank panel content before
// a game has even started).
void modex_set_display_mode(ModexDisplayMode mode);

// Title/player-select/episode-select/credits/BBS-info/high-scores/story screens (got_title.c/
// got_story.c) are always forced to Mode C (modex_set_display_mode()'s own comment) -- which, same
// as gameplay Mode B, always stretches its plain 320x192 canvas to fill the physical screen with no
// aspect correction at all. That's a fine, deliberate choice for gameplay Mode B/C (a real user
// setting, "fill the screen edge to edge"), but the menu/title/story screens never got a choice --
// they just inherited whatever Mode C already did. wootbeer, after asking whether these screens would
// "still scale and look ok" on a true 4:3 screen (a Retroid Pocket Nova, unlike his own more
// 16:9-ish Retroid Pocket 6): confirmed they wouldn't -- stretching this port's real 320x192 (5:3)
// canvas to fill a 4:3 screen squeezes everything about 20% narrower than intended.
//
// Call once per frame with `enabled=true` whenever got_title_state() != GOT_APP_PLAYING (got_main.c's
// own render loop, right alongside its existing modex_set_display_mode() call for the same
// condition) and modex_present_frame() pillarboxes/letterboxes the plain 320x192 canvas to its own
// real 5:3 aspect instead of stretching it -- the exact same black-bar math Mode A already uses for
// gameplay's own 320x240/4:3 image, just targeting this canvas's own different real aspect. Call with
// `enabled=false` during actual gameplay (Modes A/B/C all keep their own existing real behavior
// completely unchanged -- this flag only ever affects the menu/title/story rendering path).
void modex_set_menu_pillarbox(bool enabled);

// Mirror Mode (Enhancements submenu toggle, got_menu.c's own got_menu_enhancement_mirror_mode_enabled()).
// When enabled, modex_present_frame() horizontally flips the PLAYFIELD portion of whatever page
// xshowpage() last selected before presenting it -- a pure read-time flip of the pixels being uploaded
// to the screen this frame, never written back into the page buffer itself, so every other draw/erase/
// collision call site keeps working in ordinary, un-mirrored coordinates with no changes anywhere.
// The status panel (MODEX_PANEL_PAGE) and every screen-space overlay (minimal HUD, boss health bar,
// touch D-pad) are outside this flip entirely, so they're already guaranteed to stay un-flipped.
//
// got_main.c's render loop calls this with `enabled=true` continuously while Mirror Mode is on and a
// game is actually playing (title/player-select/etc. screens always pass false) -- see
// modex_set_mirror_exclude_rect() just below for how the pause menu/item picker/dialogue box/ASK
// prompt stay un-flipped without ever needing this to be switched off.
void modex_set_mirror_mode(bool enabled);

// Mirror Mode's own GUI exemption -- wootbeer, after playtesting the first round (which simply forced
// modex_set_mirror_mode(false) whenever the pause menu/item picker/a dialogue box was open): "the
// mirror mode works as described, but the brief un-mirror is pretty noticeable, not really during
// pausing, but it happens during dialogue and other pop-ups too I assume. is it too hard to work
// around this?" It wasn't -- fixed properly instead.
//
// The pause menu (got_menu_draw()), item picker (got_item_menu_draw()), dialogue box
// (got_dialogue_draw()), and ASK prompt (got_ask_draw()) all paint their own box directly into the
// SAME page buffer as the game world, with no separate compositing layer (see got_main.c's own
// comment on that) -- but each one restores/redraws the room's own clean background (plus every
// sprite) FRESH, every single frame it's open, before drawing its own box on top (got_menu_draw()'s
// own comment on why). That means, on any frame one of these is open, the page buffer holds exactly
// two things: the live (or frozen, for dialogue/ASK) world filling the WHOLE canvas, and that one
// box's own fixed-position pixels painted over a small rectangular region of it. Since that region's
// screen position and size are already known exactly (each of the four getters below --
// got_menu_get_box_rect()/got_item_menu_get_box_rect()/got_dialogue_get_box_rect()/
// got_ask_get_box_rect() -- reports its own box's current outer rect, screen coordinates, whenever
// it's open), modex_present_frame() can flip everything OUTSIDE that one rect while leaving pixels
// INSIDE it exactly where they already are -- the world stays mirrored right up to the box's own
// edge, and the box itself (and whatever it shows) never moves or flips at all, satisfying wootbeer's
// original "keep all text, dialogue, menus, pop-ups, icons, all versions of the GUI... still
// un-flipped" requirement without ever needing to turn mirroring off.
//
// Call once per frame, right alongside modex_set_mirror_mode() -- got_main.c's render loop checks
// all four getters above (at most one is ever open at a time, matching this port's established
// mutually-exclusive menu/dialogue/item-picker nesting) and passes whichever one fired, or
// `active=false` with the other four arguments ignored when none are open (ordinary gameplay, death
// spin, lightning, a phase transition -- no box to protect, the whole canvas mirrors). Harmless to
// call with `active=false` even while Mirror Mode itself is off -- modex_present_frame() only ever
// consults this when s_mirror_mode is also true.
void modex_set_mirror_exclude_rect(bool active, int x1, int y1, int x2, int y2);

// Records the real on-screen pixel dimensions modex_present_frame() needs to compute Mode A's own
// pillarbox/letterbox black-bar geometry (comparing the device's actual aspect ratio against the
// real panel's true 320x240 4:3 one), and modex_set_menu_pillarbox()'s own 320x192/5:3 geometry the
// same way -- call once at startup (got_main.c's gotMain(), same timing as got_controls_init()/
// got_hud_init()'s own screen-size setup calls). Harmless to leave uncalled/zero if neither Mode A
// nor the menu pillarbox above is ever active (Modes B/C don't need it during gameplay -- both
// always stretch to fill whatever glViewport() was already set to, exactly like every frame before
// this feature existed).
void modex_set_screen_size(int width, int height);

// Returns the actual on-screen pixel rect (physical screen pixels, origin top-left) the game is
// really being drawn into right now, accounting for Mode A's own pillarbox/letterbox black bars --
// see this function's own modexgl.c comment. Any output pointer may be NULL if that value isn't
// needed. Used by got_hud.c to anchor screen-space overlays (the boss health bar) against the actual
// visible game area instead of the full physical screen.
void modex_get_playfield_rect(int *out_x, int *out_y, int *out_w, int *out_h);

// Screen-shake: translates the WHOLE composited quad (playfield+panel, or playfield alone in Mode C)
// by (dx,dy) playfield PIXELS (the same 320x(192 or 240) coordinate space page_buffers/got_main.c's
// own sprite x/y already use), converted internally to a clip-space offset -- added for Episode 2's
// own boss fight (wootbeer: "when the spikes fall from the screen there should be a 'screen shake'
// effect, this is missing"). NOT a real GoT effect: confirmed by reading every real boss_movement()/
// bossb_movement() directly (2_boss.c and its Episode 1/3 siblings) -- no screen-shake mechanism
// exists anywhere in the original 1993 DOS source, this game predates that now-common technique. A
// deliberate, wootbeer-requested addition rather than a fidelity fix, same "documented departure from the
// real game" class as the one-way-arrow behavior (got_special_tile_actor()'s own case 205-208
// comment, got_main.c). got_main.c's own render loop recomputes this once per frame, straight off the
// Episode 2 boss driver's own real `i4` countdown field (real `actr->i4`, the exact same 50-tick
// window real bossb_movement() already counts down for its own dramatic hit-stun beat around the
// spike-wall volley -- see got_bossb2_movement()'s own comment), and calls this every frame gameplay
// is active, same "set fresh every frame, harmless when zero" convention modex_set_mirror_exclude_
// rect() already established. Only ever affects the game's own quad -- got_hud.c's screen-space
// overlays (the boss health bar, minimal-HUD/D-pad) are drawn after modex_present_frame() returns and
// never shake, matching how a modern game's UI usually stays pinned through a camera shake.
void modex_set_screen_shake_offset(int dx, int dy);

#endif
