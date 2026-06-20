# PuTTY 0.83 (WinSCP fork) — platform-independent core compiled natively (Phase 3).
#
# Supplies a Unix platform.h (native/putty/include) INSTEAD of source/putty/windows/platform.h
# (searched first), so the portable C (crypto/ssh/utils/proxy) builds on clang. The actual
# platform BACKEND (BSD sockets / select / noise / time / storage) is native/putty/src and is
# what makes it link + connect — added incrementally next.
#
# Excluded: source/putty/windows/* (Windows backend), x86-only hardware crypto (*-ni on arm64),
# and conf_data.c (one MSVC-only (int)"" constant-initializer — revisit).

set(PUTTY_DIR ${CMAKE_SOURCE_DIR}/../source/putty)
set(LIBS_DIR ${CMAKE_SOURCE_DIR}/../libs)

file(GLOB PUTTY_SRC
  ${PUTTY_DIR}/*.c
  ${PUTTY_DIR}/utils/*.c
  ${PUTTY_DIR}/crypto/*.c
  ${PUTTY_DIR}/ssh/*.c
  ${PUTTY_DIR}/proxy/*.c
  ${PUTTY_DIR}/stubs/*.c)

# Drop x86-only hardware crypto on non-x86, and the one MSVC-ism.
foreach(f ${PUTTY_SRC})
  get_filename_component(n ${f} NAME)
  if(n MATCHES "(aes-ni|sha-ni|sha1-ni|sha256-ni)\\.c$" OR n STREQUAL "conf_data.c")
    list(REMOVE_ITEM PUTTY_SRC ${f})
  endif()
endforeach()

add_library(puttycore STATIC ${PUTTY_SRC})
target_compile_definitions(puttycore PRIVATE WINSCP MPEXT NO_GSSAPI)
target_include_directories(puttycore PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include    # unix platform.h (must precede source/putty/windows)
  ${PUTTY_DIR}
  ${LIBS_DIR}/openssl/include)
# Force the unix platform.h first in every TU (some putty files include puttyexp.h before any
# header that pulls platform.h).
target_compile_options(puttycore PRIVATE
  -include platform.h -std=gnu11 -w -Wno-everything)
set_target_properties(puttycore PROPERTIES C_STANDARD 11)
