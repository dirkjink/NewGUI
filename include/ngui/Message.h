/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <ngui/Error.h>
#include <ngui/Messenger.h>
#include <ngui/gfx/Color.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>
#include <ngui/gfx/Size.h>


namespace ngui {


struct MessageDelivery;
struct PlatformEventMark;


/*!	A four-character code, as Haiku writes 'LONG': FourCharCode("LONG"). */
constexpr std::uint32_t
FourCharCode(const char (&code)[5])
{
	return static_cast<std::uint32_t>(static_cast<unsigned char>(code[0])) << 24
		| static_cast<std::uint32_t>(static_cast<unsigned char>(code[1])) << 16
		| static_cast<std::uint32_t>(static_cast<unsigned char>(code[2])) << 8
		| static_cast<std::uint32_t>(static_cast<unsigned char>(code[3]));
}


/*!	The type of a message field (Haiku's type_code and B_*_TYPE). Any other
	value is an application's own type for AddData(). */
enum class BTypeCode : std::uint32_t {
	Any = FourCharCode("ANYT"),			/*!< B_ANY_TYPE: matches every type in queries. */
	Bool = FourCharCode("BOOL"),		/*!< B_BOOL_TYPE */
	Int8 = FourCharCode("BYTE"),		/*!< B_INT8_TYPE */
	UInt8 = FourCharCode("UBYT"),		/*!< B_UINT8_TYPE */
	Int16 = FourCharCode("SHRT"),		/*!< B_INT16_TYPE */
	UInt16 = FourCharCode("USHT"),		/*!< B_UINT16_TYPE */
	Int32 = FourCharCode("LONG"),		/*!< B_INT32_TYPE */
	UInt32 = FourCharCode("ULNG"),		/*!< B_UINT32_TYPE */
	Int64 = FourCharCode("LLNG"),		/*!< B_INT64_TYPE */
	UInt64 = FourCharCode("ULLG"),		/*!< B_UINT64_TYPE */
	Float = FourCharCode("FLOT"),		/*!< B_FLOAT_TYPE */
	Double = FourCharCode("DBLE"),		/*!< B_DOUBLE_TYPE */
	String = FourCharCode("CSTR"),		/*!< B_STRING_TYPE, UTF-8 */
	Point = FourCharCode("BPNT"),		/*!< B_POINT_TYPE, as gfx::Point */
	Rect = FourCharCode("RECT"),		/*!< B_RECT_TYPE, as gfx::Rect (half-open) */
	Size = FourCharCode("SIZE"),		/*!< B_SIZE_TYPE, as gfx::Size */
	Color = FourCharCode("RGBF"),		/*!< gfx::Color, four floats; Haiku's 'RGBC' is
											rgb_color */
	Pointer = FourCharCode("PNTR"),		/*!< B_POINTER_TYPE, not flattenable */
	Message = FourCharCode("MSGG"),		/*!< B_MESSAGE_TYPE */
	Mime = FourCharCode("MIME"),		/*!< B_MIME_TYPE: data named by its MIME type
											(clipboard, drag and drop) */
	Raw = FourCharCode("RAWT")			/*!< B_RAW_TYPE: bytes without a meaning */
};


/*!	\name Specifier forms
	The `what` of a specifier (scripting, specification 20.7): how a property
	is named. Forms above kSpecifiersEnd are the application's. */
/*!	\{ */
inline constexpr std::uint32_t kDirectSpecifier = 1;
									/*!< The property itself (B_DIRECT_SPECIFIER). */
inline constexpr std::uint32_t kIndexSpecifier = 2;
									/*!< An item by the int32 "index" (B_INDEX_SPECIFIER). */
inline constexpr std::uint32_t kReverseIndexSpecifier = 3;
									/*!< An item by "index" from the end
										(B_REVERSE_INDEX_SPECIFIER). */
inline constexpr std::uint32_t kRangeSpecifier = 4;
									/*!< Items "index" on, "range" of them
										(B_RANGE_SPECIFIER). */
inline constexpr std::uint32_t kReverseRangeSpecifier = 5;
									/*!< As kRangeSpecifier, from the end
										(B_REVERSE_RANGE_SPECIFIER). */
inline constexpr std::uint32_t kNameSpecifier = 6;
									/*!< An item by the string "name" (B_NAME_SPECIFIER). */
inline constexpr std::uint32_t kIdSpecifier = 7;
									/*!< An item by its int32 "id" (B_ID_SPECIFIER). */
inline constexpr std::uint32_t kSpecifiersEnd = 128;
									/*!< The last form of the toolkit (B_SPECIFIERS_END). */
/*!	\} */


struct BSpecifier;


/*!	What a field of a BMessage holds. */
struct BFieldInfo {
			std::string			name;	/*!< The field name. */
			BTypeCode			type;	/*!< The type of all its items. */
			std::int32_t		count;	/*!< The number of items, at least 1. */

			bool				operator==(const BFieldInfo& other) const = default;
									/*!< All three equal. */
};


/*!	A message (module G2): a command `what` and named fields, each a list of
	items of one type, as Haiku's BMessage. Messages travel between loopers
	(G3), and, flattened, to the clipboard, through drag and drop and to
	remote control (specification 20.7).

	Differences to Haiku:
	- Results are std::expected: NotFound for a missing field (B_NAME_NOT_FOUND),
	  OutOfRange for a missing index (B_BAD_INDEX), BadType for an item of
	  another type (B_BAD_TYPE), InvalidArgument for an empty name.
	- Strings are std::string_view in and out. Find results that are views
	  (FindString(), FindData()) stay valid until the message changes.
	- Geometry and color are gfx::Point, Rect (half-open), Size and Color.
	- AddData() takes only types without a typed method of their own; the
	  typed methods are the only way to the built-in types.
	- Flatten() writes a format of NewGUI's own, not Haiku's: versioned,
	  little-endian, canonical (equal messages give equal bytes), with the
	  fields in the order they were added. Pointer fields cannot be
	  flattened.
	- A copy of a message carries no delivery: it is not a reply and owes
	  none. To reply later, keep the delivered message itself
	  (BLooper::DetachCurrentMessage()).
	- The specifiers of scripting (specification 20.7) are the messages of the
	  field "specifiers"; the current one is not flattened: a message read
	  from Flatten() output starts at its last specifier, as a new one does.
*/
class BMessage {
public:
			std::uint32_t		what = 0;
									/*!< The command, e.g. FourCharCode("_QRQ"). */

								BMessage();
									/*!< An empty message with what 0. */
	explicit					BMessage(std::uint32_t command);
									/*!< An empty message with what = \a command. */

								BMessage(const BMessage& other);
									/*!< A copy of what and the fields, without delivery. */
								BMessage(BMessage&& other) noexcept;
									/*!< Takes everything, the delivery included. */
								~BMessage();
									/*!< A waiting sender of an unanswered message gets
										kNoReply. */
			BMessage&			operator=(const BMessage& other);
									/*!< Copies what and the fields; the delivery is
										dropped. */
			BMessage&			operator=(BMessage&& other) noexcept;
									/*!< Takes everything, the delivery included. */

			bool				operator==(const BMessage& other) const;
									/*!< Same what, same fields in the same order, equal
										items; floats compare by their bits, so that NaN
										equals itself. */

	// Drag and drop

			bool				WasDropped() const;
									/*!< It was dropped on a view (a file or text dragged
										from another application, module P2). */
			gfx::Point			DropPoint(gfx::Point* offset = nullptr) const;
									/*!< Where it was dropped, on the screen; (0, 0) if it
										was not. \a offset gets (0, 0): NewGUI drags no
										image of its own (Haiku: the point under the mouse
										in the dragged image). */

	// Delivery and replies

			bool				WasDelivered() const;
									/*!< It came through a looper's queue. */
			bool				IsSourceWaiting() const;
									/*!< Its sender waits for a reply that is not sent yet. */
			bool				IsReply() const;
									/*!< It is a reply to another message. */
			BMessenger			ReturnAddress() const;
									/*!< Where asynchronous replies go; invalid if nowhere. */
			std::expected<void, Error> SendReply(const BMessage& reply);
									/*!< Answers the sender, waiting or through its reply
										messenger. NotFound if there is nobody to answer,
										InvalidArgument if it was answered already
										(B_DUPLICATE_REPLY). */
			std::expected<void, Error> SendReply(std::uint32_t command);
									/*!< As above, with an empty reply of \a command. */

	// Specifiers (scripting, specification 20.7)

			std::expected<void, Error> AddSpecifier(std::string_view property);
									/*!< Adds a kDirectSpecifier for \a property; it becomes
										the current one. InvalidArgument for an empty name. */
			std::expected<void, Error> AddSpecifier(std::string_view property,
									std::int32_t index);
									/*!< Adds a kIndexSpecifier: item \a index of
										\a property. */
			std::expected<void, Error> AddSpecifier(std::string_view property,
									std::int32_t index, std::int32_t range);
									/*!< Adds a kRangeSpecifier: \a range items of
										\a property from \a index on; InvalidArgument for a
										negative range. */
			std::expected<void, Error> AddSpecifier(std::string_view property,
									std::string_view name);
									/*!< Adds a kNameSpecifier: the item \a name of
										\a property. */
			std::expected<void, Error> AddSpecifier(const BMessage& specifier);
									/*!< Adds \a specifier as it is, e.g. of a form of the
										application's; it becomes the current one. */
			std::expected<void, Error> SetCurrentSpecifier(std::int32_t index);
									/*!< Makes specifier \a index the current one;
										OutOfRange if there is none. */
			std::expected<BSpecifier, Error> GetCurrentSpecifier() const;
									/*!< The current specifier, for a handler resolving it.
										BadScriptSyntax if the message was not delivered,
										has no current specifier (all popped) or the
										specifier names no property. */
			std::int32_t		CurrentSpecifierIndex() const;
									/*!< The index of the current specifier; -1 if there is
										none (left to resolve). */
			bool				HasSpecifiers() const;
									/*!< Whether specifiers were added: a scripting message. */
			std::expected<void, Error> PopSpecifier();
									/*!< A handler has resolved the current specifier; the one
										before becomes current. InvalidArgument if the message
										was not delivered or none is left. */

	// Fields

			std::int32_t		CountNames(BTypeCode type = BTypeCode::Any) const;
									/*!< The number of fields of \a type. */
			bool				IsEmpty() const;
									/*!< No fields. */
			void				MakeEmpty();
									/*!< Removes all fields; what stays. */
			std::vector<BFieldInfo> Fields(BTypeCode type = BTypeCode::Any) const;
									/*!< The fields of \a type, in the order they were added. */
			std::expected<BFieldInfo, Error> GetInfo(std::string_view name) const;
									/*!< The field \a name. */
			bool				HasData(std::string_view name, BTypeCode type = BTypeCode::Any,
									std::int32_t index = 0) const;
									/*!< Whether field \a name has an item \a index of
										\a type. */
			std::expected<void, Error> RemoveName(std::string_view name);
									/*!< Removes field \a name with all its items. */
			std::expected<void, Error> RemoveData(std::string_view name,
									std::int32_t index = 0);
									/*!< Removes item \a index; the field goes with its last
										item. */
			std::expected<void, Error> Rename(std::string_view oldName,
									std::string_view newName);
									/*!< Renames a field; InvalidArgument if \a newName is
										empty or taken. */

	// Typed items

	// Bool

			std::expected<void, Error> AddBool(std::string_view name, bool value);
									/*!< Appends a bool to field \a name. */
			std::expected<bool, Error> FindBool(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			bool				GetBool(std::string_view name, bool defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceBool(std::string_view name, bool value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceBool(std::string_view name,
									std::int32_t index, bool value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasBool(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a bool at \a index. */

	// Int8

			std::expected<void, Error> AddInt8(std::string_view name, std::int8_t value);
									/*!< Appends an int8 to field \a name. */
			std::expected<std::int8_t, Error> FindInt8(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			std::int8_t			GetInt8(std::string_view name, std::int8_t defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceInt8(std::string_view name, std::int8_t value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceInt8(std::string_view name,
									std::int32_t index, std::int8_t value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasInt8(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds an int8 at \a index. */

	// UInt8

			std::expected<void, Error> AddUInt8(std::string_view name, std::uint8_t value);
									/*!< Appends a uint8 to field \a name. */
			std::expected<std::uint8_t, Error> FindUInt8(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			std::uint8_t		GetUInt8(std::string_view name, std::uint8_t defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceUInt8(std::string_view name, std::uint8_t value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceUInt8(std::string_view name,
									std::int32_t index, std::uint8_t value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasUInt8(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a uint8 at \a index. */

	// Int16

			std::expected<void, Error> AddInt16(std::string_view name, std::int16_t value);
									/*!< Appends an int16 to field \a name. */
			std::expected<std::int16_t, Error> FindInt16(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			std::int16_t		GetInt16(std::string_view name, std::int16_t defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceInt16(std::string_view name, std::int16_t value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceInt16(std::string_view name,
									std::int32_t index, std::int16_t value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasInt16(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds an int16 at \a index. */

	// UInt16

			std::expected<void, Error> AddUInt16(std::string_view name, std::uint16_t value);
									/*!< Appends a uint16 to field \a name. */
			std::expected<std::uint16_t, Error> FindUInt16(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			std::uint16_t		GetUInt16(std::string_view name, std::uint16_t defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceUInt16(std::string_view name, std::uint16_t value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceUInt16(std::string_view name,
									std::int32_t index, std::uint16_t value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasUInt16(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a uint16 at \a index. */

	// Int32

			std::expected<void, Error> AddInt32(std::string_view name, std::int32_t value);
									/*!< Appends an int32 to field \a name. */
			std::expected<std::int32_t, Error> FindInt32(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			std::int32_t		GetInt32(std::string_view name, std::int32_t defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceInt32(std::string_view name, std::int32_t value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceInt32(std::string_view name,
									std::int32_t index, std::int32_t value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasInt32(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds an int32 at \a index. */

	// UInt32

			std::expected<void, Error> AddUInt32(std::string_view name, std::uint32_t value);
									/*!< Appends a uint32 to field \a name. */
			std::expected<std::uint32_t, Error> FindUInt32(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			std::uint32_t		GetUInt32(std::string_view name, std::uint32_t defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceUInt32(std::string_view name, std::uint32_t value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceUInt32(std::string_view name,
									std::int32_t index, std::uint32_t value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasUInt32(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a uint32 at \a index. */

	// Int64

			std::expected<void, Error> AddInt64(std::string_view name, std::int64_t value);
									/*!< Appends an int64 to field \a name. */
			std::expected<std::int64_t, Error> FindInt64(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			std::int64_t		GetInt64(std::string_view name, std::int64_t defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceInt64(std::string_view name, std::int64_t value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceInt64(std::string_view name,
									std::int32_t index, std::int64_t value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasInt64(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds an int64 at \a index. */

	// UInt64

			std::expected<void, Error> AddUInt64(std::string_view name, std::uint64_t value);
									/*!< Appends a uint64 to field \a name. */
			std::expected<std::uint64_t, Error> FindUInt64(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			std::uint64_t		GetUInt64(std::string_view name, std::uint64_t defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceUInt64(std::string_view name, std::uint64_t value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceUInt64(std::string_view name,
									std::int32_t index, std::uint64_t value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasUInt64(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a uint64 at \a index. */

	// Float

			std::expected<void, Error> AddFloat(std::string_view name, float value);
									/*!< Appends a float to field \a name. */
			std::expected<float, Error> FindFloat(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			float				GetFloat(std::string_view name, float defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceFloat(std::string_view name, float value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceFloat(std::string_view name,
									std::int32_t index, float value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasFloat(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a float at \a index. */

	// Double

			std::expected<void, Error> AddDouble(std::string_view name, double value);
									/*!< Appends a double to field \a name. */
			std::expected<double, Error> FindDouble(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			double				GetDouble(std::string_view name, double defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceDouble(std::string_view name, double value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceDouble(std::string_view name,
									std::int32_t index, double value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasDouble(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a double at \a index. */

	// String

			std::expected<void, Error> AddString(std::string_view name, std::string_view value);
									/*!< Appends a string to field \a name. */
			std::expected<std::string_view, Error> FindString(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			std::string_view	GetString(std::string_view name,
									std::string_view defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceString(std::string_view name, std::string_view value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceString(std::string_view name,
									std::int32_t index, std::string_view value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasString(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a string at \a index. */

	// Point

			std::expected<void, Error> AddPoint(std::string_view name, const gfx::Point& value);
									/*!< Appends a point to field \a name. */
			std::expected<gfx::Point, Error> FindPoint(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			gfx::Point			GetPoint(std::string_view name,
									const gfx::Point& defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplacePoint(std::string_view name, const gfx::Point& value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplacePoint(std::string_view name,
									std::int32_t index, const gfx::Point& value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasPoint(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a point at \a index. */

	// Rect

			std::expected<void, Error> AddRect(std::string_view name, const gfx::Rect& value);
									/*!< Appends a rectangle to field \a name. */
			std::expected<gfx::Rect, Error> FindRect(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			gfx::Rect			GetRect(std::string_view name, const gfx::Rect& defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceRect(std::string_view name, const gfx::Rect& value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceRect(std::string_view name,
									std::int32_t index, const gfx::Rect& value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasRect(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a rectangle at \a index. */

	// Size

			std::expected<void, Error> AddSize(std::string_view name, const gfx::Size& value);
									/*!< Appends a size to field \a name. */
			std::expected<gfx::Size, Error> FindSize(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			gfx::Size			GetSize(std::string_view name, const gfx::Size& defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceSize(std::string_view name, const gfx::Size& value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceSize(std::string_view name,
									std::int32_t index, const gfx::Size& value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasSize(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a size at \a index. */

	// Color

			std::expected<void, Error> AddColor(std::string_view name, const gfx::Color& value);
									/*!< Appends a color to field \a name. */
			std::expected<gfx::Color, Error> FindColor(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			gfx::Color			GetColor(std::string_view name,
									const gfx::Color& defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplaceColor(std::string_view name, const gfx::Color& value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceColor(std::string_view name,
									std::int32_t index, const gfx::Color& value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasColor(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a color at \a index. */

	// Pointer

			std::expected<void, Error> AddPointer(std::string_view name, const void* value);
									/*!< Appends a pointer to field \a name. */
			std::expected<const void*, Error> FindPointer(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			const void*			GetPointer(std::string_view name, const void* defaultValue) const;
									/*!< Item 0 of field \a name, or \a defaultValue. */
			std::expected<void, Error> ReplacePointer(std::string_view name, const void* value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplacePointer(std::string_view name,
									std::int32_t index, const void* value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasPointer(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a pointer at \a index. */

	// Message

			std::expected<void, Error> AddMessage(std::string_view name, const BMessage& value);
									/*!< Appends a message to field \a name. */
			std::expected<BMessage, Error> FindMessage(std::string_view name,
									std::int32_t index = 0) const;
									/*!< Item \a index of field \a name. */
			std::expected<void, Error> ReplaceMessage(std::string_view name, const BMessage& value);
									/*!< Replaces item 0 of field \a name. */
			std::expected<void, Error> ReplaceMessage(std::string_view name,
									std::int32_t index, const BMessage& value);
									/*!< Replaces item \a index of field \a name. */
			bool				HasMessage(std::string_view name, std::int32_t index = 0) const;
									/*!< Whether field \a name holds a message at \a index. */

	// Raw data of an application's own type

			std::expected<void, Error> AddData(std::string_view name, BTypeCode type,
									std::span<const std::byte> data);
									/*!< Appends bytes of \a type; BadType for Any and the
										types with typed methods. */
			std::expected<std::span<const std::byte>, Error> FindData(std::string_view name,
									BTypeCode type, std::int32_t index = 0) const;
									/*!< The bytes of item \a index; \a type may be Any. */
			std::expected<void, Error> ReplaceData(std::string_view name, BTypeCode type,
									std::int32_t index, std::span<const std::byte> data);
									/*!< Replaces the bytes of item \a index. */

	// Flattening

			std::expected<std::vector<std::byte>, Error> Flatten() const;
									/*!< The message as bytes; NotSupported if it holds
										pointers, also in nested messages. */
			std::expected<void, Error> Unflatten(std::span<const std::byte> data);
									/*!< Replaces this message by the flattened one;
										BadData if \a data is not exactly one, OutOfRange
										beyond 64 levels of nesting. Unchanged on failure. */
	static	std::expected<BMessage, Error> FromFlattened(std::span<const std::byte> data);
									/*!< A new message from Flatten() output. */

			std::string			ToString() const;
									/*!< A readable listing for diagnostics. */

private:
			using Item = std::variant<bool, std::int8_t, std::uint8_t, std::int16_t,
				std::uint16_t, std::int32_t, std::uint32_t, std::int64_t, std::uint64_t,
				float, double, std::string, gfx::Point, gfx::Rect, gfx::Size, gfx::Color,
				const void*, std::shared_ptr<const BMessage>, std::vector<std::byte>>;

			struct Field {
				std::string		name;
				BTypeCode		type;
				std::vector<Item> items;
			};

			template<typename T>
			std::expected<void, Error> _Add(std::string_view name, BTypeCode type, T value);
			template<typename T>
			std::expected<const T*, Error> _Find(std::string_view name, BTypeCode type,
									std::int32_t index) const;
			template<typename T>
			std::expected<void, Error> _Replace(std::string_view name, BTypeCode type,
									std::int32_t index, T value);

			Field*				_FindField(std::string_view name);
			const Field*		_FindField(std::string_view name) const;

			std::vector<Field>	fFields;
			std::unique_ptr<MessageDelivery> fDelivery;
			std::optional<gfx::Point> fDropPoint;
									// set by the window it was dropped on
			std::int32_t		fCurrentSpecifier = -1;
									// the specifier left to resolve, -1 for none
			bool				fPlatformEvent = false;
									// sent by the platform (Record & Replay),
									// until its looper takes it

			friend class BLooper;
			friend class BWindow;
			friend class BMessenger;
			friend class MessageFlattener;
			friend struct PlatformEventMark;
};


/*!	The current specifier of a scripting message, as
	BMessage::GetCurrentSpecifier() gives it (Haiku: its out parameters). */
struct BSpecifier {
			std::int32_t		index;		/*!< Its index in the field "specifiers". */
			BMessage			specifier;	/*!< The specifier: form as `what`, "property",
												and "index", "range" or "name". */
			std::uint32_t		form;		/*!< The specifier's `what`, e.g. kIndexSpecifier. */
			std::string			property;	/*!< The property it names. */
};


} // namespace ngui
