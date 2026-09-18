cmake_minimum_required(VERSION 3.4.1)

# The modex.h GL rendering backend -- see modex.h's header comment for the full rationale. Built
# as its own static library (rather than just adding modexgl.c to got's own source list) so it
# stays a clean, separately-buildable unit matching how it's organized on disk (its own top-level
# folder, sibling to gotandroid/ -- see got-android-port-notes_1.md section 6a).

add_library(modex STATIC
        modexgl.c
)

target_include_directories(modex PUBLIC .)

find_library(GLESv1_CM-lib GLESv1_CM)
find_library(log-lib log)

target_link_libraries(modex ${GLESv1_CM-lib} ${log-lib})
