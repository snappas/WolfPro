# Embeds a binary file as a C byte-array header. Invoked in script mode:
#   cmake -DINPUT=<path> -DOUTPUT=<path> -DVARNAME=<identifier> -P EmbedFont.cmake
if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED VARNAME)
	message(FATAL_ERROR "EmbedFont.cmake requires -DINPUT=, -DOUTPUT=, -DVARNAME=")
endif()

file(READ "${INPUT}" TTF_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," TTF_ARRAY "${TTF_HEX}")
string(LENGTH "${TTF_HEX}" TTF_HEX_LEN)
math(EXPR TTF_SIZE "${TTF_HEX_LEN} / 2")

# Derive the header guard from VARNAME to avoid collisions if used multiple times
string(TOUPPER "${VARNAME}" HEADER_GUARD_BASE)
string(APPEND HEADER_GUARD_BASE "_H")

file(WRITE "${OUTPUT}"
"// Generated from ${INPUT} by cmake/EmbedFont.cmake -- do not edit.\n"
"#ifndef ${HEADER_GUARD_BASE}\n"
"#define ${HEADER_GUARD_BASE}\n"
"static const unsigned char ${VARNAME}[] = { ${TTF_ARRAY} };\n"
"#endif\n"
)

message(STATUS "EmbedFont.cmake: wrote ${OUTPUT} (${TTF_SIZE} bytes)")
