# expat — XML parser, neon's WebDAV XML backend. Built natively as a static lib (Phase 4).
# Vendored libs/expat already ships a working expat_config.h (BYTEORDER/HAVE_* are correct for
# macOS/Linux). Only the 3 core TUs compile separately: xmltok_impl.c and xmltok_ns.c are
# #included by xmltok.c, and loadlibrary.c is Windows-only.

set(EXPAT_DIR ${CMAKE_SOURCE_DIR}/../libs/expat)

add_library(expat STATIC
  ${EXPAT_DIR}/lib/xmlparse.c
  ${EXPAT_DIR}/lib/xmlrole.c
  ${EXPAT_DIR}/lib/xmltok.c)
target_compile_definitions(expat PRIVATE HAVE_EXPAT_CONFIG_H XML_STATIC)
target_include_directories(expat PUBLIC ${EXPAT_DIR} ${EXPAT_DIR}/lib)
target_compile_options(expat PRIVATE -w -Wno-everything)
