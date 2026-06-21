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

# Headless connect harness executable. The engine archives must be whole-archive linked
# (-force_load): they carry global constructors and cross-references that on-demand archive
# extraction would drop. OpenSSL is a normal dynamic dep.
find_package(OpenSSL REQUIRED)
add_executable(winscp-harness main.cpp)
add_dependencies(winscp-harness winscpcore_genheaders)
target_link_libraries(winscp-harness PRIVATE winscpcore_flags)
target_compile_definitions(winscp-harness PRIVATE WINSCP NO_GSSAPI)
target_include_directories(winscp-harness PRIVATE
  ${CMAKE_SOURCE_DIR}/putty/include
  ${CMAKE_SOURCE_DIR}/../source/putty)
# Whole-archive the engine archives (global ctors + cross-refs). macOS ld64 uses -force_load;
# GNU/lld (Linux) uses --whole-archive/--no-whole-archive. macOS path is unchanged.
foreach(lib winscpcore winscp_harness puttycore puttyplatform puttycrypto_vs)
  if(APPLE)
    target_link_options(winscp-harness PRIVATE -Wl,-force_load,$<TARGET_FILE:${lib}>)
  else()
    target_link_options(winscp-harness PRIVATE "SHELL:-Wl,--whole-archive $<TARGET_FILE:${lib}> -Wl,--no-whole-archive")
  endif()
endforeach()
target_link_libraries(winscp-harness PRIVATE
  winscpcore winscp_harness puttycore puttyplatform puttycrypto_vs rtlcompat
  neon expat libs3                        # Phase 4: WebDAV + S3 deps, on-demand
  winscpfilezilla                         # Phase 8: FTP backend
  OpenSSL::SSL OpenSSL::Crypto)
