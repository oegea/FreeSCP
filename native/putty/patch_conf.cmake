# Patch conf.winscp.h for clang/64-bit. Upstream initializes the ConfKeyInfo default_value union
# positionally via `(int)<value>` — which initializes the first union member (int ival) and, for
# STR defaults, truncates a 64-bit `const char *` to int. That only works when string literals sit
# in low memory (MSVC); on arm64 the address is high, so .sval reads garbage and load_open_settings
# crashes. Fix: rewrite each `(int)<value>` to a DESIGNATED union initializer for the correct member
# (.sval for strings, .bval for bools, .ival for everything else), which is portable C99.
file(READ "${SRC}" _content)
string(REGEX REPLACE "\\(int\\)(\"[^\"]*\")" "{ .sval = \\1 }" _content "${_content}")
string(REGEX REPLACE "\\(int\\)(true|false)" "{ .bval = \\1 }" _content "${_content}")
string(REGEX REPLACE "\\(int\\)(-?[A-Za-z0-9_]+)" "{ .ival = \\1 }" _content "${_content}")
file(WRITE "${DST}" "${_content}")
