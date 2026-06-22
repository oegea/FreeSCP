# Qt 6 GUI. Dual-pane Commander; local panel backed by the ported engine via enginebridge.

find_package(Qt6 COMPONENTS Widgets QUIET)

if(NOT Qt6_FOUND)
  message(WARNING "Qt6 not found; skipping ui-qt target. brew install qt, then re-run cmake "
                  "with -DCMAKE_PREFIX_PATH=$(brew --prefix qt).")
  return()
endif()

set(CMAKE_AUTOMOC ON)

# enginebridge: the ONLY UI code compiled with engine flags (-fshort-wchar -fms-extensions via
# winscpcore_flags). Exposes a clean std-only header to the Qt code. It now pulls the SSH-side
# engine headers (Terminal/SecureShell -> PuTTY), so it needs the putty include set + WINSCP/
# NO_GSSAPI defines and the genprops shadow headers (like the winscpcore_ssh group).
add_library(enginebridge STATIC enginebridge.cpp)
add_dependencies(enginebridge winscpcore_genheaders)
target_link_libraries(enginebridge PRIVATE winscpcore_flags)
target_compile_definitions(enginebridge PRIVATE WINSCP NO_GSSAPI)
target_include_directories(enginebridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(enginebridge PRIVATE
  ${CMAKE_SOURCE_DIR}/putty/include ${CMAKE_SOURCE_DIR}/../source/putty)

find_package(OpenSSL REQUIRED)

# The Qt app — normal flags (QString/4-byte wchar_t); never sees engine/rtlcompat headers. It
# whole-archive links the engine (global ctors + cross-refs), the headless Interface stubs, and
# the PuTTY libs — same set as the winscp-harness.
set(WINSCP_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/winscp.icns)
set(CMAKE_AUTORCC ON)
add_executable(winscp-qt MACOSX_BUNDLE main.cpp resources.qrc ${WINSCP_ICNS})
if(APPLE)
  set_source_files_properties(${WINSCP_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(winscp-qt PROPERTIES
    MACOSX_BUNDLE_ICON_FILE winscp.icns
    MACOSX_BUNDLE_BUNDLE_NAME "FreeSCP"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.freescp.app")
endif()
# Whole-archive the engine archives (macOS -force_load / GNU-lld --whole-archive). macOS unchanged.
foreach(lib winscpcore winscp_harness puttycore puttyplatform puttycrypto_vs)
  if(APPLE)
    target_link_options(winscp-qt PRIVATE -Wl,-force_load,$<TARGET_FILE:${lib}>)
  else()
    target_link_options(winscp-qt PRIVATE "SHELL:-Wl,--whole-archive $<TARGET_FILE:${lib}> -Wl,--no-whole-archive")
  endif()
endforeach()
target_link_libraries(winscp-qt PRIVATE
  Qt6::Widgets enginebridge
  winscpcore winscp_harness puttycore puttyplatform puttycrypto_vs rtlcompat
  neon expat libs3                        # Phase 4: WebDAV + S3 deps, on-demand
  winscpfilezilla                         # Phase 8: FTP backend
  OpenSSL::SSL OpenSSL::Crypto)
