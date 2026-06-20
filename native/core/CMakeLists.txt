# Ported WinSCP engine (source/core) -> libwinscpcore.
#
# Two-step build:
#  1) genprops.py rewrites Embarcadero __property -> clang __declspec(property) in shadow
#     copies of every core header (build/geninclude/core), which are searched BEFORE the
#     originals. Lets us port headers without editing source/.
#  2) ported .cpp files (CORE_PORTED_SOURCES) compile against rtlcompat + the shadow headers.
#
# CORE_PORTED_SOURCES grows leaf-first; CORE_PARSE_HEADERS lists headers proven to parse
# (regression guard via the header-parse test).

set(CORE_DIR ${CMAKE_SOURCE_DIR}/../source/core)
set(GEN_DIR  ${CMAKE_BINARY_DIR}/geninclude/core)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# --- step 1: generate __property-rewritten shadow headers for all core headers ---
file(GLOB CORE_HEADERS ${CORE_DIR}/*.h)
set(GEN_HEADERS "")
foreach(h ${CORE_HEADERS})
  get_filename_component(name ${h} NAME)
  add_custom_command(
    OUTPUT ${GEN_DIR}/${name}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/genprops.py ${h} ${GEN_DIR}/${name}
    DEPENDS ${h} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/genprops.py
    COMMENT "genprops ${name}")
  list(APPEND GEN_HEADERS ${GEN_DIR}/${name})
endforeach()
add_custom_target(winscpcore_genheaders DEPENDS ${GEN_HEADERS})

# Shared flags for anything compiling engine sources/headers.
set(LIBS_DIR ${CMAKE_SOURCE_DIR}/../libs)
add_library(winscpcore_flags INTERFACE)
target_link_libraries(winscpcore_flags INTERFACE rtlcompat)
target_include_directories(winscpcore_flags INTERFACE
  ${GEN_DIR} ${CORE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../rtlcompat/include/winapi  # Win32 header stubs (shlobj, ...)
  ${CMAKE_SOURCE_DIR}/../source/resource   # TextsCore.h, resource string IDs
  ${LIBS_DIR}/neon/src      # WebDAV (NeonIntf, WebDAVFileSystem)
  ${LIBS_DIR}/expat/lib     # XML (pulled in via neon)
  ${LIBS_DIR}/libs3/inc     # S3
  ${LIBS_DIR}/openssl/include)  # crypto
target_compile_options(winscpcore_flags INTERFACE -fms-extensions -Wno-unknown-pragmas)

# --- step 2: ported engine objects ---
# Each ported .cpp is run through genprops into GEN_DIR and compiled FROM there, so its quoted
# #includes ("Foo.h") resolve to the __property-rewritten shadow header in the same dir
# (a .cpp in source/core would otherwise pick up its raw sibling header). Grows leaf-first.
set(CORE_PORTED_NAMES
  Global.cpp
  FileBuffer.cpp
  NamedObjs.cpp
  Common.cpp
  FileSystems.cpp
  Option.cpp
  Usage.cpp
  FileMasks.cpp
  Bookmarks.cpp
  FileInfo.cpp
  CopyParam.cpp
  FileOperationProgress.cpp
  RemoteFiles.cpp
  Exceptions.cpp
  Queue.cpp
)
set(CORE_GEN_SOURCES "")
foreach(name ${CORE_PORTED_NAMES})
  add_custom_command(
    OUTPUT ${GEN_DIR}/${name}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/genprops.py ${CORE_DIR}/${name} ${GEN_DIR}/${name}
    DEPENDS ${CORE_DIR}/${name} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/genprops.py
    COMMENT "genprops ${name}")
  list(APPEND CORE_GEN_SOURCES ${GEN_DIR}/${name})
endforeach()

add_library(winscpcore STATIC ${CORE_GEN_SOURCES})
add_dependencies(winscpcore winscpcore_genheaders)
target_link_libraries(winscpcore PUBLIC winscpcore_flags)

# --- step 3: SSH-side engine objects (include PuttyIntf.h / putty.h) ---
# These TUs need the PuTTY backend on the include path plus the WinSCP/MPEXT build defines
# (matching upstream's engine build). Same genprops shadow pipeline, extra flags. They compile
# into the SAME libwinscpcore but via an OBJECT lib so the putty include set + defines are
# scoped to them and don't perturb the 15 leaf TUs above.
# NOTE: ideally every engine TU would carry -DWINSCP -DMPEXT for ABI/ODR consistency; the leaf
# group predates this and links without it. Revisit unifying the defines before runtime trust.
set(CORE_SSH_NAMES
  PuttyIntf.cpp
  Cryptography.cpp
  Configuration.cpp
  HierarchicalStorage.cpp
  SecureShell.cpp
)
set(CORE_SSH_GEN_SOURCES "")
foreach(name ${CORE_SSH_NAMES})
  add_custom_command(
    OUTPUT ${GEN_DIR}/${name}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/genprops.py ${CORE_DIR}/${name} ${GEN_DIR}/${name}
    DEPENDS ${CORE_DIR}/${name} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/genprops.py
    COMMENT "genprops ${name}")
  list(APPEND CORE_SSH_GEN_SOURCES ${GEN_DIR}/${name})
endforeach()

add_library(winscpcore_ssh OBJECT ${CORE_SSH_GEN_SOURCES})
add_dependencies(winscpcore_ssh winscpcore_genheaders)
target_link_libraries(winscpcore_ssh PUBLIC winscpcore_flags)
target_compile_definitions(winscpcore_ssh PRIVATE WINSCP NO_GSSAPI)  # MPEXT comes from PuttyIntf.h
target_include_directories(winscpcore_ssh PRIVATE
  ${CMAKE_SOURCE_DIR}/putty/include      # unix PuTTY platform contract (precedes source/putty)
  ${CMAKE_SOURCE_DIR}/../source/putty)
target_sources(winscpcore PRIVATE $<TARGET_OBJECTS:winscpcore_ssh>)

# --- header-parse regression guard: each listed header must compile standalone ---
# (proves the RTL surface + __property codegen keep covering them as rtlcompat grows)
# Guard = every core header EXCEPT those that still need the putty backend (Phase 3:
# putty.h assumes the Windows platform layer). 35/36 currently.
set(CORE_PARSE_HEADERS "")
foreach(h ${CORE_HEADERS})
  get_filename_component(name ${h} NAME)
  if(NOT name STREQUAL "PuttyIntf.h")
    list(APPEND CORE_PARSE_HEADERS ${name})
  endif()
endforeach()
# Headers are written assuming Common.h is already included (as in every engine .cpp), so the
# guard mirrors that — include Common.h first (except for Common.h itself).
set(PARSE_STUBS "")
foreach(h ${CORE_PARSE_HEADERS})
  set(stub ${CMAKE_CURRENT_BINARY_DIR}/parse_${h}.cpp)
  if(h STREQUAL "Common.h")
    file(GENERATE OUTPUT ${stub} CONTENT "#include <vcl.h>\n#include \"${h}\"\n")
  else()
    file(GENERATE OUTPUT ${stub} CONTENT "#include <vcl.h>\n#include \"Common.h\"\n#include \"${h}\"\n")
  endif()
  list(APPEND PARSE_STUBS ${stub})
endforeach()
add_library(winscpcore_parsecheck OBJECT ${PARSE_STUBS})
add_dependencies(winscpcore_parsecheck winscpcore_genheaders)
target_link_libraries(winscpcore_parsecheck PRIVATE winscpcore_flags)
