# libs3 (WinSCP fork) — S3 client, built natively as a static lib (Phase 4).
#
# The WinSCP fork is NEON-based (not curl) and uses expat for XML, and is compiled as C++
# (libs3.h uses bare struct-tag-as-type; request.c uses new[]). So the .c are compiled with
# LANGUAGE CXX. Windows-isms are guarded (request.c SYSTEMTIME/GetSystemTime, neon_read_func
# return type — see UPSTREAM-PATCHES). strnicmp -> strncasecmp; the C++ build needs <cstring>.
# Excludes s3.c (the CLI tool's main), testsimplexml.c (a test), and mingw_* (Windows).

set(LIBS3_DIR ${CMAKE_SOURCE_DIR}/../libs/libs3)
set(LIBS_DIR ${CMAKE_SOURCE_DIR}/../libs)
find_package(OpenSSL REQUIRED)

set(LIBS3_SRC
  ${LIBS3_DIR}/src/bucket.c
  ${LIBS3_DIR}/src/bucket_metadata.c
  ${LIBS3_DIR}/src/error_parser.c
  ${LIBS3_DIR}/src/general.c
  ${LIBS3_DIR}/src/multipart.c
  ${LIBS3_DIR}/src/object.c
  ${LIBS3_DIR}/src/request.c
  ${LIBS3_DIR}/src/request_context.c
  ${LIBS3_DIR}/src/response_headers_handler.c
  ${LIBS3_DIR}/src/service.c
  ${LIBS3_DIR}/src/service_access_logging.c
  ${LIBS3_DIR}/src/simplexml.c
  ${LIBS3_DIR}/src/util.c)
set_source_files_properties(${LIBS3_SRC} PROPERTIES LANGUAGE CXX)

add_library(libs3 STATIC ${LIBS3_SRC})
target_compile_definitions(libs3 PRIVATE WINSCP strnicmp=strncasecmp)
target_include_directories(libs3 PUBLIC
  ${LIBS3_DIR}/inc
  ${LIBS_DIR}/neon/src
  ${CMAKE_SOURCE_DIR}/neon          # neon_config_unix.h
  ${LIBS_DIR}/expat/lib)
target_link_libraries(libs3 PUBLIC neon expat OpenSSL::SSL OpenSSL::Crypto)
target_compile_options(libs3 PRIVATE -std=gnu++17 -include cstring -w -Wno-everything)
