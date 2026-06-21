cmake_minimum_required(VERSION 3.20)
project(winscp_native LANGUAGES C CXX)

# WinSCP native port (macOS first, Linux next). See ../CLAUDE.md and ../docs/porting/PLAN.md.
# This tree NEVER builds the upstream Windows projects; it builds the ported engine + Qt UI.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
  add_compile_definitions(WINSCP_PLATFORM_MAC=1 WINSCP_PLATFORM_POSIX=1)
elseif(UNIX)
  add_compile_definitions(WINSCP_PLATFORM_LINUX=1 WINSCP_PLATFORM_POSIX=1)
endif()

enable_testing()

# Phase 1 keystone: Embarcadero RTL compatibility layer.
add_subdirectory(rtlcompat)

add_subdirectory(putty)        # Phase 3: PuTTY core (SSH/SFTP), platform-independent C
add_subdirectory(expat)        # Phase 4: expat XML parser (neon's WebDAV XML backend)
add_subdirectory(neon)         # Phase 4: neon WebDAV HTTP/S client (static libneon)
add_subdirectory(libs3)        # Phase 4: S3 client (static libs3; neon-based, built as C++)
add_subdirectory(core)         # Phase 1+ ported engine (libwinscpcore), grows leaf-first
add_subdirectory(filezilla)    # Phase 8: FTP backend (FileZilla) via MFC/Winsock compat shim
add_subdirectory(harness)      # Phase 3: headless GUI/Interface stubs to link the engine
add_subdirectory(ui-qt)        # Phase 0 skeleton -> Phase 6 real GUI

# Later phases (guarded until their groundwork exists):
#   add_subdirectory(platform)   # Phase 2
