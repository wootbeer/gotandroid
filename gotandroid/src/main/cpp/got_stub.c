// JNI_OnLoad and the native gotPause() GotActivity.java calls. The real render-thread entry
// point (gotMain(), the EGL pause/resume handshake, the keyboard bridge) is now in got_main.c,
// which also defines Want_pause below -- see that file for the full picture.

#include <jni.h>
#include <android/log.h>
#include <stdbool.h>

#define LOG_TAG "GotNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

extern volatile bool Want_pause; // defined in got_main.c

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
	(void) vm;
	(void) reserved;
	LOGI("libgot loaded");
	return JNI_VERSION_1_6;
}

// Matches GotActivity.java's: private static native void gotPause();
// Mirrors Descore's Java_..._descorePause() in main.c exactly: just sets the flag.
// got_show_render_buffer() (got_main.c), called once per frame from the render thread, is what
// actually acts on it.
JNIEXPORT void JNICALL Java_wootbeer_gotandroid_GotActivity_gotPause(JNIEnv *env, jclass clazz) {
	(void) env;
	(void) clazz;
	Want_pause = true;
}
