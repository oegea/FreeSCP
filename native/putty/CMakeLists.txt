# PuTTY 0.85 (WinSCP fork) — platform-independent core compiled natively (Phase 3).
#
# Supplies a Unix platform.h (native/putty/include) INSTEAD of source/putty/windows/platform.h
# (searched first), so the portable C (crypto/ssh/utils/proxy) builds on clang. The actual
# platform BACKEND (BSD sockets / select / noise / time / storage) is native/putty/src and is
# what makes it link + connect — added incrementally next.
#
# Excluded: source/putty/windows/* (Windows backend) and x86-only hardware crypto (*-ni).
# (Up to PuTTY 0.83 WinSCP kept the software AES/SHA/Argon2 bodies in a separate WINSCP_VS
# sub-build and a conf.winscp.h needing a patched shadow; 0.85 reset both to stock PuTTY.)

set(PUTTY_DIR ${CMAKE_SOURCE_DIR}/../source/putty)
set(LIBS_DIR ${CMAKE_SOURCE_DIR}/../libs)

file(GLOB PUTTY_SRC
  ${PUTTY_DIR}/*.c
  ${PUTTY_DIR}/utils/*.c
  ${PUTTY_DIR}/crypto/*.c
  ${PUTTY_DIR}/ssh/*.c
  ${PUTTY_DIR}/proxy/*.c
  ${PUTTY_DIR}/stubs/*.c)

# Drop x86-only hardware crypto on every native target. We use software
# AES everywhere (HAVE_AES_NI is 0 in defs.h on both arm64 and x86 — see the note there: the
# native AES-NI build garbles decryption), so aes-select never references the *_ni symbols and
# the *-ni.c files are unused. Excluding them keeps the build clean and avoids the dead code.
foreach(f ${PUTTY_SRC})
  get_filename_component(n ${f} NAME)
  if(n MATCHES "(aes-ni|sha-ni|sha1-ni|sha256-ni)\\.c$")
    list(REMOVE_ITEM PUTTY_SRC ${f})
  endif()
endforeach()

add_library(puttycore STATIC ${PUTTY_SRC})
target_compile_definitions(puttycore PRIVATE WINSCP MPEXT NO_GSSAPI NDEBUG)
target_include_directories(puttycore PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include    # unix platform.h (must precede source/putty/windows)
  ${PUTTY_DIR}
  ${LIBS_DIR}/openssl/include)
# Force the unix platform.h first in every TU (some putty files include puttyexp.h before any
# header that pulls platform.h).
target_compile_options(puttycore PRIVATE
  -include platform.h -std=gnu11 -w -Wno-everything -fexceptions)
set_target_properties(puttycore PROPERTIES C_STANDARD 11)

# Unix/macOS platform BACKEND for putty (network/select/noise/time/storage). Grows until
# puttycore links + connects. Built with the same flags as puttycore.
file(GLOB PUTTY_PLATFORM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
# Portable PuTTY utils that upstream files under windows/ but are pure C (session-name <-> key
# escaping; WinSCP's HierarchicalStorage calls them for stored-key-name consistency).
list(APPEND PUTTY_PLATFORM_SRC ${PUTTY_DIR}/windows/utils/escape_registry_key.c)
add_library(puttyplatform STATIC ${PUTTY_PLATFORM_SRC})
target_compile_definitions(puttyplatform PRIVATE WINSCP MPEXT NO_GSSAPI NDEBUG)
target_include_directories(puttyplatform PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include ${PUTTY_DIR} ${LIBS_DIR}/openssl/include)
target_compile_options(puttyplatform PRIVATE -include platform.h -std=gnu11 -w -Wno-everything -fexceptions)
