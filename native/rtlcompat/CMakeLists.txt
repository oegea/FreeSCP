# Embarcadero RTL compatibility layer (Phase 1 keystone).
# Built with -fshort-wchar so wchar_t == 2 bytes and L"" literals are UTF-16 (Delphi/Windows
# semantics). Any target that consumes the ported engine MUST inherit these flags — they
# propagate via target_compile_options(PUBLIC).

add_library(rtlcompat INTERFACE)
target_include_directories(rtlcompat INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(rtlcompat INTERFACE cxx_std_17)
# PUBLIC/INTERFACE so the UTF-16 ABI is consistent across the engine + UI.
target_compile_options(rtlcompat INTERFACE -fshort-wchar)

add_executable(test_rtlcompat test/test_rtlcompat.cpp)
target_link_libraries(test_rtlcompat PRIVATE rtlcompat)
add_test(NAME rtlcompat COMMAND test_rtlcompat)
