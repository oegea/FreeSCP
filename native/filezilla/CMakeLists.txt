# FileZilla FTP backend — built natively via the MFC/Winsock compat shim (native/filezilla/compat).
# Compiled with the engine flags (-fshort-wchar via the global option + -fms-extensions + WcsCompat
# force-include) so its 2-byte-wchar CString interops with the engine's UnicodeString. The compat
# headers go FIRST on the include path so <afx.h>/<winsock2.h>/<atlconv.h> resolve to our shims.

set(FZ_DIR     ${CMAKE_SOURCE_DIR}/../source/filezilla)
set(GEN_DIR    ${CMAKE_BINARY_DIR}/geninclude/core)
set(CORE_DIR   ${CMAKE_SOURCE_DIR}/../source/core)
set(RTL_DIR    ${CMAKE_SOURCE_DIR}/rtlcompat/include)
set(COMPAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/compat)

set(FZ_SOURCES
  ${FZ_DIR}/structures.cpp
  ${FZ_DIR}/ServerPath.cpp
  ${FZ_DIR}/FtpListResult.cpp
  ${FZ_DIR}/FzApiStructures.cpp
  ${FZ_DIR}/ApiLog.cpp
  ${FZ_DIR}/FileZillaIntern.cpp
  ${FZ_DIR}/FileZillaApi.cpp
  ${FZ_DIR}/FileZillaIntf.cpp
  ${FZ_DIR}/MainThread.cpp
  ${FZ_DIR}/MFC64bitFix.cpp
  ${FZ_DIR}/TransferSocket.cpp
  ${FZ_DIR}/AsyncSocketEx.cpp
  ${FZ_DIR}/AsyncSocketExLayer.cpp
  ${FZ_DIR}/AsyncProxySocketLayer.cpp
  ${FZ_DIR}/AsyncSslSocketLayer.cpp
  ${FZ_DIR}/FtpControlSocket.cpp
  # compat runtime
  ${COMPAT_DIR}/afx_format.cpp
  ${COMPAT_DIR}/afx_statics.cpp
  ${COMPAT_DIR}/winmsg.cpp)

add_library(winscpfilezilla STATIC ${FZ_SOURCES})
add_dependencies(winscpfilezilla winscpcore_genheaders)
target_link_libraries(winscpfilezilla PUBLIC rtlcompat)
target_compile_definitions(winscpfilezilla PRIVATE
  MPEXT MPEXT_NO_GSS MPEXT_NO_ZLIB WINSCP NO_GSSAPI)
target_compile_options(winscpfilezilla PRIVATE -fms-extensions -Wno-everything
  -include ${RTL_DIR}/winscp/WcsCompat.h)
# compat shims FIRST, then filezilla, then the genprops engine headers + core + rtlcompat + deps.
target_include_directories(winscpfilezilla PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/casecompat   # exact-case shims (Ws2tcpip.h, FileZillaTools.h) — Linux is case-sensitive
  ${COMPAT_DIR}
  ${FZ_DIR}
  ${GEN_DIR}
  ${CORE_DIR}
  ${RTL_DIR}
  ${RTL_DIR}/winapi
  ${CMAKE_SOURCE_DIR}/../source/resource
  ${CMAKE_SOURCE_DIR}/../libs/openssl/include)
