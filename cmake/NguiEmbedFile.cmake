# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

# Writes a C++ source that holds a file as a byte array (script mode):
#   cmake -DINPUT=<file> -DOUTPUT=<source> -DSYMBOL=<name> -P NguiEmbedFile.cmake
# The source defines ngui::embedded::<name> and <name>Size. It names the file
# only by its name and holds no time stamp, so builds stay reproducible.

file(READ "${INPUT}" content HEX)
string(LENGTH "${content}" hexLength)
math(EXPR size "${hexLength} / 2")

# Each byte as "0x..,", then thirty-two of them per line: long lines are a
# compiler limit on some systems.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")
string(REPEAT "0x[0-9a-f][0-9a-f]," 32 thirtyTwo)
string(REGEX REPLACE "(${thirtyTwo})" "\t\\1\n" lines "${bytes}")
# The bytes after the last full line.
string(REGEX REPLACE "\n(0x[0-9a-fx,]+)$" "\n\t\\1\n" lines "${lines}")

get_filename_component(name "${INPUT}" NAME)
file(WRITE "${OUTPUT}" "// Generated from ${name} by cmake/NguiEmbedFile.cmake; do not edit.\n\n"
    "#include <cstddef>\n\n"
    "namespace ngui::embedded {\n\n"
    "extern const unsigned char ${SYMBOL}[];\n"
    "extern const std::size_t ${SYMBOL}Size;\n\n"
    "const unsigned char ${SYMBOL}[] = {\n${lines}};\n\n"
    "const std::size_t ${SYMBOL}Size = ${size};\n\n"
    "} // namespace ngui::embedded\n")
