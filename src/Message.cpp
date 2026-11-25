/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Message.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

#include <ngui/AppDefs.h>

#include "LooperPort.h"


namespace ngui {


namespace {


// The fields of scripting (Haiku: B_SPECIFIER_ENTRY, B_PROPERTY_ENTRY,
// B_PROPERTY_NAME_ENTRY).
constexpr std::string_view kSpecifierField = "specifiers";
constexpr std::string_view kPropertyField = "property";
constexpr std::string_view kNameField = "name";


// The type of the items a typed method stores.
template<typename T>
constexpr BTypeCode
TypeOf()
{
	if constexpr (std::is_same_v<T, bool>)
		return BTypeCode::Bool;
	else if constexpr (std::is_same_v<T, std::int8_t>)
		return BTypeCode::Int8;
	else if constexpr (std::is_same_v<T, std::uint8_t>)
		return BTypeCode::UInt8;
	else if constexpr (std::is_same_v<T, std::int16_t>)
		return BTypeCode::Int16;
	else if constexpr (std::is_same_v<T, std::uint16_t>)
		return BTypeCode::UInt16;
	else if constexpr (std::is_same_v<T, std::int32_t>)
		return BTypeCode::Int32;
	else if constexpr (std::is_same_v<T, std::uint32_t>)
		return BTypeCode::UInt32;
	else if constexpr (std::is_same_v<T, std::int64_t>)
		return BTypeCode::Int64;
	else if constexpr (std::is_same_v<T, std::uint64_t>)
		return BTypeCode::UInt64;
	else if constexpr (std::is_same_v<T, float>)
		return BTypeCode::Float;
	else if constexpr (std::is_same_v<T, double>)
		return BTypeCode::Double;
	else if constexpr (std::is_same_v<T, std::string>)
		return BTypeCode::String;
	else if constexpr (std::is_same_v<T, gfx::Point>)
		return BTypeCode::Point;
	else if constexpr (std::is_same_v<T, gfx::Rect>)
		return BTypeCode::Rect;
	else if constexpr (std::is_same_v<T, gfx::Size>)
		return BTypeCode::Size;
	else if constexpr (std::is_same_v<T, gfx::Color>)
		return BTypeCode::Color;
	else if constexpr (std::is_same_v<T, const void*>)
		return BTypeCode::Pointer;
	else
		return BTypeCode::Message;
}


constexpr std::array kTypedCodes = {BTypeCode::Bool, BTypeCode::Int8, BTypeCode::UInt8,
	BTypeCode::Int16, BTypeCode::UInt16, BTypeCode::Int32, BTypeCode::UInt32, BTypeCode::Int64,
	BTypeCode::UInt64, BTypeCode::Float, BTypeCode::Double, BTypeCode::String, BTypeCode::Point,
	BTypeCode::Rect, BTypeCode::Size, BTypeCode::Color, BTypeCode::Pointer, BTypeCode::Message};


bool
IsTyped(BTypeCode type)
{
	return std::find(kTypedCodes.begin(), kTypedCodes.end(), type) != kTypedCodes.end();
}


bool
Matches(BTypeCode asked, BTypeCode actual)
{
	return asked == BTypeCode::Any || asked == actual;
}


Error
MissingField(std::string_view name)
{
	return Error{ErrorCode::NotFound, "no field \"" + std::string(name) + "\""};
}


Error
WrongType(std::string_view name)
{
	return Error{ErrorCode::BadType, "the field \"" + std::string(name)
		+ "\" holds another type"};
}


Error
MissingIndex(std::string_view name, std::int32_t index)
{
	return Error{ErrorCode::OutOfRange, "the field \"" + std::string(name) + "\" has no item "
		+ std::to_string(index)};
}


template<typename F>
bool
SameBits(F a, F b)
{
	return std::bit_cast<std::conditional_t<sizeof(F) == 4, std::uint32_t, std::uint64_t>>(a)
		== std::bit_cast<std::conditional_t<sizeof(F) == 4, std::uint32_t, std::uint64_t>>(b);
}


std::string
CodeText(BTypeCode type)
{
	const auto code = static_cast<std::uint32_t>(type);
	std::string text = "'";
	for (int shift = 24; shift >= 0; shift -= 8) {
		const auto c = static_cast<char>((code >> shift) & 0xff);
		text += c >= 0x20 && c < 0x7f ? c : '?';
	}
	return text + "'";
}


} // namespace


// Writes and reads the flattened form. The format (version 1, all numbers
// little-endian):
//   "NGMS" u8 version  u32 what  u32 fieldCount
//   per field: u32 nameLength, name, u32 type, u32 itemCount (>= 1), items
//   items: bool u8 (0 or 1); integers in their width; float, double and the
//   floats of Point (2), Rect (4), Size (2), Color (4) as IEEE bits;
//   String, Message (flattened) and raw data as u32 length and bytes.
class MessageFlattener {
public:
	static constexpr std::uint8_t kVersion = 1;
	static constexpr int kMaxDepth = 64;

	static std::expected<void, Error> Write(const BMessage& message,
		std::vector<std::byte>& out)
	{
		const std::size_t start = out.size();
		for (const char c : {'N', 'G', 'M', 'S'})
			out.push_back(static_cast<std::byte>(c));
		out.push_back(static_cast<std::byte>(kVersion));
		Put(out, message.what);
		Put(out, static_cast<std::uint32_t>(message.fFields.size()));
		for (const BMessage::Field& field : message.fFields) {
			PutBytes(out, std::as_bytes(std::span(field.name)));
			Put(out, static_cast<std::uint32_t>(field.type));
			Put(out, static_cast<std::uint32_t>(field.items.size()));
			for (const BMessage::Item& item : field.items) {
				const std::expected<void, Error> written = WriteItem(item, out);
				if (!written.has_value()) {
					out.resize(start);
					return std::unexpected(Error{written.error().code,
						"field \"" + field.name + "\": " + written.error().message});
				}
			}
		}
		return {};
	}

	static std::expected<BMessage, Error> Read(std::span<const std::byte> data, int depth)
	{
		if (depth > kMaxDepth)
			return Failure(ErrorCode::OutOfRange, "messages nested too deeply");
		Reader reader{data};
		std::array<std::byte, 5> magic{};
		if (!reader.Take(magic) || magic[0] != std::byte{'N'} || magic[1] != std::byte{'G'}
			|| magic[2] != std::byte{'M'} || magic[3] != std::byte{'S'}) {
			return Failure(ErrorCode::BadData, "not a flattened message");
		}
		if (magic[4] != std::byte{kVersion})
			return Failure(ErrorCode::BadData, "unknown version of the flattened form");

		BMessage message;
		std::uint32_t fieldCount = 0;
		if (!reader.Get(message.what) || !reader.Get(fieldCount))
			return Truncated();
		// Every field takes at least 13 bytes; a count beyond that is a lie.
		if (fieldCount > reader.Left() / 13)
			return Truncated();
		message.fFields.reserve(fieldCount);
		for (std::uint32_t f = 0; f < fieldCount; f++) {
			std::span<const std::byte> name;
			std::uint32_t type = 0;
			std::uint32_t itemCount = 0;
			if (!reader.GetBytes(name) || !reader.Get(type) || !reader.Get(itemCount))
				return Truncated();
			BMessage::Field field;
			field.name.assign(reinterpret_cast<const char*>(name.data()), name.size());
			field.type = static_cast<BTypeCode>(type);
			if (field.name.empty() || message._FindField(field.name) != nullptr)
				return Failure(ErrorCode::BadData, "empty or repeated field name");
			if (itemCount == 0 || itemCount > reader.Left())
				return Truncated();
			if (field.type == BTypeCode::Any || field.type == BTypeCode::Pointer)
				return Failure(ErrorCode::BadData, "a field of type " + CodeText(field.type));
			field.items.reserve(itemCount);
			for (std::uint32_t i = 0; i < itemCount; i++) {
				std::expected<BMessage::Item, Error> item = ReadItem(field.type, reader, depth);
				if (!item.has_value())
					return std::unexpected(item.error());
				field.items.push_back(std::move(*item));
			}
			message.fFields.push_back(std::move(field));
		}
		if (reader.Left() != 0)
			return Failure(ErrorCode::BadData, "bytes after the message");
		return message;
	}

private:
	struct Reader {
		std::span<const std::byte> data;

		std::size_t Left() const
		{
			return data.size();
		}

		template<std::size_t N>
		bool Take(std::array<std::byte, N>& bytes)
		{
			if (data.size() < N)
				return false;
			std::copy_n(data.begin(), N, bytes.begin());
			data = data.subspan(N);
			return true;
		}

		template<typename T>
		bool Get(T& value)
		{
			std::array<std::byte, sizeof(T)> bytes;
			if (!Take(bytes))
				return false;
			using Bits = std::conditional_t<sizeof(T) == 1, std::uint8_t,
				std::conditional_t<sizeof(T) == 2, std::uint16_t,
				std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
			Bits bits = 0;
			for (std::size_t i = 0; i < sizeof(T); i++)
				bits = static_cast<Bits>(bits
					| static_cast<Bits>(static_cast<Bits>(bytes[i]) << (8 * i)));
			value = std::bit_cast<T>(bits);
			return true;
		}

		bool GetBytes(std::span<const std::byte>& bytes)
		{
			std::uint32_t length = 0;
			if (!Get(length) || length > data.size())
				return false;
			bytes = data.first(length);
			data = data.subspan(length);
			return true;
		}
	};

	static std::unexpected<Error> Truncated()
	{
		return Failure(ErrorCode::BadData, "the flattened message is truncated");
	}

	template<typename T>
	static void Put(std::vector<std::byte>& out, T value)
	{
		using Bits = std::conditional_t<sizeof(T) == 1, std::uint8_t,
			std::conditional_t<sizeof(T) == 2, std::uint16_t,
			std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
		const auto bits = std::bit_cast<Bits>(value);
		for (std::size_t i = 0; i < sizeof(T); i++)
			out.push_back(static_cast<std::byte>((bits >> (8 * i)) & 0xff));
	}

	static void PutBytes(std::vector<std::byte>& out, std::span<const std::byte> bytes)
	{
		Put(out, static_cast<std::uint32_t>(bytes.size()));
		out.insert(out.end(), bytes.begin(), bytes.end());
	}

	static std::expected<void, Error> WriteItem(const BMessage::Item& item,
		std::vector<std::byte>& out)
	{
		return std::visit([&](const auto& value) -> std::expected<void, Error> {
			using T = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<T, bool>) {
				Put(out, static_cast<std::uint8_t>(value ? 1 : 0));
			} else if constexpr (std::is_arithmetic_v<T>) {
				Put(out, value);
			} else if constexpr (std::is_same_v<T, std::string>) {
				PutBytes(out, std::as_bytes(std::span(value)));
			} else if constexpr (std::is_same_v<T, gfx::Point>) {
				Put(out, value.x);
				Put(out, value.y);
			} else if constexpr (std::is_same_v<T, gfx::Rect>) {
				Put(out, value.x0);
				Put(out, value.y0);
				Put(out, value.x1);
				Put(out, value.y1);
			} else if constexpr (std::is_same_v<T, gfx::Size>) {
				Put(out, value.w);
				Put(out, value.h);
			} else if constexpr (std::is_same_v<T, gfx::Color>) {
				Put(out, value.r);
				Put(out, value.g);
				Put(out, value.b);
				Put(out, value.a);
			} else if constexpr (std::is_same_v<T, const void*>) {
				return Failure(ErrorCode::NotSupported, "pointers cannot be flattened");
			} else if constexpr (std::is_same_v<T, std::shared_ptr<const BMessage>>) {
				std::vector<std::byte> nested;
				const std::expected<void, Error> written = Write(*value, nested);
				if (!written.has_value())
					return written;
				PutBytes(out, nested);
			} else {
				PutBytes(out, value);
			}
			return {};
		}, item);
	}

	template<typename T>
	static std::expected<BMessage::Item, Error> ReadValue(Reader& reader)
	{
		T value{};
		if (!reader.Get(value))
			return Truncated();
		return BMessage::Item(value);
	}

	static std::expected<BMessage::Item, Error> ReadItem(BTypeCode type, Reader& reader,
		int depth)
	{
		switch (type) {
			case BTypeCode::Bool:
			{
				std::uint8_t value = 0;
				if (!reader.Get(value))
					return Truncated();
				if (value > 1)
					return Failure(ErrorCode::BadData, "a bool other than 0 or 1");
				return BMessage::Item(value == 1);
			}
			case BTypeCode::Int8:
				return ReadValue<std::int8_t>(reader);
			case BTypeCode::UInt8:
				return ReadValue<std::uint8_t>(reader);
			case BTypeCode::Int16:
				return ReadValue<std::int16_t>(reader);
			case BTypeCode::UInt16:
				return ReadValue<std::uint16_t>(reader);
			case BTypeCode::Int32:
				return ReadValue<std::int32_t>(reader);
			case BTypeCode::UInt32:
				return ReadValue<std::uint32_t>(reader);
			case BTypeCode::Int64:
				return ReadValue<std::int64_t>(reader);
			case BTypeCode::UInt64:
				return ReadValue<std::uint64_t>(reader);
			case BTypeCode::Float:
				return ReadValue<float>(reader);
			case BTypeCode::Double:
				return ReadValue<double>(reader);
			case BTypeCode::Point:
			{
				gfx::Point point{};
				if (!reader.Get(point.x) || !reader.Get(point.y))
					return Truncated();
				return BMessage::Item(point);
			}
			case BTypeCode::Rect:
			{
				gfx::Rect rect{};
				if (!reader.Get(rect.x0) || !reader.Get(rect.y0) || !reader.Get(rect.x1)
					|| !reader.Get(rect.y1)) {
					return Truncated();
				}
				return BMessage::Item(rect);
			}
			case BTypeCode::Size:
			{
				gfx::Size size{};
				if (!reader.Get(size.w) || !reader.Get(size.h))
					return Truncated();
				return BMessage::Item(size);
			}
			case BTypeCode::Color:
			{
				gfx::Color color{};
				if (!reader.Get(color.r) || !reader.Get(color.g) || !reader.Get(color.b)
					|| !reader.Get(color.a)) {
					return Truncated();
				}
				return BMessage::Item(color);
			}
			default:
				break;
		}

		std::span<const std::byte> bytes;
		if (!reader.GetBytes(bytes))
			return Truncated();
		if (type == BTypeCode::String) {
			return BMessage::Item(std::string(reinterpret_cast<const char*>(bytes.data()),
				bytes.size()));
		}
		if (type == BTypeCode::Message) {
			std::expected<BMessage, Error> nested = Read(bytes, depth + 1);
			if (!nested.has_value())
				return std::unexpected(nested.error());
			return BMessage::Item(std::make_shared<const BMessage>(std::move(*nested)));
		}
		return BMessage::Item(std::vector<std::byte>(bytes.begin(), bytes.end()));
	}
};


BMessage::BMessage() = default;


BMessage::BMessage(std::uint32_t command)
	:
	what(command)
{
}


BMessage::BMessage(const BMessage& other)
	:
	what(other.what),
	fFields(other.fFields),
	fDropPoint(other.fDropPoint),
	fCurrentSpecifier(other.fCurrentSpecifier)
{
}


BMessage::BMessage(BMessage&& other) noexcept = default;


BMessage::~BMessage() = default;


BMessage&
BMessage::operator=(const BMessage& other)
{
	if (this != &other) {
		what = other.what;
		fFields = other.fFields;
		fDelivery.reset();
		fDropPoint = other.fDropPoint;
		fCurrentSpecifier = other.fCurrentSpecifier;
	}
	return *this;
}


BMessage&
BMessage::operator=(BMessage&& other) noexcept = default;


bool
BMessage::WasDropped() const
{
	return fDropPoint.has_value();
}


gfx::Point
BMessage::DropPoint(gfx::Point* offset) const
{
	if (offset != nullptr)
		*offset = {0, 0};
	return fDropPoint.value_or(gfx::Point{0, 0});
}


bool
BMessage::WasDelivered() const
{
	return fDelivery != nullptr && fDelivery->delivered;
}


bool
BMessage::IsSourceWaiting() const
{
	return fDelivery != nullptr && fDelivery->reply != nullptr
		&& fDelivery->reply->IsWaiting();
}


bool
BMessage::IsReply() const
{
	return fDelivery != nullptr && fDelivery->isReply;
}


BMessenger
BMessage::ReturnAddress() const
{
	if (fDelivery == nullptr || fDelivery->reply == nullptr)
		return BMessenger();
	return fDelivery->reply->replyTo;
}


std::expected<void, Error>
BMessage::SendReply(const BMessage& reply)
{
	ReplyToken* token = fDelivery != nullptr ? fDelivery->reply.get() : nullptr;
	const bool waiting = token != nullptr && token->IsWaiting();
	if (token == nullptr || (!waiting && !token->replyTo.IsValid()))
		return Failure(ErrorCode::NotFound, "nobody to reply to");
	if (!token->Answer())
		return Failure(ErrorCode::InvalidArgument, "the message was answered already");

	auto answer = std::make_unique<BMessage>(reply);
	answer->fDelivery = std::make_unique<MessageDelivery>();
	answer->fDelivery->isReply = true;
	if (waiting) {
		const std::shared_ptr<ReplyWaiter> waiter = token->waiter.lock();
		if (waiter == nullptr)
			return Failure(ErrorCode::NotFound, "the sender stopped waiting");
		waiter->Deliver(std::move(*answer));
		return {};
	}
	answer->fDelivery->handlerToken = token->replyTo.fToken;
	return token->replyTo._Send(std::move(answer));
}


std::expected<void, Error>
BMessage::SendReply(std::uint32_t command)
{
	return SendReply(BMessage(command));
}


bool
BMessage::operator==(const BMessage& other) const
{
	if (what != other.what || fFields.size() != other.fFields.size())
		return false;
	for (std::size_t f = 0; f < fFields.size(); f++) {
		const Field& a = fFields[f];
		const Field& b = other.fFields[f];
		if (a.name != b.name || a.type != b.type || a.items.size() != b.items.size())
			return false;
		for (std::size_t i = 0; i < a.items.size(); i++) {
			const bool equal = std::visit([&](const auto& value) {
				using T = std::decay_t<decltype(value)>;
				const T& otherValue = std::get<T>(b.items[i]);
				if constexpr (std::is_floating_point_v<T>) {
					return SameBits(value, otherValue);
				} else if constexpr (std::is_same_v<T, gfx::Point>) {
					return SameBits(value.x, otherValue.x) && SameBits(value.y, otherValue.y);
				} else if constexpr (std::is_same_v<T, gfx::Rect>) {
					return SameBits(value.x0, otherValue.x0) && SameBits(value.y0, otherValue.y0)
						&& SameBits(value.x1, otherValue.x1) && SameBits(value.y1, otherValue.y1);
				} else if constexpr (std::is_same_v<T, gfx::Size>) {
					return SameBits(value.w, otherValue.w) && SameBits(value.h, otherValue.h);
				} else if constexpr (std::is_same_v<T, gfx::Color>) {
					return SameBits(value.r, otherValue.r) && SameBits(value.g, otherValue.g)
						&& SameBits(value.b, otherValue.b) && SameBits(value.a, otherValue.a);
				} else if constexpr (std::is_same_v<T, std::shared_ptr<const BMessage>>) {
					return *value == *otherValue;
				} else {
					return value == otherValue;
				}
			}, a.items[i]);
			if (!equal)
				return false;
		}
	}
	return true;
}


BMessage::Field*
BMessage::_FindField(std::string_view name)
{
	const auto field = std::find_if(fFields.begin(), fFields.end(),
		[&](const Field& candidate) { return candidate.name == name; });
	return field != fFields.end() ? &*field : nullptr;
}


const BMessage::Field*
BMessage::_FindField(std::string_view name) const
{
	return const_cast<BMessage*>(this)->_FindField(name);
}


std::int32_t
BMessage::CountNames(BTypeCode type) const
{
	return static_cast<std::int32_t>(std::count_if(fFields.begin(), fFields.end(),
		[&](const Field& field) { return Matches(type, field.type); }));
}


bool
BMessage::IsEmpty() const
{
	return fFields.empty();
}


void
BMessage::MakeEmpty()
{
	fFields.clear();
	fCurrentSpecifier = -1;
}


std::vector<BFieldInfo>
BMessage::Fields(BTypeCode type) const
{
	std::vector<BFieldInfo> fields;
	for (const Field& field : fFields) {
		if (Matches(type, field.type))
			fields.push_back({field.name, field.type,
				static_cast<std::int32_t>(field.items.size())});
	}
	return fields;
}


std::expected<BFieldInfo, Error>
BMessage::GetInfo(std::string_view name) const
{
	const Field* field = _FindField(name);
	if (field == nullptr)
		return std::unexpected(MissingField(name));
	return BFieldInfo{field->name, field->type, static_cast<std::int32_t>(field->items.size())};
}


bool
BMessage::HasData(std::string_view name, BTypeCode type, std::int32_t index) const
{
	const Field* field = _FindField(name);
	return field != nullptr && Matches(type, field->type) && index >= 0
		&& static_cast<std::size_t>(index) < field->items.size();
}


std::expected<void, Error>
BMessage::RemoveName(std::string_view name)
{
	const auto field = std::find_if(fFields.begin(), fFields.end(),
		[&](const Field& candidate) { return candidate.name == name; });
	if (field == fFields.end())
		return std::unexpected(MissingField(name));
	fFields.erase(field);
	return {};
}


std::expected<void, Error>
BMessage::RemoveData(std::string_view name, std::int32_t index)
{
	Field* field = _FindField(name);
	if (field == nullptr)
		return std::unexpected(MissingField(name));
	if (index < 0 || static_cast<std::size_t>(index) >= field->items.size())
		return std::unexpected(MissingIndex(name, index));
	field->items.erase(field->items.begin() + index);
	if (field->items.empty())
		return RemoveName(name);
	return {};
}


std::expected<void, Error>
BMessage::Rename(std::string_view oldName, std::string_view newName)
{
	Field* field = _FindField(oldName);
	if (field == nullptr)
		return std::unexpected(MissingField(oldName));
	if (newName.empty() || (newName != oldName && _FindField(newName) != nullptr))
		return Failure(ErrorCode::InvalidArgument, "the new name is empty or taken");
	field->name = newName;
	return {};
}


template<typename T>
std::expected<void, Error>
BMessage::_Add(std::string_view name, BTypeCode type, T value)
{
	if (name.empty())
		return Failure(ErrorCode::InvalidArgument, "empty field name");
	Field* field = _FindField(name);
	if (field == nullptr) {
		fFields.push_back({std::string(name), type, {}});
		field = &fFields.back();
	} else if (field->type != type) {
		return std::unexpected(WrongType(name));
	}
	field->items.emplace_back(std::move(value));
	return {};
}


template<typename T>
std::expected<const T*, Error>
BMessage::_Find(std::string_view name, BTypeCode type, std::int32_t index) const
{
	const Field* field = _FindField(name);
	if (field == nullptr)
		return std::unexpected(MissingField(name));
	if (!Matches(type, field->type))
		return std::unexpected(WrongType(name));
	if (index < 0 || static_cast<std::size_t>(index) >= field->items.size())
		return std::unexpected(MissingIndex(name, index));
	const T* value = std::get_if<T>(&field->items[static_cast<std::size_t>(index)]);
	if (value == nullptr)
		return std::unexpected(WrongType(name));
	return value;
}


template<typename T>
std::expected<void, Error>
BMessage::_Replace(std::string_view name, BTypeCode type, std::int32_t index, T value)
{
	Field* field = _FindField(name);
	if (field == nullptr)
		return std::unexpected(MissingField(name));
	if (field->type != type)
		return std::unexpected(WrongType(name));
	if (index < 0 || static_cast<std::size_t>(index) >= field->items.size())
		return std::unexpected(MissingIndex(name, index));
	field->items[static_cast<std::size_t>(index)] = std::move(value);
	return {};
}


// The typed methods of one type: Add, Find, Get, Replace (twice), Has.
// Stored is the type the items have in the message, Out what Find returns.
#define NGUI_MESSAGE_TYPED(Name, Param, Stored, Out, ToStored, ToOut) \
	std::expected<void, Error> \
	BMessage::Add##Name(std::string_view name, Param value) \
	{ \
		return _Add<Stored>(name, TypeOf<Stored>(), ToStored(value)); \
	} \
	\
	std::expected<Out, Error> \
	BMessage::Find##Name(std::string_view name, std::int32_t index) const \
	{ \
		using StoredType = Stored; \
		const std::expected<const StoredType*, Error> found \
			= _Find<StoredType>(name, TypeOf<StoredType>(), index); \
		if (!found.has_value()) \
			return std::unexpected(found.error()); \
		return ToOut(**found); \
	} \
	\
	std::expected<void, Error> \
	BMessage::Replace##Name(std::string_view name, Param value) \
	{ \
		return _Replace<Stored>(name, TypeOf<Stored>(), 0, ToStored(value)); \
	} \
	\
	std::expected<void, Error> \
	BMessage::Replace##Name(std::string_view name, std::int32_t index, Param value) \
	{ \
		return _Replace<Stored>(name, TypeOf<Stored>(), index, ToStored(value)); \
	} \
	\
	bool \
	BMessage::Has##Name(std::string_view name, std::int32_t index) const \
	{ \
		return _Find<Stored>(name, TypeOf<Stored>(), index).has_value(); \
	}

#define NGUI_MESSAGE_GET(Name, Param, Out) \
	Out \
	BMessage::Get##Name(std::string_view name, Param defaultValue) const \
	{ \
		return Find##Name(name).value_or(defaultValue); \
	}

#define NGUI_SAME(value) (value)
#define NGUI_TO_STRING(value) std::string(value)
#define NGUI_TO_VIEW(value) std::string_view(value)
#define NGUI_TO_SHARED(value) std::make_shared<const BMessage>(value)
#define NGUI_TO_COPY(value) BMessage(*(value))

#define NGUI_MESSAGE_SIMPLE(Name, Type) \
	NGUI_MESSAGE_TYPED(Name, Type, Type, Type, NGUI_SAME, NGUI_SAME) \
	NGUI_MESSAGE_GET(Name, Type, Type)

NGUI_MESSAGE_SIMPLE(Bool, bool)
NGUI_MESSAGE_SIMPLE(Int8, std::int8_t)
NGUI_MESSAGE_SIMPLE(UInt8, std::uint8_t)
NGUI_MESSAGE_SIMPLE(Int16, std::int16_t)
NGUI_MESSAGE_SIMPLE(UInt16, std::uint16_t)
NGUI_MESSAGE_SIMPLE(Int32, std::int32_t)
NGUI_MESSAGE_SIMPLE(UInt32, std::uint32_t)
NGUI_MESSAGE_SIMPLE(Int64, std::int64_t)
NGUI_MESSAGE_SIMPLE(UInt64, std::uint64_t)
NGUI_MESSAGE_SIMPLE(Float, float)
NGUI_MESSAGE_SIMPLE(Double, double)
NGUI_MESSAGE_SIMPLE(Pointer, const void*)

NGUI_MESSAGE_TYPED(String, std::string_view, std::string, std::string_view, NGUI_TO_STRING,
	NGUI_TO_VIEW)
NGUI_MESSAGE_GET(String, std::string_view, std::string_view)

NGUI_MESSAGE_TYPED(Point, const gfx::Point&, gfx::Point, gfx::Point, NGUI_SAME, NGUI_SAME)
NGUI_MESSAGE_GET(Point, const gfx::Point&, gfx::Point)
NGUI_MESSAGE_TYPED(Rect, const gfx::Rect&, gfx::Rect, gfx::Rect, NGUI_SAME, NGUI_SAME)
NGUI_MESSAGE_GET(Rect, const gfx::Rect&, gfx::Rect)
NGUI_MESSAGE_TYPED(Size, const gfx::Size&, gfx::Size, gfx::Size, NGUI_SAME, NGUI_SAME)
NGUI_MESSAGE_GET(Size, const gfx::Size&, gfx::Size)
NGUI_MESSAGE_TYPED(Color, const gfx::Color&, gfx::Color, gfx::Color, NGUI_SAME, NGUI_SAME)
NGUI_MESSAGE_GET(Color, const gfx::Color&, gfx::Color)

NGUI_MESSAGE_TYPED(Message, const BMessage&, std::shared_ptr<const BMessage>, BMessage,
	NGUI_TO_SHARED, NGUI_TO_COPY)

#undef NGUI_MESSAGE_SIMPLE
#undef NGUI_TO_COPY
#undef NGUI_TO_SHARED
#undef NGUI_TO_VIEW
#undef NGUI_TO_STRING
#undef NGUI_SAME
#undef NGUI_MESSAGE_GET
#undef NGUI_MESSAGE_TYPED


std::expected<void, Error>
BMessage::AddData(std::string_view name, BTypeCode type, std::span<const std::byte> data)
{
	if (type == BTypeCode::Any || IsTyped(type))
		return Failure(ErrorCode::BadType, CodeText(type) + " has typed methods");
	return _Add(name, type, std::vector<std::byte>(data.begin(), data.end()));
}


std::expected<std::span<const std::byte>, Error>
BMessage::FindData(std::string_view name, BTypeCode type, std::int32_t index) const
{
	const std::expected<const std::vector<std::byte>*, Error> found
		= _Find<std::vector<std::byte>>(name, type, index);
	if (!found.has_value())
		return std::unexpected(found.error());
	return std::span<const std::byte>(**found);
}


std::expected<void, Error>
BMessage::ReplaceData(std::string_view name, BTypeCode type, std::int32_t index,
	std::span<const std::byte> data)
{
	if (type == BTypeCode::Any || IsTyped(type))
		return Failure(ErrorCode::BadType, CodeText(type) + " has typed methods");
	return _Replace(name, type, index, std::vector<std::byte>(data.begin(), data.end()));
}


std::expected<std::vector<std::byte>, Error>
BMessage::Flatten() const
{
	std::vector<std::byte> bytes;
	const std::expected<void, Error> written = MessageFlattener::Write(*this, bytes);
	if (!written.has_value())
		return std::unexpected(written.error());
	return bytes;
}


std::expected<void, Error>
BMessage::Unflatten(std::span<const std::byte> data)
{
	std::expected<BMessage, Error> message = FromFlattened(data);
	if (!message.has_value())
		return std::unexpected(message.error());
	*this = std::move(*message);
	return {};
}


std::expected<BMessage, Error>
BMessage::FromFlattened(std::span<const std::byte> data)
{
	std::expected<BMessage, Error> message = MessageFlattener::Read(data, 0);
	// a new scripting message: its last specifier is the current one
	if (message.has_value()) {
		const Field* specifiers = message->_FindField(kSpecifierField);
		if (specifiers != nullptr && specifiers->type == BTypeCode::Message)
			message->fCurrentSpecifier = static_cast<std::int32_t>(specifiers->items.size()) - 1;
	}
	return message;
}


// #pragma mark - Specifiers


// Ported from Haiku: src/kits/app/Message.cpp (commit b1d497bb69).


std::expected<void, Error>
BMessage::AddSpecifier(std::string_view property)
{
	if (property.empty())
		return Failure(ErrorCode::InvalidArgument, "a specifier needs a property");
	BMessage message(kDirectSpecifier);
	(void)message.AddString(kPropertyField, property);
	return AddSpecifier(message);
}


std::expected<void, Error>
BMessage::AddSpecifier(std::string_view property, std::int32_t index)
{
	if (property.empty())
		return Failure(ErrorCode::InvalidArgument, "a specifier needs a property");
	BMessage message(kIndexSpecifier);
	(void)message.AddString(kPropertyField, property);
	(void)message.AddInt32("index", index);
	return AddSpecifier(message);
}


std::expected<void, Error>
BMessage::AddSpecifier(std::string_view property, std::int32_t index, std::int32_t range)
{
	if (range < 0)
		return Failure(ErrorCode::InvalidArgument, "a range must not be negative");
	if (property.empty())
		return Failure(ErrorCode::InvalidArgument, "a specifier needs a property");
	BMessage message(kRangeSpecifier);
	(void)message.AddString(kPropertyField, property);
	(void)message.AddInt32("index", index);
	(void)message.AddInt32("range", range);
	return AddSpecifier(message);
}


std::expected<void, Error>
BMessage::AddSpecifier(std::string_view property, std::string_view name)
{
	if (property.empty())
		return Failure(ErrorCode::InvalidArgument, "a specifier needs a property");
	BMessage message(kNameSpecifier);
	(void)message.AddString(kPropertyField, property);
	(void)message.AddString(kNameField, name);
	return AddSpecifier(message);
}


std::expected<void, Error>
BMessage::AddSpecifier(const BMessage& specifier)
{
	if (auto added = AddMessage(kSpecifierField, specifier); !added)
		return added;
	fCurrentSpecifier++;
	return {};
}


std::expected<void, Error>
BMessage::SetCurrentSpecifier(std::int32_t index)
{
	const Field* specifiers = _FindField(kSpecifierField);
	if (index < 0 || specifiers == nullptr
		|| index >= static_cast<std::int32_t>(specifiers->items.size())) {
		return Failure(ErrorCode::OutOfRange, "no such specifier");
	}
	fCurrentSpecifier = index;
	return {};
}


std::expected<BSpecifier, Error>
BMessage::GetCurrentSpecifier() const
{
	if (fCurrentSpecifier < 0 || !WasDelivered())
		return Failure(ErrorCode::BadScriptSyntax, "no specifier to resolve");
	auto specifier = FindMessage(kSpecifierField, fCurrentSpecifier);
	if (!specifier.has_value())
		return Failure(ErrorCode::BadScriptSyntax, "no specifier to resolve");
	const auto property = specifier->FindString(kPropertyField);
	if (!property.has_value())
		return Failure(ErrorCode::BadScriptSyntax, "the specifier names no property");
	const std::string name(*property);
	const std::uint32_t form = specifier->what;
	return BSpecifier{fCurrentSpecifier, std::move(*specifier), form, name};
}


std::int32_t
BMessage::CurrentSpecifierIndex() const
{
	return fCurrentSpecifier;
}


bool
BMessage::HasSpecifiers() const
{
	return _FindField(kSpecifierField) != nullptr;
}


std::expected<void, Error>
BMessage::PopSpecifier()
{
	if (fCurrentSpecifier < 0 || !WasDelivered())
		return Failure(ErrorCode::InvalidArgument, "no specifier to pop");
	fCurrentSpecifier--;
	return {};
}


std::string
BMessage::ToString() const
{
	std::string text = "BMessage(" + CodeText(static_cast<BTypeCode>(what)) + ") {\n";
	for (const Field& field : fFields) {
		text += "  " + field.name + " " + CodeText(field.type) + ":";
		for (const Item& item : field.items) {
			char buffer[64];
			std::visit([&](const auto& value) {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, bool>) {
					text += value ? " true" : " false";
				} else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
					std::snprintf(buffer, sizeof(buffer), " %" PRId64,
						static_cast<std::int64_t>(value));
					text += buffer;
				} else if constexpr (std::is_unsigned_v<T>) {
					std::snprintf(buffer, sizeof(buffer), " %" PRIu64,
						static_cast<std::uint64_t>(value));
					text += buffer;
				} else if constexpr (std::is_floating_point_v<T>) {
					std::snprintf(buffer, sizeof(buffer), " %g", static_cast<double>(value));
					text += buffer;
				} else if constexpr (std::is_same_v<T, std::string>) {
					text += " \"" + value + "\"";
				} else if constexpr (std::is_same_v<T, gfx::Point>) {
					std::snprintf(buffer, sizeof(buffer), " (%g %g)", static_cast<double>(value.x),
						static_cast<double>(value.y));
					text += buffer;
				} else if constexpr (std::is_same_v<T, gfx::Rect>) {
					std::snprintf(buffer, sizeof(buffer), " [%g %g %g %g]",
						static_cast<double>(value.x0), static_cast<double>(value.y0),
						static_cast<double>(value.x1), static_cast<double>(value.y1));
					text += buffer;
				} else if constexpr (std::is_same_v<T, gfx::Size>) {
					std::snprintf(buffer, sizeof(buffer), " %gx%g", static_cast<double>(value.w),
						static_cast<double>(value.h));
					text += buffer;
				} else if constexpr (std::is_same_v<T, gfx::Color>) {
					std::snprintf(buffer, sizeof(buffer), " (%g %g %g %g)",
						static_cast<double>(value.r), static_cast<double>(value.g),
						static_cast<double>(value.b), static_cast<double>(value.a));
					text += buffer;
				} else if constexpr (std::is_same_v<T, const void*>) {
					std::snprintf(buffer, sizeof(buffer), " %p", value);
					text += buffer;
				} else if constexpr (std::is_same_v<T, std::shared_ptr<const BMessage>>) {
					std::string nested = value->ToString();
					std::string indented;
					for (const char c : nested) {
						indented += c;
						if (c == '\n')
							indented += "  ";
					}
					text += " " + indented.substr(0, indented.size() - 2);
					text.pop_back();
				} else {
					text += " " + std::to_string(value.size()) + " bytes";
				}
			}, item);
		}
		text += "\n";
	}
	return text + "}\n";
}


} // namespace ngui
