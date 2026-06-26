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

# Drop x86-only hardware crypto on every native target, and the one MSVC-ism. We use software
# AES everywhere (HAVE_AES_NI is 0 in defs.h on both arm64 and x86 — see the note there: the
# native AES-NI build garbles decryption), so aes-select never references the *_ni symbols and
# the *-ni.c files are unused. Excluding them keeps the build clean and avoids the dead code.
foreach(f ${PUTTY_SRC})
  get_filename_component(n ${f} NAME)
  if(n MATCHES "(aes-ni|sha-ni|sha1-ni|sha256-ni)\\.c$" OR n STREQUAL "conf_data.c")
    list(REMOVE_ITEM PUTTY_SRC ${f})
  endif()
endforeach()

# conf_data.c instantiates CONF_OPTION(...) data from conf.winscp.h, which uses MSVC's `(int)""`
# (a string literal cast to int — not a compile-time constant in clang). Generate a patched
# shadow of both into build/putty-gen (`(int)""` -> `(int)0`, an equivalent empty default) and
# compile that copy; its quoted `#include "conf.winscp.h"` resolves to the patched sibling.
set(PUTTY_GEN ${CMAKE_BINARY_DIR}/putty-gen)
add_custom_command(
  OUTPUT ${PUTTY_GEN}/conf.winscp.h ${PUTTY_GEN}/conf_data.c
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PUTTY_GEN}
  COMMAND ${CMAKE_COMMAND} -DSRC=${PUTTY_DIR}/conf.winscp.h -DDST=${PUTTY_GEN}/conf.winscp.h -P ${CMAKE_CURRENT_SOURCE_DIR}/patch_conf.cmake
  COMMAND ${CMAKE_COMMAND} -E copy ${PUTTY_DIR}/utils/conf_data.c ${PUTTY_GEN}/conf_data.c
  DEPENDS ${PUTTY_DIR}/conf.winscp.h ${PUTTY_DIR}/utils/conf_data.c ${CMAKE_CURRENT_SOURCE_DIR}/patch_conf.cmake
  COMMENT "patch conf.winscp.h (int)\"\" -> (int)0")
list(APPEND PUTTY_SRC ${PUTTY_GEN}/conf_data.c)

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

# Software crypto bodies. WinSCP gates the C reference AES/SHA implementations behind WINSCP_VS
# (a crypto-only "VS" sub-build); without it the main putty build only DECLARES aes_sliced_key_
# setup / sha256_sw_block etc, leaving them undefined at link. We compile just those files a
# second time WITH -DWINSCP_VS to supply the bodies — they define a disjoint symbol set from the
# non-VS copies in puttycore (verified: no overlap), so both coexist.
set(PUTTY_CRYPTO_VS_SRC
  ${PUTTY_DIR}/crypto/aes-sw.c
  ${PUTTY_DIR}/crypto/sha256-sw.c
  ${PUTTY_DIR}/crypto/argon2.c)
add_library(puttycrypto_vs STATIC ${PUTTY_CRYPTO_VS_SRC})
target_compile_definitions(puttycrypto_vs PRIVATE WINSCP MPEXT NO_GSSAPI WINSCP_VS NDEBUG)
target_include_directories(puttycrypto_vs PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include ${PUTTY_DIR} ${LIBS_DIR}/openssl/include)
target_compile_options(puttycrypto_vs PRIVATE -include platform.h -std=gnu11 -w -Wno-everything -fexceptions)
set_target_properties(puttycrypto_vs PROPERTIES C_STANDARD 11)
