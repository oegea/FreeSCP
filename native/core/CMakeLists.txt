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
add_library(winscpcore_flags INTERFACE)
target_link_libraries(winscpcore_flags INTERFACE rtlcompat)
target_include_directories(winscpcore_flags INTERFACE ${GEN_DIR} ${CORE_DIR})
target_compile_options(winscpcore_flags INTERFACE -fms-extensions -Wno-unknown-pragmas)

# --- step 2: ported engine objects ---
set(CORE_PORTED_SOURCES
  ${CORE_DIR}/Global.cpp
)
add_library(winscpcore STATIC ${CORE_PORTED_SOURCES})
add_dependencies(winscpcore winscpcore_genheaders)
target_link_libraries(winscpcore PUBLIC winscpcore_flags)

# --- header-parse regression guard: each listed header must compile standalone ---
# (proves the RTL surface + __property codegen keep covering them as rtlcompat grows)
set(CORE_PARSE_HEADERS
  Global.h Common.h NamedObjs.h Http.h Option.h Security.h KeyGen.h
  Exceptions.h FileInfo.h RemoteFiles.h
  Cryptography.h FileBuffer.h FileMasks.h HierarchicalStorage.h Usage.h
)
set(PARSE_STUBS "")
foreach(h ${CORE_PARSE_HEADERS})
  set(stub ${CMAKE_CURRENT_BINARY_DIR}/parse_${h}.cpp)
  file(GENERATE OUTPUT ${stub} CONTENT "#include <vcl.h>\n#include \"${h}\"\n")
  list(APPEND PARSE_STUBS ${stub})
endforeach()
add_library(winscpcore_parsecheck OBJECT ${PARSE_STUBS})
add_dependencies(winscpcore_parsecheck winscpcore_genheaders)
target_link_libraries(winscpcore_parsecheck PRIVATE winscpcore_flags)
