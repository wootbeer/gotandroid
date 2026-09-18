cmake_minimum_required(VERSION 3.4.1)

# Real render shell, native side. got_main.c has the JNI entry point (gotMain), the EGL
# pause/resume handshake, the keyboard bridge, real GOTRES.DAT resource loading (res_man.c/
# lzss.c -- a C port of the already-verified Python logic, see notes sections 2/2a/5) that draws
# the user's own real BPICS1 tiles on screen (falling back to a synthetic self-test draw if that
# fails), and Thor's real player-controlled movement; got_controls.c has the on-screen touch
# D-pad and gamepad auto-detect (its own JNI entry points, touchHandler()/setGamepadConnected());
# got_hud.c has the minimal/alternate HUD overlay (Display Mode C, health/magic bars + jewels/keys/
# score, see that file's own comment); got_panel.c has the real authentic status panel (Display
# Modes A/B, a port of 1_panel.c's own display_*() functions -- see that file's own header
# comment); got_font.c has real bitmap-font text rendering (a port of xprint(), built on
# modex/'s now-real xtext()); got_menu.c has the pause/options menu (a port of select_option()/
# option_menu()/ask_exit(), drawn with got_font.c into the real page buffer); got_title.c has the
# title screen / player-selection screen shown before a game starts (an ORIGINAL design, not a
# port -- see that file's own header comment for why: the real title/menu program is a separate
# executable never part of this project's decompiled source); got_item.c has the item picker (a
# port of select_item(), reusing got_menu.c's own box-drawing conventions -- see got_item.h's own
# header comment); got_script.c has the
# real dialogue/scripting engine (a port of 1_script.c's execute_script() and its full real command
# interpreter, run against the real SPEAK1 resource); got_dialogue.c has the SAY/TEXT/ITEMSAY
# dialogue box (a port of display_speech()) and the ASK prompt (a second real select_option() port)
# those scripts drive; got_story.c has the new-game story sequence (real STORY1/2/3 narration text
# plus a chapter-title card, shown once per brand new episode -- a port of 1_init.c's/2_init.c's/
# 3_init.c's own story() function and the title-card xprintx() calls right after it, see that
# file's own header comment); got_stub.c still holds JNI_OnLoad and the native gotPause() GotActivity
# calls. The actual ported GoT game loop (io/, main/) doesn't exist yet -- see
# got-android-port-notes_1.md for that plan.
#
# TODO: once the ported game code exists, this follows ../../../../../Descore-Mobile-master's own
# Descore/src/main/cpp/CMakeLists.txt as a template -- add_subdirectory() each ported engine
# folder and link it into this "got" target, same as modex/ below.

add_library(got SHARED
		got_stub.c
		got_main.c
		got_controls.c
		got_hud.c
		got_panel.c
		got_font.c
		got_menu.c
		got_title.c
		got_item.c
		got_script.c
		got_dialogue.c
		got_story.c
		res_man.c
		lzss.c
)

# ../../../../modex is gotandroid-master/modex -- a sibling of the gotandroid/ module this
# CMakeLists.txt lives inside, not a separate top-level checkout (unlike Descore-Mobile-master
# above), so it's one directory shallower.
add_subdirectory(../../../../modex modex)

find_library(log-lib log)
find_library(GLESv1_CM-lib GLESv1_CM)
find_library(EGL-lib EGL)
find_library(android-lib android)

target_link_libraries(got ${log-lib})
target_link_libraries(got ${GLESv1_CM-lib})
target_link_libraries(got ${EGL-lib})
target_link_libraries(got ${android-lib})
target_link_libraries(got modex)
