# Embarcadero RTL compatibility layer (Phase 1 keystone).
# Built with -fshort-wchar so wchar_t == 2 bytes and L"" literals are UTF-16 (Delphi/Windows
# semantics), and -fms-extensions for __declspec(property). Consumers inherit both PUBLICly.

add_library(rtlcompat STATIC
  src/SysUtils.cpp
  src/Streams.cpp
  src/SysStrFuncs.cpp
  src/Containers.cpp
  src/StringConv.cpp
  src/StrUtils.cpp
  src/Format.cpp
  src/SysExtra.cpp
  src/WinTime.cpp
  src/Threading.cpp
  src/IniFiles.cpp
  src/WinSock.cpp
  src/XmlDoc.cpp
  src/Masks.cpp
  src/ResStrings.cpp
  src/WcsCompat.cpp
  src/JsonDoc.cpp
)
target_include_directories(rtlcompat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(rtlcompat PUBLIC cxx_std_17)
# PUBLIC so the UTF-16 ABI + property syntax are consistent across the engine + UI.
# -fno-builtin-wcslen: with -fshort-wchar the compiler assumes the C library's wcslen also works on
# 2-byte characters and (LLVM 20+, e.g. Xcode 26) turns any hand-written `while (*p) ++p` loop over
# wchar_t into a call to it - including our own winscp_wcslen. The system wcslen reads 4-byte units:
# wrong lengths and reads past the end of the string (found by AddressSanitizer).
target_compile_options(rtlcompat PUBLIC -fshort-wchar -fno-builtin-wcslen -fms-extensions
  # Must accompany -fshort-wchar everywhere: stops libc++ sending 2-byte strings to libc's 4-byte
  # wmemchr/wcslen/wmemcmp (see ShortWchar.h). PUBLIC so every dependent TU gets both together.
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/include/winscp/ShortWchar.h")

add_executable(test_rtlcompat test/test_rtlcompat.cpp)
target_link_libraries(test_rtlcompat PRIVATE rtlcompat)
add_test(NAME rtlcompat COMMAND test_rtlcompat)

add_executable(test_format test/test_format.cpp)
target_link_libraries(test_format PRIVATE rtlcompat)
add_test(NAME format COMMAND test_format)

add_executable(test_wintime test/test_wintime.cpp)
target_link_libraries(test_wintime PRIVATE rtlcompat)
add_test(NAME wintime COMMAND test_wintime)
