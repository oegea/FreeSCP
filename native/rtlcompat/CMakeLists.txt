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
target_compile_options(rtlcompat PUBLIC -fshort-wchar -fms-extensions)

add_executable(test_rtlcompat test/test_rtlcompat.cpp)
target_link_libraries(test_rtlcompat PRIVATE rtlcompat)
add_test(NAME rtlcompat COMMAND test_rtlcompat)

add_executable(test_format test/test_format.cpp)
target_link_libraries(test_format PRIVATE rtlcompat)
add_test(NAME format COMMAND test_format)
