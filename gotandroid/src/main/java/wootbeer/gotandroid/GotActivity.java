package wootbeer.gotandroid;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Adapted from the Descore Android port's DescoreActivity.java. Same Storage Access Framework
 * folder-picker pattern (the user's copy of the game data is proprietary, so it isn't bundled
 * in the APK -- the user picks the folder containing their own copy, and we copy what we need
 * into private app storage), same SurfaceView lifecycle shape.
 *
 * Simplified relative to Descore's version:
 *  - One data file (GOTRES.DAT) instead of two (DESCORE.HOG/DESCORE.PIG) -- GoT's whole resource
 *    archive (levels, sprites, actors, palette, everything) lives in this single file.
 *  - No gyroscope motion controls -- that's Descore's mouselook-by-tilting-the-device feature,
 *    specific to a first-person 3D game. GoT is top-down 2D; there's no camera to tilt.
 *  - No render-scale / force-4:3 settings menu (yet) -- Descore's version of this is tied to its
 *    own in-game Detail Level Customization menu, which GoT doesn't have an equivalent of. Worth
 *    revisiting once the core engine is running: GoT's original 320x192 resolution was displayed
 *    on non-square CRT pixels, so there's a real "faithful aspect ratio" question here similar to
 *    Descore's Force 4:3, just not wired up yet.
 *  - No MIDI playback via MediaPlayer -- GoT's music/sound were released public domain and are
 *    bundled as pre-rendered audio files rather than synthesized live (see the AdLib/OPL2 finding
 *    in the project notes; wootbeer: "the original game has a toggle for midi/digital audio. but i
 *    don't want to worry about including midi in our port"), so there's no user-supplied MIDI path
 *    to mirror. A bundled-MP3 player using the same MediaPlayer mechanism Descore uses for MIDI is
 *    now built (GotView.java's playMusic()/pauseMusic()/resumeMusic(), notes section 31) -- this
 *    file's own onPause()/onResume() just relay the app-background lifecycle into it below.
 */
public class GotActivity extends Activity {
	// Debugging note: two straight logcat captures (log15.txt, log16.txt -- the second one even
	// after a full Android Studio "Invalidate Caches / Restart") showed ZERO lines tagged
	// "GotActivity", despite Log.i calls having supposedly been added throughout onCreate()/
	// launchGame()/doCopyDataFile() to chase the ACTOR44/ACTOR14 read-failure bug. Root cause turned
	// out to be a tooling mistake on the assistant's end, not anything in this project: the edited
	// file got silently overwritten by a stale re-fetch from the device right before it was shipped,
	// so the logging (and the deploy) never actually happened -- two full clean-reinstall test cycles
	// were spent debugging a build that was never running the new code. BUILD_MARKER exists so this
	// specific failure mode can never happen invisibly again: it's shown as an on-screen Toast at
	// launch, which is unambiguous in a way a logcat capture (subject to filter/export mistakes, as
	// above) isn't -- if wootbeer doesn't see it after a rebuild, the new code flatly isn't running, full
	// stop, no log-tooling ambiguity possible.
	private static final String TAG = "GotActivity";
	private static final String BUILD_MARKER = "gotres-diag-v2";

	// GOTRES.DAT is the entire resource archive -- levels, sprites, actors, palette, everything
	// except the (already-bundled, public-domain) music and sound effects. Proprietary, so the
	// user must supply their own copy.
	private static final String DATA_FILENAME_GOTRES = "GOTRES.DAT";
	private static final int REQUEST_CODE_OPEN_DATA_FOLDER = 4242;
	// The real GOTRES.DAT for this release is 739732 bytes; this floor is well below that (leaving
	// slack for a legitimately different-but-still-complete build of the archive) while still well
	// above the ~305KB where a REAL bug was found and fixed: doCopyDataFile()'s copy loop had no
	// check that it actually copied the whole file, so an interrupted/short SAF read (the device
	// killing the app mid-copy, a flaky source stream, etc.) silently produced a truncated
	// GOTRES.DAT that onCreate()'s old `gotResFile.exists()` check accepted as good forever after --
	// every resource whose data happens to sit past the truncation point then fails to read
	// (confirmed via logcat: "got_load_sprite: ACTOR44 read failed (-1)" and "ACTOR14 read failed
	// (-1)", both of whose real offsets in a 739732-byte GOTRES.DAT land past the ~305KB mark, while
	// every earlier-offset resource wootbeer had tested up to that point -- including ACTOR10, GLOBE's
	// own oracle globe, whose data ends at offset 305189 -- happened to still be intact). Two-part
	// fix: this floor lets onCreate() catch an *already*-truncated file left over from that bug (see
	// its own comment) and re-prompt for a fresh copy, and doCopyDataFile() now verifies the copy
	// actually completed (see its own comment) so a future interrupted copy fails loudly instead of
	// being silently accepted.
	private static final long MIN_PLAUSIBLE_GOTRES_SIZE = 700_000L;

	private GotView gotView;
	private File gotResFile;
	private float buttonSizeBias;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		// See BUILD_MARKER's own comment -- unmissable, log-tooling-proof confirmation that this
		// exact build is the one running. Safe to delete once the underlying ACTOR44/ACTOR14 bug is
		// actually confirmed fixed.
		Toast.makeText(this, "GotActivity build " + BUILD_MARKER + " running", Toast.LENGTH_LONG).show();

		setImmersive();
		if (Build.VERSION.SDK_INT >= 19) {
			getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(
					new View.OnSystemUiVisibilityChangeListener() {
						@Override
						public void onSystemUiVisibilityChange(int visibility) {
							if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
								setImmersive();
							}
						}
					});
		}

		DisplayMetrics metrics = getResources().getDisplayMetrics();
		buttonSizeBias = (float) Math.min(Math.max((metrics.widthPixels / metrics.xdpi
				+ metrics.heightPixels / metrics.ydpi) / 5.5f, 1), 1.4);

		gotResFile = new File(getFilesDir(), DATA_FILENAME_GOTRES);

		Log.i(TAG, "onCreate: internal " + DATA_FILENAME_GOTRES + " exists=" + gotResFile.exists()
				+ " length=" + (gotResFile.exists() ? gotResFile.length() : -1)
				+ " (floor=" + MIN_PLAUSIBLE_GOTRES_SIZE + ")");

		if (gotResFile.exists() && gotResFile.length() < MIN_PLAUSIBLE_GOTRES_SIZE) {
			// A truncated copy from before doCopyDataFile()'s own completeness check existed (see
			// MIN_PLAUSIBLE_GOTRES_SIZE's own comment) -- existence alone isn't enough evidence this
			// file is actually usable. Delete it and fall through to the picker for a fresh copy
			// rather than launching into a game that's silently missing whatever resources happen to
			// sit past wherever this one got cut off.
			Log.i(TAG, "onCreate: internal copy is under the plausible-size floor -- deleting it and"
					+ " re-prompting for a fresh copy");
			//noinspection ResultOfMethodCallIgnored
			gotResFile.delete();
		}

		if (gotResFile.exists()) {
			Log.i(TAG, "onCreate: internal copy looks usable, launching game directly (no picker)");
			launchGame();
		} else {
			Log.i(TAG, "onCreate: no usable internal copy, showing folder picker");
			showDataPicker(null);
		}
	}

	/**
	 * Creates the OpenGL ES view and starts the native game engine. Only safe to call once
	 * GOTRES.DAT is present in {@link #getFilesDir()}.
	 */
	private void launchGame() {
		// The decisive line: whatever size is logged here is the exact file the native engine is
		// about to res_open() -- if this doesn't say 739732 (or close to it), the copy/verification
		// path above didn't do its job, whether or not the picker/copy ran this session.
		Log.i(TAG, "launchGame: about to start native engine with " + DATA_FILENAME_GOTRES
				+ " length=" + (gotResFile != null && gotResFile.exists() ? gotResFile.length() : -1));

		gotView = new GotView(this);
		setContentView(gotView);

		// Keep the screen from going to sleep
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
	}

	// --- User-supplied game data (Storage Access Framework) --------------------------------

	private void showDataPicker(String errorMessage) {
		LinearLayout layout = new LinearLayout(this);
		layout.setOrientation(LinearLayout.VERTICAL);
		layout.setGravity(Gravity.CENTER);
		layout.setBackgroundColor(Color.BLACK);
		int pad = (int) dpToPx(24);
		layout.setPadding(pad, pad, pad, pad);

		TextView title = new TextView(this);
		title.setText("God of Thunder game data needed");
		title.setTextColor(Color.WHITE);
		title.setTextSize(22);
		title.setGravity(Gravity.CENTER);
		layout.addView(title);

		TextView message = new TextView(this);
		message.setText("Select the folder that contains your own copy of GOTRES.DAT.");
		message.setTextColor(Color.LTGRAY);
		message.setGravity(Gravity.CENTER);
		message.setPadding(0, (int) dpToPx(16), 0, (int) dpToPx(16));
		layout.addView(message);

		if (errorMessage != null) {
			TextView error = new TextView(this);
			error.setText(errorMessage);
			error.setTextColor(Color.rgb(255, 120, 120));
			error.setGravity(Gravity.CENTER);
			error.setPadding(0, 0, 0, (int) dpToPx(16));
			layout.addView(error);
		}

		Button chooseButton = new Button(this);
		chooseButton.setText("Choose Folder");
		chooseButton.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				openFolderPicker();
			}
		});
		layout.addView(chooseButton);

		setContentView(layout);
	}

	private void showCopyingProgress() {
		LinearLayout layout = new LinearLayout(this);
		layout.setOrientation(LinearLayout.VERTICAL);
		layout.setGravity(Gravity.CENTER);
		layout.setBackgroundColor(Color.BLACK);

		ProgressBar progressBar = new ProgressBar(this);
		layout.addView(progressBar);

		TextView text = new TextView(this);
		text.setText("Copying game data...");
		text.setTextColor(Color.WHITE);
		text.setGravity(Gravity.CENTER);
		text.setPadding(0, (int) dpToPx(16), 0, 0);
		layout.addView(text);

		setContentView(layout);
	}

	private void openFolderPicker() {
		if (Build.VERSION.SDK_INT < 21) {
			showDataPicker("This Android version can't select external files.");
			return;
		}
		Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
		startActivityForResult(intent, REQUEST_CODE_OPEN_DATA_FOLDER);
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		if (requestCode == REQUEST_CODE_OPEN_DATA_FOLDER) {
			if (resultCode != RESULT_OK || data == null || data.getData() == null) {
				// User cancelled -- leave the picker screen showing so they can try again.
				return;
			}
			copyDataFileFromTree(data.getData());
		}
	}

	private void copyDataFileFromTree(final Uri treeUri) {
		showCopyingProgress();
		new Thread(new Runnable() {
			@Override
			public void run() {
				final String error = doCopyDataFile(treeUri);
				runOnUiThread(new Runnable() {
					@Override
					public void run() {
						if (error != null) {
							showDataPicker(error);
						} else {
							launchGame();
						}
					}
				});
			}
		}).start();
	}

	/**
	 * Runs on a background thread. Returns null on success, or a user-facing error message.
	 */
	private String doCopyDataFile(Uri treeUri) {
		File gotResTemp = new File(getFilesDir(), DATA_FILENAME_GOTRES + ".tmp");
		try {
			Uri gotResUri = findChildDocument(treeUri, DATA_FILENAME_GOTRES);
			if (gotResUri == null) {
				Log.i(TAG, "doCopyDataFile: " + DATA_FILENAME_GOTRES + " not found under picked tree");
				return "Couldn't find " + DATA_FILENAME_GOTRES
						+ " in that folder. Please pick a folder that contains it.";
			}
			// Real bug this closes off (see MIN_PLAUSIBLE_GOTRES_SIZE's own comment): the copy loop
			// below has no way to tell a short read from a completed one on its own, so an
			// interrupted SAF stream (app backgrounded/killed mid-copy, a flaky source, etc.) used to
			// produce a truncated file that got renamed into place and accepted just the same as a
			// complete one. Ask the source document its own real size first, then check what actually
			// landed on disk against it -- a mismatch means the copy didn't finish, not that it did.
			long expectedSize = queryDocumentSize(gotResUri);
			Log.i(TAG, "doCopyDataFile: found " + gotResUri + ", provider-reported size="
					+ expectedSize + " (-1 means the provider didn't report one -- verification below"
					+ " is skipped in that case)");
			long copiedSize = copyUriToFile(gotResUri, gotResTemp);
			Log.i(TAG, "doCopyDataFile: copy loop wrote " + copiedSize + " bytes to " + gotResTemp);
			if (expectedSize >= 0 && copiedSize != expectedSize) {
				Log.i(TAG, "doCopyDataFile: size mismatch, rejecting copy");
				//noinspection ResultOfMethodCallIgnored
				gotResTemp.delete();
				return "Copy of " + DATA_FILENAME_GOTRES + " was incomplete (got " + copiedSize
						+ " of " + expectedSize + " bytes). Please try again.";
			}
			if (!gotResTemp.renameTo(gotResFile)) {
				Log.i(TAG, "doCopyDataFile: renameTo(" + gotResFile + ") failed");
				return "Couldn't finish copying the data file. Please try again.";
			}
			Log.i(TAG, "doCopyDataFile: success, final size on disk=" + gotResFile.length());
			return null;
		} catch (IOException e) {
			Log.i(TAG, "doCopyDataFile: IOException: " + e.getMessage());
			//noinspection ResultOfMethodCallIgnored
			gotResTemp.delete();
			return "Couldn't copy the data file: " + e.getMessage();
		}
	}

	// Returns the source document's own reported size in bytes, or -1 if it couldn't be determined
	// (some providers don't report COLUMN_SIZE) -- callers treat -1 as "can't verify, proceed
	// anyway" rather than a hard failure, since a provider that genuinely can't report size is a
	// separate, rarer problem than the truncated-copy bug this is guarding against.
	private long queryDocumentSize(Uri documentUri) {
		Cursor cursor = getContentResolver().query(documentUri,
				new String[]{DocumentsContract.Document.COLUMN_SIZE}, null, null, null);
		if (cursor == null) {
			return -1;
		}
		try {
			if (cursor.moveToFirst() && !cursor.isNull(0)) {
				return cursor.getLong(0);
			}
			return -1;
		} finally {
			cursor.close();
		}
	}

	private Uri findChildDocument(Uri treeUri, String displayName) {
		String treeDocId = DocumentsContract.getTreeDocumentId(treeUri);
		Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, treeDocId);
		Cursor cursor = getContentResolver().query(childrenUri, new String[]{
				DocumentsContract.Document.COLUMN_DOCUMENT_ID,
				DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null);
		if (cursor == null) {
			return null;
		}
		try {
			while (cursor.moveToNext()) {
				String name = cursor.getString(1);
				if (displayName.equalsIgnoreCase(name)) {
					String docId = cursor.getString(0);
					return DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
				}
			}
		} finally {
			cursor.close();
		}
		return null;
	}

	private long copyUriToFile(Uri uri, File destination) throws IOException {
		InputStream in = null;
		OutputStream out = null;
		long totalCopied = 0;
		try {
			in = getContentResolver().openInputStream(uri);
			if (in == null) {
				throw new IOException("could not open " + uri);
			}
			out = new FileOutputStream(destination);
			byte[] buffer = new byte[64 * 1024];
			int read;
			while ((read = in.read(buffer)) != -1) {
				out.write(buffer, 0, read);
				totalCopied += read;
			}
			out.flush();
			return totalCopied;
		} finally {
			if (in != null) {
				try {
					in.close();
				} catch (IOException ignored) {
				}
			}
			if (out != null) {
				try {
					out.close();
				} catch (IOException ignored) {
				}
			}
		}
	}

	@Override
	protected void onPause() {
		super.onPause();
		// gotView (and the native engine) only exist once GOTRES.DAT is in place -- onPause can
		// fire earlier than that, e.g. while the SAF folder picker or the data copy is in
		// progress.
		if (gotView != null) {
			gotPause();
			// Pauses whatever background music is currently playing (see GotView.onAppPause()'s
			// own comment) -- deliberately independent of gotPause()/the native render-thread pause
			// above: real music_pause()/music_resume() are their own thing in the DOS source too,
			// not tied to the render loop, and MediaPlayer has no reason to keep playing while the
			// app is backgrounded regardless of what the native side is doing.
			gotView.onAppPause();
		}
	}

	@Override
	protected void onResume() {
		super.onResume();
		setImmersive();
		if (gotView != null && !gotView.getSurfaceWasDestroyed()) {
			gotView.resumeRenderThread();
		}
		if (gotView != null) {
			gotView.onAppResume();
		}
	}

	@SuppressWarnings("unused")
	private float dpToPx(float dp) {
		DisplayMetrics metrics = getResources().getDisplayMetrics();
		return dp * (((float) metrics.densityDpi / DisplayMetrics.DENSITY_DEFAULT) * buttonSizeBias);
	}

	@SuppressWarnings("unused")
	private float pxToDp(float px) {
		DisplayMetrics metrics = getResources().getDisplayMetrics();
		return px / (((float) metrics.densityDpi / DisplayMetrics.DENSITY_DEFAULT) * buttonSizeBias);
	}

	/**
	 * Enables immersive mode, hiding navigation controls
	 */
	private void setImmersive() {
		if (Build.VERSION.SDK_INT >= 19) {
			getWindow().getDecorView().setSystemUiVisibility(
					View.SYSTEM_UI_FLAG_LAYOUT_STABLE
							| View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
							| View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
							| View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
							| View.SYSTEM_UI_FLAG_FULLSCREEN
							| View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
		}
	}

	// TODO: implemented on the native side, mirroring Descore's descorePause() in motion.c --
	// should just set the same Want_pause-style flag GoT's own game loop checks.
	private static native void gotPause();

	static {
		// TODO: match this to whatever your native library ends up being named in CMakeLists.txt
		System.loadLibrary("got");
	}
}
