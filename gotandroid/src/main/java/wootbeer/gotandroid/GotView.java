package wootbeer.gotandroid;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.graphics.drawable.GradientDrawable;
import android.hardware.input.InputManager;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.media.SoundPool;
import android.os.Build;
import android.os.Handler;
import android.text.InputFilter;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.io.IOException;
import java.util.HashSet;
import java.util.Set;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;
import javax.microedition.khronos.opengles.GL10;

/**
 * The real render shell -- JNI bridge / EGL setup / render-thread lifecycle -- replacing the
 * placeholder from the earlier build-skeleton pass. Modeled directly on Descore's
 * DescoreView.java (Descore-Mobile-master/Descore/src/main/java/.../DescoreView.java)
 * and its native counterpart (Descore/src/main/cpp/main.c + render.c) -- see
 * got-android-port-notes_1.md section 1a for why this pattern was chosen and what's
 * engine-agnostic vs. GoT-specific.
 *
 * What's real here: EGL context creation (this class, plain Java EGL10 -- not native), the
 * background render thread, the native entry point call (gotMain(), analogous to Descore's
 * descoreMain() -> descore_main()), and pause/resume via the same blocking
 * pauseRenderThread()/resumeRenderThread() handshake Descore uses (native calls back into this
 * class's pauseRenderThread() via JNI and blocks there until the main thread calls
 * resumeRenderThread()).
 *
 * What's still a placeholder (see got_main.c): there is no ported GoT game loop yet, so
 * gotMain() runs its own temporary loop that just clears the screen and swaps buffers every
 * frame instead of running GoT's actual game logic -- that loop is exactly where the real
 * ported main() will eventually live, calling the same got_show_render_buffer() once per frame
 * the way Descore's own descore_main() calls showRenderBuffer(). Touch/on-screen controls are
 * not implemented at all yet (no UI design for them exists) -- only the keyboard scancode
 * bridge (keyHandler() -> key_flag[]) is wired, since that's the input path GoT's ported game
 * code will actually read from (see notes section 1a.3). D-pad movement is now wired too (see
 * gotScancode() below) -- Thor's real movement code reads key_flag[72/80/75/77] directly.
 * On-screen touch controls and gamepad auto-detect are wired too now (see the touch/gamepad
 * section below), following Descore's DescoreView.java pattern (InputManager.InputDeviceListener
 * + a native controls.c-style button table) adapted for GoT's much simpler input needs.
 */
public class GotView extends SurfaceView implements KeyEvent.Callback, SurfaceHolder.Callback,
		InputManager.InputDeviceListener {

	private boolean gotRunning, paused, surfaceWasDestroyed;
	private final Context context;
	private final GotView thiz;
	private final Handler mainHandler;
	private final Object renderThreadObj = new Object();
	private Point size;
	private SurfaceHolder holder;
	private InputManager inputManager;
	// Device IDs currently identified as gamepad/joystick-class -- tracked as a set (not just a
	// count) so onInputDeviceRemoved() (which only gets an ID, the device is already gone by
	// then) can tell whether the departing device actually mattered without re-querying it.
	private final Set<Integer> connectedGamepadIds = new HashSet<>();

	// --- Sound effects (SFX) --------------------------------------------------------------------
	// Real sound-effect indices from 1_define.h (1_sound.c's play_sound() dispatch table) -- kept
	// as the real names/values to match got_main.c's own SOUND_* enum exactly, since got_play_sound()
	// there passes one of these straight through via JNI to playSound() below. Real NUM_SOUNDS is
	// 19, but only 16 of those ever actually got loaded from GOTRES.DAT in the shipping DOS game
	// (a latent bug in the original resource-loading code) -- BOSS11/12/13 (indices 16-18) were
	// never reachable there either. Now mapped for the Episode 1 boss round (see SOUND_FILES' own
	// comment below) to 3 of the bundled pack's own 7 boss-specific stinger files that were already
	// extracted/resampled/shipped in the original sound-system round, just left unmapped until a
	// boss actually existed to wire them to.
	private static final int SOUND_OW = 0;
	private static final int SOUND_GULP = 1;
	private static final int SOUND_SWISH = 2;
	private static final int SOUND_YAH = 3;
	private static final int SOUND_ELECTRIC = 4;
	private static final int SOUND_THUNDER = 5;
	private static final int SOUND_DOOR = 6;
	private static final int SOUND_FALL = 7;
	private static final int SOUND_ANGEL = 8;
	private static final int SOUND_WOOP = 9;
	private static final int SOUND_DEAD = 10;
	private static final int SOUND_BRAAPP = 11;
	private static final int SOUND_WIND = 12;
	private static final int SOUND_PUNCH1 = 13;
	private static final int SOUND_CLANG = 14;
	private static final int SOUND_EXPLODE = 15;
	private static final int SOUND_BOSS11 = 16;
	private static final int SOUND_BOSS12 = 17;
	private static final int SOUND_BOSS13 = 18;
	// Port-only addition, no real DOS index -- matches got_main.c's own SOUND_LOKI_ROAR enum entry
	// (see that comment for the root cause: SOUND_BOSS11 is a real generic slot every episode's own
	// boss-start code reuses, but this port's SOUND_FILES table below can only back it with one file
	// at a time, and it's always been the snake's -- wootbeer: "when entering loki's boss room... it
	// shouldn't be the snake sound from episode 1, it's more like a roar"). A dedicated slot for
	// Loki's own entrance sidesteps that without disturbing SOUND_BOSS11's existing episode 1/2 uses.
	private static final int SOUND_LOKI_ROAR = 19;
	private static final int NUM_SOUNDS = 20;

	// assets/sounds/*.ogg filenames, one per index above -- all public-domain audio from Ron
	// Davis's GOTstuff.zip (see got-android-port-notes_1.md section 7's resolved licensing plan).
	// Confidence noted per entry: "confirmed" means the real source's own call site (traced in
	// 1_move.c/1_movpat.c/1_object.c/etc.) makes the mapping unambiguous; "best-guess" is a
	// plausible match by name/context that hasn't been confirmed by ear; unmapped (null) entries
	// have no call site wired yet (the game system that would trigger them -- pickups, doors,
	// dialog -- isn't built) and are left for whoever wires that system to pick and confirm.
	private static final String[] SOUND_FILES = new String[NUM_SOUNDS];
	static {
		SOUND_FILES[SOUND_OW] = "thorHit.ogg";        // confirmed: 1_object.c "bad apple" damage
		SOUND_FILES[SOUND_GULP] = "eatApple.ogg";      // confirmed: 1_object.c "good apple" heal
		SOUND_FILES[SOUND_SWISH] = "hammerThrow.ogg";  // confirmed: 1_move.c thor_shoots()
		// SOUND_YAH: generic "picked up an item" cheer (1_object.c pick_up_object(), fires on every
		// non-apple pickup -- jewels, potions, keys, treasure, trophy, crown). First guess here was
		// jump.ogg (no name in the bundle screamed "pickup"); wootbeer confirmed against the real game
		// directly -- picking up jewels plays thorEww.ogg -- which was sitting completely unmapped
		// this whole time despite matching by name (Thor's own voice clip) far better than a guess
		// ever could. Corrected.
		SOUND_FILES[SOUND_YAH] = "thorEww.ogg";
		SOUND_FILES[SOUND_ELECTRIC] = "lightning.ogg"; // best-guess: electric hazard zap
		SOUND_FILES[SOUND_THUNDER] = "thunder.ogg";    // confirmed by name
		SOUND_FILES[SOUND_DOOR] = "slam.ogg";          // best-guess: door open/close thud
		SOUND_FILES[SOUND_FALL] = "fall.ogg";          // confirmed by name
		SOUND_FILES[SOUND_ANGEL] = "angel.ogg";        // confirmed by name
		SOUND_FILES[SOUND_WOOP] = "talkBeep.ogg";      // best-guess: frequent dialog/panel blip
		SOUND_FILES[SOUND_DEAD] = "thorDie.ogg";       // confirmed by name
		SOUND_FILES[SOUND_BRAAPP] = "buzzer.ogg";      // best-guess: comedic negative buzzer
		SOUND_FILES[SOUND_WIND] = "wind.ogg";          // confirmed by name
		SOUND_FILES[SOUND_PUNCH1] = "punch.ogg";       // confirmed by name
		SOUND_FILES[SOUND_CLANG] = "hammerHit.ogg";    // confirmed: 1_movpat.c hammer wall bounce
		SOUND_FILES[SOUND_EXPLODE] = "explosion.ogg";  // confirmed by name
		// Episode 1 boss round: real BOSS11 (windup roar, `play_sound(BOSS11,...)` at a strike's own
		// wind-up and at the very start of the fight), BOSS12 (fire, shot_pattern_five's own firing
		// cue), and BOSS13 (hit, check_boss1_hit's own damage cue) all real, generic slots any boss's
		// own code reuses -- mapped here to the snake's own 3 of the pack's 7 boss stingers
		// (bossSkullYell/Laugh and bossLokiGong/Swirl remain unmapped, reserved for Episode 2/3's own
		// later bosses; bossHit is shared rather than snake-specific since the real DOS game's own
		// per-episode resource bank would have had a distinct BOSS13 sample per boss, a distinction
		// this port's single shared bundled-file table can't express until/unless a future round
		// swaps these 3 slots' own files when a different boss becomes active).
		SOUND_FILES[SOUND_BOSS11] = "bossSnakeHiss.ogg";
		SOUND_FILES[SOUND_BOSS12] = "bossSnakeFire.ogg";
		SOUND_FILES[SOUND_BOSS13] = "bossHit.ogg";
		// SOUND_LOKI_ROAR: Loki's own boss-entrance cue (got_boss3_start(), got_main.c), a dedicated
		// slot rather than reusing SOUND_BOSS11 -- see that constant's own comment above. Of the two
		// previously-unmapped Loki stingers, bossLokiGong.ogg (a low, slow-building resonant hit --
		// spectral analysis: ~730Hz centroid, peaks right at its own tail, well above bossSnakeHiss's
		// own ~2500Hz/front-loaded hiss on both counts) reads far closer to wootbeer's "more like a roar"
		// than bossLokiSwirl.ogg (brighter/~1450Hz, front-loaded, noisier -- a whoosh/sweep, not a
		// roar); best-guess pending wootbeer's own confirmation by ear, same as every other best-guess
		// entry in this table, and easy to swap for bossLokiSwirl.ogg if he hears it differently.
		// bossLokiSwirl.ogg remains unmapped, still reserved for a future Loki-specific cue.
		SOUND_FILES[SOUND_LOKI_ROAR] = "bossLokiGong.ogg";
	}

	private SoundPool soundPool;
	// SoundPool's own per-sound IDs, indexed by the real SOUND_* index above (parallel to
	// SOUND_FILES); -1 means "not loaded" (either SOUND_FILES[i] is null, or the asset failed to
	// load), same sentinel style as this file's connectedGamepadIds-less int fields elsewhere.
	private final int[] soundIds = new int[NUM_SOUNDS];
	// SoundPool's own per-stream ID for whichever instance of each SOUND_* index is still playing
	// (0 = none active) -- see playSound() below for why this exists.
	private final int[] activeStreamIds = new int[NUM_SOUNDS];
	// New Sound/Music submenu gain slider (got_menu.c) -- wootbeer: "each one will be an option with a
	// slider similar to descore to set the audio/gain level of the sound... the current sound and
	// music level will be the max" -- so this starts at 1.0f (today's fixed, pre-slider volume) and
	// only ever moves down from there, via setSoundVolume() below (called via JNI from got_main.c's
	// own got_sound_set_volume()). Applied per-call at playSound()'s own soundPool.play(), same as a
	// real SoundPool has no single persistent "channel volume" to set once and forget.
	private float sfxVolume = 1.0f;

	// --- Music -----------------------------------------------------------------------------------
	// Real music-track indices from 1_music.c's load_music() (`names[]`) -- kept matching got_main.c's
	// own MUSIC_* enum exactly, same reasoning as SOUND_* above. Real GOTRES.DAT's own SONG*/
	// WINSONG/BOSSSONG/OPENSONG resources are raw AdLib/OPL2 sequencer data this port never touches
	// (wootbeer: "the original game has a toggle for midi/digital audio. but I don't want to worry about
	// including midi in our port" -- see got_menu.h's own note on why there's deliberately no
	// separate Digital/MIDI toggle here either, unlike the real options menu). Pre-rendered
	// public-domain .mp3 tracks instead (Ron Davis's GOTstuff.zip again, its own "songs" folder this
	// time), played through a MediaPlayer rather than SoundPool -- these are multi-minute streaming
	// tracks that need real looping and pause/resume, not short one-shot clips.
	private static final int MUSIC_SONG1 = 0;
	private static final int MUSIC_SONG2 = 1;
	private static final int MUSIC_SONG3 = 2;
	private static final int MUSIC_SONG4 = 3;
	private static final int MUSIC_WINSONG = 4;
	private static final int MUSIC_BOSSSONG = 5;
	private static final int MUSIC_OPENSONG = 6;
	// Episode 2's own override for the MUSIC_SONG2 slot -- real per-episode music is genuinely
	// different tracks even for "the same" room `type`, which this port's original single flat
	// MUSIC_SONG1-4 table (built before multi-episode support existed) had nowhere to represent.
	// wootbeer, in-game: "puzzle2 should be the song used for the music on the first board of level 2"
	// -- got_main.c's own got_room_music_index() sends this index instead of MUSIC_SONG2 whenever
	// episode 2 is active and a room's own `type` is 1, same "swap the whole slot" shape as the
	// existing MUSIC_SONG4/creepy1.mp3 cave fix below, not a single-room special case.
	private static final int MUSIC_SONG2_EP2 = 7;
	// wootbeer: "next is a bug with the correct music, especially in episode 3. I don't think the
	// opening track for episode 3 is correct. also the track that it changes to when entering or
	// inside lokisburg is not correct." Root cause (see got_main.c's own MUSIC_SONG35/MUSIC_SONG36
	// comment for the full real-source citation): episodes 2 and 3 don't share episode 1's 7-slot
	// `names[]` shape at all -- their own real load_music() tables have SIX generic room-song slots
	// before WINSONG/BOSSSONG/OPENSONG (indices 0-5, real names SONG21-25+SONG35 for g2 / SONG31-36
	// for g3), not four, pushing WINSONG/BOSSSONG/OPENSONG to indices 6/7/8 instead of 4/5/6. Since
	// a room's raw `type` byte is sent straight through as this index with zero translation, every
	// `type`==4 room in episodes 2/3 (41 of episode 2's 120 rooms; 36 of episode 3's) and every
	// `type`==5 room in episode 3 (2 rooms) was silently playing MUSIC_WINSONG/MUSIC_BOSSSONG
	// (win.mp3/boss.mp3) instead of an ordinary ambient track -- confirmed one such episode-3
	// `type`==4 room is the gate room holding a SPEAR trap guarding Lokisburg (notes section 119),
	// directly matching wootbeer's "entering... lokisburg" report; the "opening track" half is the same
	// bug, just hit almost immediately since `type`==4 covers 30% of episode 3's own map. Episode 1
	// is untouched -- its own real `type`==4 rooms (2 of them) really do mean WINSONG, and it never
	// authors `type`==5 at all. MUSIC_SONG35 is shared by both episodes 2 and 3 (real g2/g3 use the
	// literal same "SONG35" resource name for their own `type`==4 slot); MUSIC_SONG36 is episode 3
	// only (`type`==5). got_main.c's own got_room_music_index() sends these instead of
	// MUSIC_WINSONG/MUSIC_BOSSSONG whenever episode 2 or 3 is active, same "swap the whole slot"
	// shape as MUSIC_SONG2_EP2 above.
	private static final int MUSIC_SONG35 = 8;
	private static final int MUSIC_SONG36 = 9;
	// wootbeer: "use action3 as the background music" for the new-game story-text screens (got_story.c,
	// got_main.c) -- one single track, wootbeer's own explicit pick, reused unchanged for all 3
	// episodes' own story screens (not a real per-episode resource match like SONG1-4/SONG35/
	// SONG36 above). The chapter-title screen that follows plays no music at all
	// (got_pause_music()), matching wootbeer's own explicit "this screen plays no music."
	private static final int MUSIC_STORY = 10;
	private static final int NUM_MUSIC_TRACKS = 11;

	// assets/songs/*.mp3 filenames, one per index above. WINSONG/BOSSSONG/OPENSONG are exact-name
	// matches already sitting in Ron Davis's own "songs" folder; SONG1-4 (the four generic per-room
	// ambient slots -- real level data picks one of these per room via each LEVEL's own `type` byte,
	// level.h offset 241, got_main.c's own s_room_type) have no equivalent real name to match by, so
	// these four are an arbitrary but reasonable pick out of the folder's other 13 mood tracks --
	// trivial to swap for a different one later, this table is the only place that choice lives.
	private static final String[] MUSIC_FILES = new String[NUM_MUSIC_TRACKS];
	static {
		MUSIC_FILES[MUSIC_SONG1] = "adventure1.mp3";  // arbitrary pick -- most rooms use this slot
		MUSIC_FILES[MUSIC_SONG2] = "action1.mp3";     // arbitrary pick
		MUSIC_FILES[MUSIC_SONG3] = "puzzle1.mp3";     // arbitrary pick
		// wootbeer, in-game: "the player going into a cave should play Creepy1 and not Puzzle1" -- the
		// cave room he was in resolves to this exact slot (SONG4 is the only slot that was pointing
		// at puzzle1.mp3), so swapped straight across with SONG3 above rather than guessing at some
		// other slot -- both tracks stay in use, just traded.
		MUSIC_FILES[MUSIC_SONG4] = "creepy1.mp3";
		MUSIC_FILES[MUSIC_WINSONG] = "win.mp3";       // confirmed by name
		MUSIC_FILES[MUSIC_BOSSSONG] = "boss.mp3";     // confirmed by name
		MUSIC_FILES[MUSIC_OPENSONG] = "opening.mp3";  // confirmed by name
		MUSIC_FILES[MUSIC_SONG2_EP2] = "puzzle2.mp3"; // see MUSIC_SONG2_EP2's own comment above
		// See MUSIC_SONG35/MUSIC_SONG36's own comment above -- another arbitrary-but-reasonable pick
		// out of GOTstuff's remaining mood tracks, same as SONG1-4, since (like those) SONG35/SONG36
		// have no equivalent real name to match by.
		MUSIC_FILES[MUSIC_SONG35] = "adventure2.mp3"; // episodes 2 AND 3's own shared type==4 slot
		MUSIC_FILES[MUSIC_SONG36] = "puzzle3.mp3";    // episode 3's own type==5 slot
		MUSIC_FILES[MUSIC_STORY] = "action3.mp3";     // see MUSIC_STORY's own comment above -- wootbeer's
													   // own explicit pick, not an arbitrary one
	}

	private MediaPlayer musicPlayer;
	// Same new Sound/Music submenu gain slider as sfxVolume above, music's own channel -- unlike
	// sfxVolume this DOES have a single persistent place to apply it (MediaPlayer.setVolume()), so
	// setMusicVolume() below both stores this and pushes it to musicPlayer live, and playMusic() below
	// applies it to every freshly created player too (a new track shouldn't reset to full volume out
	// from under a slider the player already pulled down).
	private float musicVolume = 1.0f;
	// Set right before pausing for an app-background event (onAppPause() below), so onAppResume()
	// knows whether bringing music back is actually correct -- if the user had already toggled
	// Music off in-game before backgrounding (musicPlayer already paused, not playing), this reads
	// false and onAppResume() correctly leaves it off instead of un-pausing something the player
	// deliberately silenced.
	private boolean musicWasPlayingBeforeAppPause;
	// True once musicPlayer has actually finished async-preparing (set in playMusic()'s own
	// OnPreparedListener, cleared whenever a new player is created) -- MediaPlayer.start()/pause()
	// both throw IllegalStateException if called before the player reaches the Prepared state, a
	// real risk now that playMusic() below uses prepareAsync() instead of a blocking prepare() (see
	// that method's own comment on why): resumeMusic()/pauseMusic() need to know whether it's safe
	// to touch the player directly yet, not just whether it happens to be currently playing.
	private boolean musicPrepared;
	// True while music should stay silent even once musicPlayer finishes preparing -- set by
	// pauseMusic() (both the in-game Music-off toggle and an app-background event route through
	// it), cleared by playMusic() (a fresh track always wants to play once ready) and resumeMusic().
	// Without this, a track requested right before backgrounding (e.g. warping into a cave, then
	// immediately switching away) could finish its async decode while the app is backgrounded and
	// start playing music the player never asked to hear yet.
	private boolean musicAutoStartSuppressed;

	public GotView(Activity activity) {
		super(activity);
		this.context = activity;
		this.thiz = this;
		this.mainHandler = new Handler(activity.getMainLooper());
		this.holder = getHolder();
		// See DescoreView's constructor comment for why this matters: without it, the EGL
		// config's alpha channel (requested in initEgl() below) can let the platform treat this
		// SurfaceView as translucent-capable, which has caused visible seam/border artifacts in
		// the Descore port. Pin it opaque up front to avoid that whole class of bug.
		holder.setFormat(PixelFormat.OPAQUE);
		this.setFocusableInTouchMode(true);
		if (Build.VERSION.SDK_INT >= 26) {
			setDefaultFocusHighlightEnabled(false);
		}
		holder.addCallback(this);

		// Gamepad auto-detect -- same InputManager.InputDeviceListener pattern DescoreView.java
		// uses, hiding/disabling the on-screen touch D-pad whenever a real controller (like the
		// Retroid Pocket 6's own built-in one) is connected, and bringing it back the instant one
		// isn't. registerInputDeviceListener()'s callbacks land on mainHandler's looper, not the
		// render thread, matching every other Android input callback in this class.
		this.inputManager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
		if (inputManager != null) {
			inputManager.registerInputDeviceListener(this, mainHandler);
		}
		for (int deviceId : InputDevice.getDeviceIds()) {
			if (isGamepad(InputDevice.getDevice(deviceId))) {
				connectedGamepadIds.add(deviceId);
			}
		}
		setGamepadConnected(!connectedGamepadIds.isEmpty());

		java.util.Arrays.fill(soundIds, -1);
		AudioAttributes audioAttributes = new AudioAttributes.Builder()
				.setUsage(AudioAttributes.USAGE_GAME)
				.setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
				.build();
		soundPool = new SoundPool.Builder()
				.setMaxStreams(6) // headroom for a few overlapping SFX -- GoT has no real mixing
				// limit of its own to match (PC speaker / one SB DMA channel could only play one
				// digitized sound at a time), but SoundPool has no reason to be that restrictive.
				.setAudioAttributes(audioAttributes)
				.build();
		loadSounds();
	}

	// Kicks off an async load for every mapped SOUND_FILES entry. SoundPool.load() returns
	// immediately and finishes on a background thread -- playSound() below just no-ops if a
	// sound's load hasn't finished yet (soundIds[i] still -1), which in practice never matters:
	// these are all a few KB and finish loading well before the game reaches any of their real
	// trigger points. Not wired to OnLoadCompleteListener for that reason -- worth revisiting only
	// if a specific sound is ever observed missing its very first play.
	private void loadSounds() {
		AssetManager assets = context.getAssets();
		for (int i = 0; i < NUM_SOUNDS; ++i) {
			if (SOUND_FILES[i] == null) {
				continue;
			}
			try {
				AssetFileDescriptor afd = assets.openFd("sounds/" + SOUND_FILES[i]);
				soundIds[i] = soundPool.load(afd, 1);
				afd.close();
			} catch (IOException e) {
				// Missing/corrupt bundled asset -- non-fatal, that one sound just won't play.
			}
		}
	}

	/** Called via JNI from got_play_sound() (got_main.c) with one of the real SOUND_* indices
	 *  above. Runs on the render thread, same as every other native->Java call in this class. */
	@SuppressWarnings("unused")
	private void playSound(int soundIndex) {
		if (soundPool == null || soundIndex < 0 || soundIndex >= NUM_SOUNDS) {
			return;
		}
		int soundId = soundIds[soundIndex];
		if (soundId > 0) {
			// Stop whatever instance of THIS SAME sound is still playing before starting a new
			// one. Needed for GLOBE's dialogue box (got_dialogue.c), which fires SOUND_WOOP once
			// per revealed character, much faster than talkBeep.ogg's own playback length -- with
			// SoundPool's default overlapping-streams behavior, that meant several instances of
			// the same clip sounding at once, staggered by a few milliseconds each, which phases/
			// comb-filters together into one sustained, differently-pitched tone instead of
			// distinct blips (wootbeer: "sounds the same the entire length of his speech... until he
			// finishes the sentence, then the sound plays normally" -- exactly this effect: no
			// more overlap once retriggering stops, so the last instance finally plays cleanly).
			// Real DOS never had this problem because real PC speaker/Sound Blaster hardware could
			// only play one digitized sound at a time to begin with (see soundPool's own
			// setMaxStreams(6) comment above) -- retriggering the same sound there just cleanly
			// replaced it, no overlap possible. Stopping the previous stream per sound index
			// reproduces that same real constraint without limiting SoundPool's own ability to mix
			// genuinely different sounds (e.g. WOOP and CLANG) at once.
			if (activeStreamIds[soundIndex] != 0) {
				soundPool.stop(activeStreamIds[soundIndex]);
			}
			activeStreamIds[soundIndex] = soundPool.play(soundId, sfxVolume, sfxVolume, 1, 0, 1.0f);
		}
	}

	/** Called via JNI from got_play_music() (got_main.c) -- got_main.c has already decided this is
	 *  actually a new track (or a forced restart), so this always (re)starts playback from the top:
	 *  releases whatever was playing before, loads the new asset, loops it (real GoT music loops
	 *  continuously, matching MU_StartMusic()'s own real behavior), and starts it once decoding is
	 *  ready.
	 *
	 *  prepareAsync(), NOT the old blocking prepare() -- wootbeer, in-game: "the music takes a second to
	 *  play the new track after entering a cave, this may be due to loading times, but maybe can
	 *  mask this load during the screen transition?" It was exactly that: this method runs via JNI
	 *  on got_main.c's own render thread (the same thread got_show_render_buffer() calls
	 *  modex_present_frame()/eglSwapBuffers() from every frame, see that function), and the old
	 *  blocking prepare() sits there decoding the mp3's header and buffering audio -- however long
	 *  that takes -- before returning control to native code, stalling the ENTIRE render loop (no
	 *  new frames at all, not just delayed audio) for the same duration. prepareAsync() returns
	 *  immediately and start()s from setOnPreparedListener() instead, once decoding actually
	 *  finishes, in the background -- the render loop (and, once the cave-transition fix above
	 *  ships, got_step_phase_transition()'s own screen-dissolve reveal) keeps running the whole
	 *  time, so whatever load delay is left is now naturally masked by however long that reveal
	 *  takes to run rather than by a frozen screen. The `musicPlayer == mp` check in the listener
	 *  guards against a stale callback: if another playMusic() call (or pauseMusic()/a second warp)
	 *  supersedes this one before decoding finishes, `musicPlayer` no longer points at `mp` by the
	 *  time the listener fires, so the now-unwanted track is left alone instead of starting late. */
	@SuppressWarnings("unused")
	private void playMusic(int musicIndex) {
		if (musicIndex < 0 || musicIndex >= NUM_MUSIC_TRACKS || MUSIC_FILES[musicIndex] == null) {
			return;
		}
		if (musicPlayer != null) {
			musicPlayer.release();
			musicPlayer = null;
		}
		musicPrepared = false;
		musicAutoStartSuppressed = false; // a freshly requested track always wants to play once
										   // ready -- see this field's own comment
		try {
			AssetFileDescriptor afd = context.getAssets().openFd("songs/" + MUSIC_FILES[musicIndex]);
			final MediaPlayer player = new MediaPlayer();
			player.setAudioAttributes(new AudioAttributes.Builder()
					.setUsage(AudioAttributes.USAGE_GAME)
					.setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
					.build());
			player.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getLength());
			afd.close();
			player.setLooping(true);
			player.setVolume(musicVolume, musicVolume); // see musicVolume's own comment above -- a
														  // fresh track keeps whatever level the slider
														  // was already set to, not full volume

			player.setOnPreparedListener(new MediaPlayer.OnPreparedListener() {
				@Override
				public void onPrepared(MediaPlayer mp) {
					if (musicPlayer != mp) {
						return; // superseded by a later playMusic() call -- see this method's own
								// comment above
					}
					musicPrepared = true;
					if (!musicAutoStartSuppressed) {
						mp.start();
					}
					// else: paused or backgrounded while this was still decoding -- leave it
					// sitting in the Prepared state; resumeMusic() (app foregrounded again) or the
					// in-game Music toggle's own forced-restart already handle starting it from
					// here, and both are now safe to call since musicPrepared is true.
				}
			});
			player.prepareAsync();
			musicPlayer = player;
		} catch (IOException e) {
			// Missing/corrupt bundled asset -- non-fatal, same as loadSounds() above: music just
			// won't play until the next track switch.
			musicPlayer = null;
		}
	}

	/** Called via JNI from got_pause_music() (got_main.c) when the Music menu toggle is switched
	 *  off, and from onAppPause() below when the app itself backgrounds -- pauses in place (real
	 *  music_pause()/MU_MusicOff()), doesn't release, so resumeMusic() below can pick back up
	 *  exactly where this left off. Also raises musicAutoStartSuppressed unconditionally (not just
	 *  when already playing) -- since playMusic() switched to prepareAsync(), a track can still be
	 *  mid-decode when this is called (isPlaying() reads false either way, so the pause() call
	 *  below correctly no-ops for it -- calling pause() before a player reaches the Prepared state
	 *  would throw), and without this flag that track would ignore the pause entirely and start
	 *  playing the moment decoding finished, regardless of why pauseMusic() was called. */
	@SuppressWarnings("unused")
	void pauseMusic() {
		musicAutoStartSuppressed = true;
		if (musicPlayer != null && musicPlayer.isPlaying()) {
			musicPlayer.pause();
		}
	}

	/** Resumes exactly where pauseMusic() left off. Only ever called from onAppResume() below (the
	 *  app-background case) -- the in-game Music menu toggle's own "on" path goes through
	 *  got_play_music()'s forced restart (got_main.c's got_music_set_enabled_from_menu(), matching
	 *  real music_play(level_type,1)) instead of this, matching the real options-menu code's own
	 *  asymmetric off=pause/on=restart behavior. Guarded by musicPrepared, not just musicPlayer!=
	 *  null -- calling start() before the player has actually finished its (now async) prepare
	 *  would throw IllegalStateException. If it's not prepared yet, clearing
	 *  musicAutoStartSuppressed here is enough: playMusic()'s own OnPreparedListener will start it
	 *  the moment decoding finishes, since the suppression flag is now down. */
	private void resumeMusic() {
		musicAutoStartSuppressed = false;
		if (musicPlayer != null && musicPrepared) {
			musicPlayer.start();
		}
	}

	/** Called via JNI from got_sound_set_volume() (got_main.c), itself driven by the new Sound/Music
	 *  submenu's Sound gain slider (got_menu.c). volume is already 0.0-1.0 (got_menu.c's own 0-
	 *  SOUND_MUSIC_GAIN_STEPS int, divided down before this call). Just stores it -- SoundPool has no
	 *  single persistent per-sound-index volume to push it to right now, so it's applied per-play
	 *  instead, at playSound()'s own soundPool.play() call above. */
	@SuppressWarnings("unused")
	private void setSoundVolume(float volume) {
		sfxVolume = volume;
	}

	/** Called via JNI from got_music_set_volume() (got_main.c), same new submenu's Music gain slider.
	 *  Stores it (so playMusic() picks it up on the next track too) AND pushes it to musicPlayer live,
	 *  unlike setSoundVolume() above -- MediaPlayer's setVolume() applies immediately to whatever's
	 *  already playing, so the currently-playing track's volume actually moves as the slider moves
	 *  rather than only taking effect the next time a track starts. */
	@SuppressWarnings("unused")
	private void setMusicVolume(float volume) {
		musicVolume = volume;
		if (musicPlayer != null) {
			musicPlayer.setVolume(musicVolume, musicVolume);
		}
	}

	/** Called via JNI from got_dp_to_px()/got_px_to_dp() (got_main.c), themselves used by
	 *  got_controls.c's new density-aware touch control layout -- ported from the same fix wootbeer's
	 *  Descore-Android project already shipped (DescoreActivity.dpToPx()/pxToDp()), minus that
	 *  project's tablet-only buttonSizeBias multiplier: GoT's touch layout only needed the density
	 *  normalization itself (raw-pixel sizing was the actual bug -- a "0.11 * min(w,h)" cell size
	 *  came out huge on a high-density medium phone since it never accounted for dp vs. px at all),
	 *  not a tablet bump on top of it. Plain density-dpi normalization, same formula Android's own
	 *  TypedValue.applyDimension(COMPLEX_UNIT_DIP, ...) uses under the hood. */
	@SuppressWarnings("unused")
	private float dpToPx(float dp) {
		DisplayMetrics metrics = context.getResources().getDisplayMetrics();
		return dp * ((float) metrics.densityDpi / DisplayMetrics.DENSITY_DEFAULT);
	}

	/** See dpToPx() above -- same formula, inverted. */
	@SuppressWarnings("unused")
	private float pxToDp(float px) {
		DisplayMetrics metrics = context.getResources().getDisplayMetrics();
		return px / ((float) metrics.densityDpi / DisplayMetrics.DENSITY_DEFAULT);
	}

	/** Called from GotActivity.onPause(). Records whether music was actually audible -- OR still
	 *  loading and would have become audible -- right before backgrounding (as opposed to already
	 *  paused by the in-game Music toggle) so onAppResume() below knows whether bringing it back is
	 *  actually correct. The `!musicPrepared` half of this matters now that playMusic() uses
	 *  prepareAsync(): backgrounding the app in the narrow window while a just-requested track (e.g.
	 *  right after warping into a cave) is still decoding must still resume it on return, even
	 *  though isPlaying() alone would read false for a track that never got the chance to start. */
	void onAppPause() {
		musicWasPlayingBeforeAppPause =
				musicPlayer != null && (musicPlayer.isPlaying() || !musicPrepared);
		pauseMusic();
	}

	/** Called from GotActivity.onResume(). */
	void onAppResume() {
		if (musicWasPlayingBeforeAppPause) {
			resumeMusic();
		}
	}

	// --- Player name entry (got_title.c) ----------------------------------------------------------
	// got_title.c has no real source to port (see got_title.h's own scope note) -- this is this
	// port's own original design for the one piece of that screen Android already does well
	// natively, rather than trying to draw a text-entry field with got_font.c's own tile font.
	//
	// wootbeer: "can we just make it go straight into the android keyboard after selecting episode
	// instead of the 'New Player' 'Enter a name:' popup, just because that popup looks really
	// plain." This used to be a plain AlertDialog.Builder (title "New Player", message "Enter a
	// name:", an EditText, OK/Cancel buttons) -- functional, but unmistakably just a stock Android
	// dialog box floating over the game. Replaced with a borderless EditText layered directly on
	// top of the game view via Activity.addContentView() (the standard way to add another view on
	// top of an existing setContentView() root without needing a shared parent ViewGroup of its
	// own -- GotActivity.launchGame() calls setContentView(gotView) directly, so there's no
	// existing layout to hook into otherwise). No title bar, no dialog box chrome, no OK/Cancel
	// buttons to tap first: the soft keyboard comes up the instant the field gains focus, and its
	// own "Done" action key submits directly -- matching the "just start typing" feel wootbeer asked
	// for. Tapping anywhere outside the field cancels, the same as the old AlertDialog's
	// setCancelable(true) outside-tap-dismiss behavior; NameEntryEditText's onKeyPreIme() override
	// below makes a single Back press do the same, closing the keyboard AND cancelling together
	// (an actual improvement over the old dialog, which -- like any view with an open IME -- needed
	// one Back press to close the keyboard and a second to reach the dialog's own cancel).
	//
	// wootbeer: "after it started the game it made thor's hammer throw over and over again, assuming
	// 'a' is being 'stuck'." Confirmed: unlike the old AlertDialog (its own separate Dialog window,
	// wholly outside GotView's own key handling), this overlay's EditText lives in the SAME window
	// as GotView (Activity.addContentView() adds a sibling view, not a new window) -- so whichever
	// physical button was still held the instant the field called requestFocus() (e.g. the
	// Fire/Confirm button just used to pick an empty slot, landing here in the first place) has its
	// eventual key-UP delivered to the EditText instead of back to GotView.onKeyUp(), since Android
	// routes a key event to whatever view currently owns focus. key_flag[] is only ever cleared by
	// that onKeyUp()->keyHandler() call (see keyHandler() below), so key_flag[KEY_FIRE] never got
	// cleared, and got_move_thor()'s own hammer-throw check is deliberately NOT edge-triggered
	// (matches real 1_main.c) -- so it fired every single frame once gameplay started. Same
	// "clear key_flag[] across a focus-stealing transition" precaution the native side already
	// applies elsewhere (got_menu_close()/got_item.c/got_dialogue.c all force key_flag[KEY_FIRE]=0
	// on close, so the same press that dismissed them can't bleed into whatever comes next) --
	// applied here via clearInputKeyFlags() below on BOTH edges of the overlay's lifetime, since
	// either one can strand a held key the same way: once right before the overlay takes focus
	// (clears whatever's already held, so its eventual release can't get lost), and once more when
	// the overlay is dismissed and focus returns to GotView (clears whatever was pressed to
	// submit/cancel, so it can't bleed into the very next gameplay frame either).

	// Every scancode gotScancode()/gotExtraScancode() can ever produce -- D-pad, Fire, Magic,
	// Select Item, Delete Save, the pause-menu Esc, and the Confirm/Cancel pair. See
	// clearInputKeyFlags()'s own comment for why this list needs to exist.
	private static final char[] ALL_GOT_SCANCODES = {
			72, 80, 75, 77, // KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT
			56,             // KEY_FIRE
			29,             // KEY_MAGIC
			57,             // KEY_SELECT
			83,             // KEY_DELETE_SAVE
			28, 14,         // KEY_CONFIRM, KEY_CANCEL
			1,              // KEY_ESC
	};

	// See the long comment above promptPlayerName() for why this exists -- called once right before
	// the name-entry overlay takes focus, and again once it's dismissed, so a physical button held
	// across either transition can never get stuck "down" in key_flag[] for good.
	private void clearInputKeyFlags() {
		for (char scancode : ALL_GOT_SCANCODES) {
			keyHandler(scancode, false);
		}
	}

	// Small EditText subclass purely so Back can be caught before the IME swallows it --
	// onKeyPreIme() is the documented Android hook for exactly this ("catch a key before the input
	// method has a chance to handle it"), and it isn't reachable through a plain OnKeyListener on a
	// stock EditText. See promptPlayerName() below, its only user.
	private static class NameEntryEditText extends EditText {
		interface BackListener {
			void onBack();
		}

		private BackListener backListener;

		NameEntryEditText(Context context) {
			super(context);
		}

		void setBackListener(BackListener listener) {
			this.backListener = listener;
		}

		@Override
		public boolean onKeyPreIme(int keyCode, KeyEvent event) {
			if (keyCode == KeyEvent.KEYCODE_BACK && event.getAction() == KeyEvent.ACTION_UP
					&& backListener != null) {
				backListener.onBack();
				return true; // consumed -- also dismisses the IME, same as returning true normally would
			}
			return super.onKeyPreIme(keyCode, event);
		}
	}

	/** Called via JNI from got_request_name_prompt() (got_main.c) on the render thread, either
	 *  when got_title.c's player-select screen lands on an empty slot (slot 0-4), or -- new as of
	 *  the real high-score system -- when a just-finished episode's score qualifies for the high
	 *  score table (slot == -1, a reserved sentinel; see that function's own updated comment). This
	 *  same overlay serves both: `slot` is never used for anything here except being echoed back
	 *  verbatim to nameEntered() below, so there was nothing Java-side that needed a real branch --
	 *  just a hint string that reads correctly for whichever reason the player is being asked for a
	 *  name. Unlike pauseRenderThread() above, this does NOT block the calling thread -- neither
	 *  caller has anything further of its own to update or draw while this overlay has focus (see
	 *  got_title_draw_player_select()'s own comment for the slot case: the last player-select frame
	 *  just stays up underneath it; the high-score case freezes gameplay the same way the pause menu
	 *  already does, see got_show_render_buffer()'s own s_highscore_prompt_outstanding comment), so
	 *  there's nothing for the render thread to wait on, and blocking it here the way
	 *  pauseRenderThread() does would just freeze that frame instead of leaving it live. Views have
	 *  to be built/added on the main/UI thread (the same reason initEgl() above hops there for its
	 *  setFixedSize() call), so this only posts a Runnable via mainHandler and returns immediately --
	 *  the eventual result reaches native asynchronously from the main thread instead, through
	 *  nameEntered()/nameEntryCancelled() below, calling back into got_main.c's own JNI exports,
	 *  which route to got_title.c or to got_main.c's own high-score handoff depending on which of
	 *  the two this same overlay was actually opened for (see those exports' own comments) -- from a
	 *  different thread than the one that requested the prompt. That's the same cross-thread
	 *  native-global handoff shape key_flag[]/Want_pause already use elsewhere in this port (no
	 *  mutex; see got_main.c's own comment on that precedent) -- either destination is a single
	 *  word-sized write. */
	@SuppressWarnings("unused")
	private void promptPlayerName(final int slot) {
		mainHandler.post(new Runnable() {
			@Override
			public void run() {
				final Activity activity = (Activity) context;
				final float density = getResources().getDisplayMetrics().density;

				final NameEntryEditText input = new NameEntryEditText(context);
				input.setInputType(InputType.TYPE_CLASS_TEXT);
				input.setSingleLine(true);
				input.setImeOptions(EditorInfo.IME_ACTION_DONE);
				// got_main.c's own GotSaveHeader.name is a fixed 24-byte buffer -- cap entry well
				// under that so a multi-byte UTF-8 name can't overflow it once got_start_new_game()
				// copies/truncates it via snprintf(). The real limit is enforced native-side too;
				// this is just to avoid handing native a string that's needlessly long to begin with.
				input.setFilters(new InputFilter[]{new InputFilter.LengthFilter(20)});
				// slot == -1 is the high-score sentinel (see this method's own updated comment) --
				// worth a distinct hint since unlike the new-game-slot case, the player didn't just
				// choose to be here, so a plain "Enter a name" wouldn't explain why this popped up.
				input.setHint(slot == -1 ? "New high score! Enter your name" : "Enter a name");
				input.setHintTextColor(Color.argb(160, 255, 255, 255));
				input.setTextColor(Color.WHITE);
				input.setTextSize(22);
				input.setGravity(Gravity.CENTER);
				GradientDrawable background = new GradientDrawable();
				background.setColor(Color.argb(200, 20, 20, 20));
				background.setCornerRadius(8 * density);
				background.setStroke((int) (1 * density), Color.argb(160, 255, 255, 255));
				input.setBackground(background);
				int hPad = (int) (24 * density);
				int vPad = (int) (12 * density);
				input.setPadding(hPad, vPad, hPad, vPad);

				// Full-screen transparent overlay -- exists purely to (a) host the input field
				// centered on screen without needing to find/modify the activity's real layout, and
				// (b) catch a tap outside the field as Cancel, matching the old dialog's own
				// outside-tap-dismiss behavior (EditText itself consumes taps that land on it, so
				// this listener only ever fires for the transparent area around it).
				final FrameLayout overlay = new FrameLayout(context);
				FrameLayout.LayoutParams inputParams = new FrameLayout.LayoutParams(
						(int) (240 * density), FrameLayout.LayoutParams.WRAP_CONTENT);
				inputParams.gravity = Gravity.CENTER;
				overlay.addView(input, inputParams);

				final Runnable dismiss = new Runnable() {
					@Override
					public void run() {
						InputMethodManager imm = (InputMethodManager)
								context.getSystemService(Context.INPUT_METHOD_SERVICE);
						if (imm != null) {
							imm.hideSoftInputFromWindow(input.getWindowToken(), 0);
						}
						ViewGroup parent = (ViewGroup) overlay.getParent();
						if (parent != null) {
							parent.removeView(overlay);
						}
						// Clears whatever key was pressed to submit/cancel before focus returns to
						// GotView -- see the long comment above promptPlayerName() for why.
						clearInputKeyFlags();
					}
				};

				overlay.setOnClickListener(new View.OnClickListener() {
					@Override
					public void onClick(View v) {
						dismiss.run();
						nameEntryCancelled();
					}
				});
				input.setBackListener(new NameEntryEditText.BackListener() {
					@Override
					public void onBack() {
						dismiss.run();
						nameEntryCancelled();
					}
				});
				input.setOnEditorActionListener(new TextView.OnEditorActionListener() {
					@Override
					public boolean onEditorAction(TextView v, int actionId, KeyEvent event) {
						if (actionId != EditorInfo.IME_ACTION_DONE) {
							return false;
						}
						String name = input.getText().toString().trim();
						dismiss.run();
						if (name.isEmpty()) {
							// Matches got_title_name_entry_cancelled()'s own documented contract
							// (got_title.h): an empty name is treated the same as an explicit
							// cancel, not as a game started under a blank name.
							nameEntryCancelled();
						} else {
							nameEntered(slot, name);
						}
						return true;
					}
				});

				activity.addContentView(overlay, new ViewGroup.LayoutParams(
						ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
				// Clears whatever key was held to reach this screen (e.g. Fire/Confirm, just used
				// to pick an empty slot) before the overlay steals focus -- see the long comment
				// above this method for why.
				clearInputKeyFlags();
				input.requestFocus();
				InputMethodManager imm = (InputMethodManager)
						context.getSystemService(Context.INPUT_METHOD_SERVICE);
				if (imm != null) {
					imm.showSoftInput(input, InputMethodManager.SHOW_IMPLICIT);
				}
			}
		});
	}

	// Same test DescoreView.java uses: a real gamepad/joystick reports one of these two source
	// classes. `device` can be null (a just-removed device's InputDevice.getDevice() call).
	private static boolean isGamepad(InputDevice device) {
		if (device == null) {
			return false;
		}
		int sources = device.getSources();
		return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
				|| (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
	}

	@Override
	public void onInputDeviceAdded(int deviceId) {
		if (isGamepad(InputDevice.getDevice(deviceId))) {
			connectedGamepadIds.add(deviceId);
			setGamepadConnected(!connectedGamepadIds.isEmpty());
		}
	}

	@Override
	public void onInputDeviceRemoved(int deviceId) {
		if (connectedGamepadIds.remove(deviceId)) {
			setGamepadConnected(!connectedGamepadIds.isEmpty());
		}
	}

	@Override
	public void onInputDeviceChanged(int deviceId) {
		// A device can change source class after connecting (rare, but DescoreView.java guards
		// for it too) -- re-evaluate rather than assume whatever we recorded at add-time still
		// holds.
		if (isGamepad(InputDevice.getDevice(deviceId))) {
			connectedGamepadIds.add(deviceId);
		} else {
			connectedGamepadIds.remove(deviceId);
		}
		setGamepadConnected(!connectedGamepadIds.isEmpty());
	}

	// --- Keyboard input ------------------------------------------------------------------------
	// GoT reads input through a flat `volatile char key_flag[100]` scancode array (see
	// 1_movpat.c), structurally identical to Descore's own keyd_pressed[256] -- so the bridge
	// here is the same one-line shim shape Descore uses, just without Descore's gamepad
	// axis/deadzone/remap machinery (GoT has no gamepad support designed yet).
	//
	// D-pad presses need a real scancode, not event.getUnicodeChar() -- Android's D-pad/arrow
	// KeyEvents have no unicode character (getUnicodeChar() returns 0 for both press and release,
	// indistinguishable), so movement silently never reached key_flag[] before this. GoT's own
	// key_up/key_down/key_left/key_right default to 72/80/75/77 (1_init.c's setup_input(), from
	// 1_define.h's UP/DOWN/LEFT/RIGHT) -- a different scancode convention than Descore's own
	// DescoreView.java uses for the same physical keys (0xC8 etc.), so Descore's mapping can't be
	// reused verbatim, just the same intercept-before-getUnicodeChar() pattern.

	@Override
	public boolean onKeyDown(int keyCode, KeyEvent event) {
		boolean handled = false;
		// See isRemappableGamepadButton()'s own comment -- these 7 buttons no longer have a fixed
		// gotScancode() translation at all; native code (got_controls.c) owns the live keyCode<->
		// action table and decides what (if anything) each one currently does.
		if (isRemappableGamepadButton(keyCode)) {
			gamepadButtonRaw(keyCode, true);
			handled = true;
		}
		char scancode = gotScancode(keyCode);
		if (scancode != 0) {
			keyHandler(scancode, true);
			handled = true;
		}
		// See gotExtraScancode()'s own comment -- A/B always send their own dedicated Confirm/
		// Cancel scancode too, ADDITIVELY, regardless of whatever gotScancode()/gamepadButtonRaw()
		// above just did (or didn't do) -- called unconditionally now, not just when gotScancode()
		// itself produced something, since A/B no longer do above the Remap Gamepad feature.
		char extra = gotExtraScancode(keyCode);
		if (extra != 0) {
			keyHandler(extra, true);
			handled = true;
		}
		if (handled) {
			return true;
		}
		keyHandler((char) event.getUnicodeChar(), true);
		return event.getUnicodeChar() != 0;
	}

	@Override
	public boolean onKeyUp(int keyCode, KeyEvent event) {
		boolean handled = false;
		if (isRemappableGamepadButton(keyCode)) {
			gamepadButtonRaw(keyCode, false);
			handled = true;
		}
		char scancode = gotScancode(keyCode);
		if (scancode != 0) {
			keyHandler(scancode, false);
			handled = true;
		}
		char extra = gotExtraScancode(keyCode);
		if (extra != 0) {
			keyHandler(extra, false);
			handled = true;
		}
		if (handled) {
			return true;
		}
		keyHandler((char) event.getUnicodeChar(), false);
		return event.getUnicodeChar() != 0;
	}

	// D-pad plus MAGIC/SELECT now -- got_main.c's got_move_thor() reads key_flag[KEY_MAGIC] to use
	// whichever item is currently selected (got_use_item()); got_item_menu_poll_toggle_key() reads
	// key_flag[KEY_SELECT] to open the item-picker screen. A physical keyboard's ALT key (both left
	// and right) still maps straight to Fire here, matching GoT's own real default binding
	// (key_fire=ALT, 1_init.c) exactly -- on top of the on-screen touch FIRE/MAGIC/SELECT buttons,
	// which reach key_flag[] through touchHandler()/got_controls.c instead of this function.
	//
	// Fire/Magic/Select's own GAMEPAD bindings moved OFF this function as of the "Remap Gamepad"
	// pause-menu feature -- isRemappableGamepadButton()/gamepadButtonRaw() below are what a real
	// gamepad's A/B/X/Y/L1/R1/L3 actually drive now (native code owns the live keyCode<->action
	// table, got_controls.c), so this switch no longer hardcodes BUTTON_A/B/X/Y to anything.
	//
	// BUTTON_START opens/closes got_menu.c's pause/options menu, matching how Descore's own
	// DescoreView.java handles its physical gamepad Start/Menu button -- per gamepad_remap.c's own
	// comment there ("D-pad, Start ('Menu') and Select ('Map') are intentionally never routed
	// through [the remappable-button] system -- DescoreView.handleGamepadKey() still dispatches
	// them directly via its original, fixed keyHandler() calls"), Start is one of a handful of
	// buttons Descore hardcodes here rather than exposing to its remapping UI, and this port
	// follows the same precedent: KEY_ESC (scancode 1) is exactly the key
	// got_menu_poll_toggle_key() (got_menu.c) already edge-detects every frame to toggle the menu
	// -- the same key the on-screen touch BTN_PAUSE button (got_controls.c) presses/releases, so
	// physical Start and the touch button reach the native side through the identical key_flag[]
	// entry, no separate native-side wiring needed.
	//
	// Returns 0 ("not a GoT key") for everything else, which falls through to the existing
	// getUnicodeChar() passthrough above, unchanged.
	private char gotScancode(int keyCode) {
		switch (keyCode) {
			case KeyEvent.KEYCODE_DPAD_UP:    return (char) 72;
			case KeyEvent.KEYCODE_DPAD_DOWN:  return (char) 80;
			case KeyEvent.KEYCODE_DPAD_LEFT:  return (char) 75;
			case KeyEvent.KEYCODE_DPAD_RIGHT: return (char) 77;
			case KeyEvent.KEYCODE_ALT_LEFT:
			case KeyEvent.KEYCODE_ALT_RIGHT:  return (char) 56; // KEY_FIRE, real ALT scancode --
															     // physical keyboard only; a real
															     // gamepad's Fire binding (default
															     // A) goes through gamepadButtonRaw()
															     // below instead.
			case KeyEvent.KEYCODE_BUTTON_START: return (char) 1; // KEY_ESC -- pause/options menu
			// New this round -- wootbeer: "add a button to delete player saves from the 'Select Player'
			// menu, make it the Retroid Pocket 6's 'Select' button." KEYCODE_BUTTON_SELECT is a
			// distinct Android keycode from BUTTON_Y (this port's own, differently-named "Select
			// Item" gamepad action, see isRemappableGamepadButton()), so the two don't collide
			// despite the similar names -- see got_title.c's own KEY_DELETE_SAVE comment for the
			// full picture (only read by that screen's own GOT_APP_PLAYER_SELECT case, harmless
			// everywhere else key_flag[] is read).
			case KeyEvent.KEYCODE_BUTTON_SELECT: return (char) 83; // KEY_DELETE_SAVE
			default: return 0;
		}
	}

	// The 7 gamepad buttons a real gamepad can remap Fire/Magic/Select Item to (got_controls.c's
	// own kGamepadRemapActions[]/capturable pool, "Remap Gamepad" pause-menu feature) -- D-pad,
	// Start, and Select (delete-save) are deliberately excluded, same as gamepad_remap.c's own
	// exclusion of Descore's D-pad/Start/Select, and stay hardcoded in gotScancode() above instead.
	// onKeyDown()/onKeyUp() call gamepadButtonRaw() for exactly these, ALWAYS, regardless of pause-
	// menu state -- there's no Java-side "is a menu open" gate the way Descore's own
	// handleGamepadKey() needs, because native code already only acts on the resulting key_flag[]
	// writes where it makes sense (got_move_thor() during actual gameplay, got_menu_update()'s own
	// capture poll while the Remap Gamepad screen is up) -- see got_controls.c's own "Gamepad
	// remapping" section comment for the full design.
	private boolean isRemappableGamepadButton(int keyCode) {
		switch (keyCode) {
			case KeyEvent.KEYCODE_BUTTON_A:
			case KeyEvent.KEYCODE_BUTTON_B:
			case KeyEvent.KEYCODE_BUTTON_X:
			case KeyEvent.KEYCODE_BUTTON_Y:
			case KeyEvent.KEYCODE_BUTTON_L1:
			case KeyEvent.KEYCODE_BUTTON_R1:
			case KeyEvent.KEYCODE_BUTTON_THUMBL:
				return true;
			default:
				return false;
		}
	}

	// A/B's own dedicated Confirm/Cancel scancode, decoupled from whatever Fire is currently
	// remapped to (see got_menu.c's own KEY_CONFIRM #define comment) -- got_title.c's "Delete
	// Save?" confirmation (GOT_APP_DELETE_CONFIRM) also wants A specifically to mean Yes/confirm and
	// B specifically to mean No/cancel -- wootbeer: "can be the Retroid Pocket 6's 'A' button for confirm
	// and 'B' button for cancel." Since gotScancode() no longer sends A/B to Fire at all, telling
	// them apart at all needs each one to also send its own second,
	// dedicated scancode -- onKeyDown()/onKeyUp() above call this UNCONDITIONALLY now (not just
	// alongside a Fire signal gotScancode() no longer produces for A/B), so it fires every single
	// physical A/B press regardless of pause-menu state or what Fire's currently remapped to. Two
	// consumers read it today: got_title.c's own delete-save-confirm screen (as originally built),
	// and -- new this round -- got_menu.c's own got_menu_update(), which now also accepts KEY_CONFIRM
	// as an always-on alternate to Fire for menu confirm, and already read KEY_CANCEL as an always-
	// on "back out" signal before this round (see that function's own comments). Outside anything
	// that explicitly reads them, KEY_CONFIRM/KEY_CANCEL just sit unused in key_flag[], same as
	// before. Picked from real DOS scancode VALUES purely as a memorable mnemonic (28=Enter,
	// 14=Backspace) -- there's no real GoT mechanic either button actually matches, same "original
	// design" scope as KEY_DELETE_SAVE above and got_title.c's own screen as a whole.
	private char gotExtraScancode(int keyCode) {
		switch (keyCode) {
			case KeyEvent.KEYCODE_BUTTON_A: return (char) 28; // KEY_CONFIRM
			case KeyEvent.KEYCODE_BUTTON_B: return (char) 14; // KEY_CANCEL
			default: return 0;
		}
	}

	@Override
	public boolean onKeyMultiple(int keyCode, int count, KeyEvent event) {
		return false;
	}

	@Override
	public boolean onKeyLongPress(int keyCode, KeyEvent event) {
		return false;
	}

	// Forwards raw multi-touch pointer events to native touchHandler() (got_controls.c), which
	// hit-tests them against the on-screen D-pad and turns hits into key_flag[] writes -- same
	// per-pointer-index loop DescoreView.onTouchEvent() uses (ACTION_POINTER_DOWN/UP only
	// concern the one pointer that changed -- getActionIndex(); ACTION_MOVE can carry more than
	// one pointer's new position at once -- every pointer via getPointerCount()). Unlike
	// DescoreView, there's no renderScale conversion here: Descore's game canvas renders at a
	// different resolution than the screen and needs touch coordinates scaled to match, but
	// GoT's on-screen buttons are drawn directly in real screen pixels (see got_controls.c's
	// comment on why), so raw event coordinates already line up with what native hit-tests
	// against.
	@SuppressLint("ClickableViewAccessibility")
	@Override
	public boolean onTouchEvent(MotionEvent event) {
		int action = event.getActionMasked();
		int firstPointerIndex = (action == MotionEvent.ACTION_POINTER_DOWN
				|| action == MotionEvent.ACTION_POINTER_UP) ? event.getActionIndex() : 0;
		int numPointers = (action == MotionEvent.ACTION_MOVE) ? event.getPointerCount() : 1;
		boolean touchHandled = false;

		for (int i = firstPointerIndex; i < numPointers + firstPointerIndex; ++i) {
			float prevX, prevY;
			if (event.getHistorySize() > 0) {
				prevX = event.getHistoricalX(i, 0);
				prevY = event.getHistoricalY(i, 0);
			} else {
				prevX = event.getX(i);
				prevY = event.getY(i);
			}
			touchHandled |= touchHandler(action, event.getPointerId(i), event.getX(i),
					event.getY(i), prevX, prevY);
		}
		return touchHandled;
	}

	// --- Render-thread / EGL lifecycle ----------------------------------------------------------

	@Override
	public void surfaceCreated(SurfaceHolder holder) {
		if (!gotRunning) {
			new Thread(new Runnable() {
				@Override
				public void run() {
					size = new Point(getWidth(), getHeight());
					initEgl();
					gotMain(size.x, size.y, context, thiz, context.getAssets(),
							context.getFilesDir().getAbsolutePath(),
							context.getCacheDir().getAbsolutePath());
				}
			}).start();
			gotRunning = true;
		} else {
			this.holder = holder;
			resumeRenderThread();
		}
	}

	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
		// See DescoreView's surfaceChanged() for why this re-pins the buffer rather than doing
		// nothing: a transient bounds change (on-screen keyboard, system bars) shouldn't leave
		// a stale-sized buffer compositing against new bounds.
		if (size != null && holder != null) {
			holder.setFixedSize(size.x, size.y);
		}
	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		surfaceWasDestroyed = true;
		surfaceWasDestroyed();
	}

	public boolean getSurfaceWasDestroyed() {
		return surfaceWasDestroyed;
	}

	public void resumeRenderThread() {
		synchronized (renderThreadObj) {
			paused = false;
			surfaceWasDestroyed = false;
			renderThreadObj.notifyAll();
		}
	}

	/** Called via JNI from got_show_render_buffer() (got_main.c) on the render thread -- blocks
	 *  it until resumeRenderThread() above is called back on the main thread. */
	@SuppressWarnings("unused")
	private void pauseRenderThread() {
		synchronized (renderThreadObj) {
			paused = true;
			while (paused) {
				try {
					renderThreadObj.wait();
				} catch (InterruptedException e) {
					e.printStackTrace();
				}
			}
		}
	}

	/**
	 * Initializes EGL for the current (render) thread. Also called from native via JNI after a
	 * surface is torn down and recreated -- see got_show_render_buffer() in got_main.c.
	 */
	@SuppressWarnings("unused")
	private void initEgl() {
		int EGL_CONTEXT_CLIENT_VERSION = 0x3098;
		int[] num_config = new int[1];
		final EGLConfig[] configs = new EGLConfig[1];
		int[] attrib_list = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL10.EGL_NONE};
		EGL10 egl;
		EGLConfig eglConfig;
		EGLContext eglContext;
		EGLDisplay eglDisplay;
		EGLSurface eglSurface;
		GL10 gl;

		// setFixedSize() can trigger requestLayout(), which Android only allows from the thread
		// that owns the view hierarchy -- this method always runs on the background render
		// thread (including when native re-invokes it via JNI after a pause/resume). A normal
		// cold launch has enough of a head start before this runs that the race goes unnoticed,
		// but GotActivity.launchGame() -- like Descore's -- can be invoked from a background
		// copy thread's runOnUiThread() right after the SAF folder picker finishes, which
		// replaces an *already fully-attached* window's content view and crashes reliably
		// without this fix (see Descore-Mobile CHANGELOG.md, "Crash on first launch after
		// picking the game-data folder" -- same launch path, same bug, ported the same fix).
		if (size != null) {
			final Point fixedSize = size;
			final Object fixedSizeDone = new Object();
			final boolean[] applied = {false};
			synchronized (fixedSizeDone) {
				mainHandler.post(new Runnable() {
					@Override
					public void run() {
						holder.setFixedSize(fixedSize.x, fixedSize.y);
						synchronized (fixedSizeDone) {
							applied[0] = true;
							fixedSizeDone.notifyAll();
						}
					}
				});
				while (!applied[0]) {
					try {
						fixedSizeDone.wait();
					} catch (InterruptedException e) {
						Thread.currentThread().interrupt();
						break;
					}
				}
			}
		}

		egl = (EGL10) EGLContext.getEGL();
		eglDisplay = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
		egl.eglInitialize(eglDisplay, new int[]{1, 0});
		egl.eglChooseConfig(eglDisplay, new int[]{
				EGL10.EGL_RED_SIZE, 8,
				EGL10.EGL_GREEN_SIZE, 8,
				EGL10.EGL_BLUE_SIZE, 8,
				// No alpha / depth / stencil -- GoT is a flat 2D tile-and-sprite game (one
				// textured fullscreen quad per frame, see notes section 6a), none of these are
				// needed. Kept explicit at 0 rather than omitted so the intent is clear.
				EGL10.EGL_ALPHA_SIZE, 0,
				EGL10.EGL_DEPTH_SIZE, 0,
				EGL10.EGL_STENCIL_SIZE, 0,
				EGL10.EGL_NONE}, configs, 1, num_config);
		eglConfig = configs[0];
		eglContext = egl.eglCreateContext(eglDisplay, eglConfig, EGL10.EGL_NO_CONTEXT, attrib_list);
		eglSurface = egl.eglCreateWindowSurface(eglDisplay, eglConfig, holder, null);
		egl.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
		gl = (GL10) eglContext.getGL();

		gl.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		gl.glClear(GL10.GL_COLOR_BUFFER_BIT);
		gl.glViewport(0, 0, size.x, size.y);

		// Blending, for sprite alpha once real GL assets exist. Cheap to enable now.
		gl.glEnable(GL10.GL_BLEND);
		gl.glBlendFunc(GL10.GL_SRC_ALPHA, GL10.GL_ONE_MINUS_SRC_ALPHA);
	}

	// --- Native entry points -------------------------------------------------------------------

	private static native void keyHandler(char key, boolean down);

	private static native boolean touchHandler(int action, int pointerId, float x, float y,
												float prevX, float prevY);

	private static native void setGamepadConnected(boolean connected);

	// Raw Android keyCode forwarding for the "Remap Gamepad" feature -- see
	// isRemappableGamepadButton()'s own comment above for exactly which 7 buttons this is called
	// for, and got_controls.c's own Java_wootbeer_gotandroid_GotView_gamepadButtonRaw() for what
	// native code does with it (either records it for the remap screen's own capture step, or
	// dispatches it to whichever action -- if any -- currently claims that keycode).
	private static native void gamepadButtonRaw(int keyCode, boolean down);

	private static native void surfaceWasDestroyed();

	private static native void gotMain(int w, int h, Context activity, GotView gotView,
										AssetManager assetManager, String documentPath,
										String cachePath);

	// got_title.c's own player-name-entry result, from promptPlayerName() above -- see
	// got_main.c's Java_wootbeer_gotandroid_GotView_nameEntered()/_nameEntryCancelled() JNI
	// exports, which forward straight into got_title_name_entered()/
	// got_title_name_entry_cancelled().
	private static native void nameEntered(int slot, String name);

	private static native void nameEntryCancelled();
}
