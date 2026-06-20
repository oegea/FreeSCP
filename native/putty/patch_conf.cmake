# Patch conf.winscp.h's MSVC-only `(int)""` (string-literal-to-int, not a compile-time constant
# in clang) to `(int)0` (an equivalent empty default). Shell-independent; invoked with -P.
file(READ "${SRC}" _content)
# Any `(int)"...string..."` STR default -> `(int)0`. ((int)true/(int)false are real constants.)
string(REGEX REPLACE "\\(int\\)\"[^\"]*\"" "(int)0" _content "${_content}")
file(WRITE "${DST}" "${_content}")
