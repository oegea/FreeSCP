# Qt 6 GUI (Phase 0 skeleton -> Phase 6+ real Commander/Explorer rewrite of source/forms).

find_package(Qt6 COMPONENTS Widgets QUIET)

if(NOT Qt6_FOUND)
  message(WARNING "Qt6 not found; skipping ui-qt target. brew install qt, then re-run cmake "
                  "with -DCMAKE_PREFIX_PATH=$(brew --prefix qt).")
  return()
endif()

set(CMAKE_AUTOMOC ON)

add_executable(winscp-qt MACOSX_BUNDLE main.cpp)
target_link_libraries(winscp-qt PRIVATE Qt6::Widgets)
# Phase 6: target_link_libraries(winscp-qt PRIVATE winscpcore)
