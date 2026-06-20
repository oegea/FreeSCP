# Headless engine harness: the GUI/Interface boundary stubs (interface_stub.cpp) compiled with
# the engine flags + PuTTY include set, so a non-interactive SFTP session links + runs. Grows a
# main + a Seat that answers prompts from supplied credentials.
set(GEN_DIR ${CMAKE_BINARY_DIR}/geninclude/core)

add_library(winscp_harness STATIC
  interface_stub.cpp)
add_dependencies(winscp_harness winscpcore_genheaders)
target_link_libraries(winscp_harness PUBLIC winscpcore_flags)
target_compile_definitions(winscp_harness PRIVATE WINSCP NO_GSSAPI)
target_include_directories(winscp_harness PRIVATE
  ${CMAKE_SOURCE_DIR}/putty/include
  ${CMAKE_SOURCE_DIR}/../source/putty)
