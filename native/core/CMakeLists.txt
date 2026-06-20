# Ported WinSCP engine (source/core) -> libwinscpcore.
#
# Phase 1 grows CORE_PORTED_SOURCES leaf-first: each file is added only once it COMPILES
# against rtlcompat (+ platform later). Keep this list = the set that actually builds, so the
# library always links. STATUS.md mirrors this list.

set(CORE_DIR ${CMAKE_SOURCE_DIR}/../source/core)

set(CORE_PORTED_SOURCES
  ${CORE_DIR}/Global.cpp
)

add_library(winscpcore STATIC ${CORE_PORTED_SOURCES})
target_link_libraries(winscpcore PUBLIC rtlcompat)
target_include_directories(winscpcore PUBLIC ${CORE_DIR})
# bcc64 pragmas (hdrstop, package) are no-ops on clang; silence the noise.
target_compile_options(winscpcore PRIVATE -Wno-unknown-pragmas)
