/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The language of Haiku's hey (src/bin/hey.cpp, commit b1d497bb69), for the
// tool ngui-hey (specification 20.7): a command as words, made into a
// scripting message, and a reply as text.
//
//   <verb> <specifier> [of <specifier>]* [to <value>] [with name=<value>
//     [and name=<value>]*]
//   <verb>      do | get | set | count | create | delete | getsuites
//   <specifier> [the] <property> [ <index> | -<index> | [<index>] | [-<index>]
//                 | [<index> to <end>] | <name> | "<name>" ]
//   <value>     "string" | integer | float | true | false | int8(v)
//                 | int16(v) | int32(v) | int64(v) | bool(v) | float(v)
//                 | double(v) | BPoint(x,y) | BRect(x0,y0,x1,y1)
//                 | rgb_color(r,g,b,a)
//
// Differences to hey: only scripting verbs (remote control takes nothing
// else); no "let … do" (it needs a handler's messenger) and no file();
// BRect is the half-open rectangle of NewGUI, rgb_color becomes a color
// field; an "of" at the end is an error.


#include <expected>
#include <span>
#include <string>

#include <ngui/Error.h>
#include <ngui/Message.h>


namespace ngui::remote::detail {


// The scripting message of the command \a words (after the application);
// BadScriptSyntax with what is wrong, also for a value its message cannot
// take (a field named twice with two types).
std::expected<BMessage, Error> ParseHeyCommand(std::span<const std::string> words);

// A value as hey's add_data() adds it, to \a message under "data" or the
// name before "=".
std::expected<void, Error> AddHeyValue(BMessage& message, const std::string& value);

// \a reply as hey prints it: "Reply BMessage(B_REPLY):" and a line per item;
// property tables of kGetSupportedSuites as a table.
std::string DescribeReply(const BMessage& reply);

// The items of "result", one per line (hey -o).
std::string ResultText(const BMessage& reply);


} // namespace ngui::remote::detail
