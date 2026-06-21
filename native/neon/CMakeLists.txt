# neon 0.34.2 (WinSCP fork) — WebDAV HTTP/S client, built natively as a static lib (Phase 4).
#
# The WinSCP fork's libs/neon/src/config.h is Windows-only (everything under `#ifdef WIN32`); a
# guarded `#else` there pulls native/neon/neon_config_unix.h (captured from autotools configure —
# see docs/porting/STATUS.md "Phase 4 neon build"). We build the 26 portable src/*.c with -DWINSCP
# (the engine + the fork's NeonIntf expect WinSCP integration hooks) and -DHAVE_CONFIG_H. The few
# WinSCP branches that assumed Windows (ne_openssl <windows.h>, ne_socket ioctlsocket/FIONBIO) are
# now guarded with _WIN32 (see UPSTREAM-PATCHES.md), so the unix portable paths compile.

set(NEON_DIR ${CMAKE_SOURCE_DIR}/../libs/neon/src)
set(LIBS_DIR ${CMAKE_SOURCE_DIR}/../libs)

find_package(OpenSSL REQUIRED)
file(GLOB NEON_SRC ${NEON_DIR}/*.c)

add_library(neon STATIC ${NEON_SRC})
target_compile_definitions(neon PRIVATE HAVE_CONFIG_H WINSCP)
target_include_directories(neon PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}        # neon_config_unix.h (pulled by libs/neon/src/config.h on unix)
  ${NEON_DIR}
  ${LIBS_DIR}/expat/lib)             # expat.h (XML parser)
target_link_libraries(neon PUBLIC OpenSSL::SSL OpenSSL::Crypto)
target_compile_options(neon PRIVATE -w -Wno-everything)
