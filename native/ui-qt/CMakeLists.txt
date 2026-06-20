# Qt 6 GUI. Dual-pane Commander; local panel backed by the ported engine via enginebridge.

find_package(Qt6 COMPONENTS Widgets QUIET)

if(NOT Qt6_FOUND)
  message(WARNING "Qt6 not found; skipping ui-qt target. brew install qt, then re-run cmake "
                  "with -DCMAKE_PREFIX_PATH=$(brew --prefix qt).")
  return()
endif()

set(CMAKE_AUTOMOC ON)

# enginebridge: the ONLY UI code compiled with engine flags (-fshort-wchar -fms-extensions via
# winscpcore_flags). Exposes a clean std-only header to the Qt code.
add_library(enginebridge STATIC enginebridge.cpp)
target_link_libraries(enginebridge PRIVATE winscpcore_flags)
target_include_directories(enginebridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The Qt app — normal flags (QString/4-byte wchar_t); never sees engine/rtlcompat headers.
add_executable(winscp-qt MACOSX_BUNDLE main.cpp)
target_link_libraries(winscp-qt PRIVATE Qt6::Widgets enginebridge)
