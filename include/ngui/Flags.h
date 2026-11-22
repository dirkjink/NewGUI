/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <concepts>
#include <type_traits>


namespace ngui {


/*!	An enumeration Flags accepts: an unsigned `enum class` that opted in
	with NGUI_FLAG_ENUM(). The opt-in is found by argument-dependent lookup,
	so it works for enumerations in any namespace. */
template<typename E>
concept FlagEnum = std::is_enum_v<E> && std::is_unsigned_v<std::underlying_type_t<E>>
	&& requires(E flag) {
		{ NguiIsFlagEnum(flag) } -> std::same_as<bool>;
	};


/*!	A type-safe set of bit flags over the enumeration \a E. Replaces Haiku's
	anonymous `B_` constants combined with `|` (CONVENTIONS.md, section 3,
	rule 5; specification 18.3, L5): `ControlStateFlags state
	= ControlState::Focused | ControlState::Clicked;`.

	Trivially copyable; the value is the bitwise or of the contained flags.
*/
template<FlagEnum E>
class Flags {
public:
	/*!	The underlying integer type of \a E. */
	using Underlying = std::underlying_type_t<E>;

	constexpr					Flags() = default;
									/*!< The empty set. */
	constexpr					Flags(E flag);
									/*!< The set with the single \a flag; implicit, so that an `E`
										converts. */

	static	constexpr Flags		FromRaw(Underlying bits);
									/*!< The set whose value is \a bits. */
			constexpr Underlying Raw() const;
									/*!< The bitwise or of all contained flags. */

			constexpr bool		Has(E flag) const;
									/*!< All bits of \a flag are set. */
			constexpr bool		HasAny(Flags other) const;
									/*!< At least one bit of \a other is set. */
			constexpr bool		IsEmpty() const;
									/*!< No bit is set. */

			constexpr Flags&	Set(E flag, bool on = true);
									/*!< Sets the bits of \a flag, or clears them if \a on is
										false. */
			constexpr Flags&	Clear(E flag);
									/*!< Clears the bits of \a flag. */

			constexpr bool		operator==(const Flags& other) const = default;
									/*!< The same bits are set. */
			constexpr Flags		operator|(Flags other) const;
									/*!< Union. */
			constexpr Flags		operator&(Flags other) const;
									/*!< Intersection. */
			constexpr Flags		operator^(Flags other) const;
									/*!< Symmetric difference. */
			constexpr Flags		operator~() const;
									/*!< Complement over all bits of #Underlying. */
			constexpr Flags&	operator|=(Flags other);
									/*!< Adds the flags of \a other. */
			constexpr Flags&	operator&=(Flags other);
									/*!< Keeps only the flags also in \a other. */
			constexpr Flags&	operator^=(Flags other);
									/*!< Toggles the flags of \a other. */

private:
			Underlying			fBits = 0;
};


template<FlagEnum E>
constexpr
Flags<E>::Flags(E flag)
	:
	fBits(static_cast<Underlying>(flag))
{
}


template<FlagEnum E>
constexpr Flags<E>
Flags<E>::FromRaw(Underlying bits)
{
	Flags flags;
	flags.fBits = bits;
	return flags;
}


template<FlagEnum E>
constexpr typename Flags<E>::Underlying
Flags<E>::Raw() const
{
	return fBits;
}


template<FlagEnum E>
constexpr bool
Flags<E>::Has(E flag) const
{
	const Underlying bits = static_cast<Underlying>(flag);
	return (fBits & bits) == bits;
}


template<FlagEnum E>
constexpr bool
Flags<E>::HasAny(Flags other) const
{
	return (fBits & other.fBits) != 0;
}


template<FlagEnum E>
constexpr bool
Flags<E>::IsEmpty() const
{
	return fBits == 0;
}


template<FlagEnum E>
constexpr Flags<E>&
Flags<E>::Set(E flag, bool on)
{
	if (on)
		fBits |= static_cast<Underlying>(flag);
	else
		fBits &= static_cast<Underlying>(~static_cast<Underlying>(flag));
	return *this;
}


template<FlagEnum E>
constexpr Flags<E>&
Flags<E>::Clear(E flag)
{
	return Set(flag, false);
}


template<FlagEnum E>
constexpr Flags<E>
Flags<E>::operator|(Flags other) const
{
	return FromRaw(static_cast<Underlying>(fBits | other.fBits));
}


template<FlagEnum E>
constexpr Flags<E>
Flags<E>::operator&(Flags other) const
{
	return FromRaw(static_cast<Underlying>(fBits & other.fBits));
}


template<FlagEnum E>
constexpr Flags<E>
Flags<E>::operator^(Flags other) const
{
	return FromRaw(static_cast<Underlying>(fBits ^ other.fBits));
}


template<FlagEnum E>
constexpr Flags<E>
Flags<E>::operator~() const
{
	return FromRaw(static_cast<Underlying>(~fBits));
}


template<FlagEnum E>
constexpr Flags<E>&
Flags<E>::operator|=(Flags other)
{
	fBits |= other.fBits;
	return *this;
}


template<FlagEnum E>
constexpr Flags<E>&
Flags<E>::operator&=(Flags other)
{
	fBits &= other.fBits;
	return *this;
}


template<FlagEnum E>
constexpr Flags<E>&
Flags<E>::operator^=(Flags other)
{
	fBits ^= other.fBits;
	return *this;
}


} // namespace ngui


/*!	Opts the `enum class` \a E into ngui::Flags and makes `E | E` give a
	`Flags<E>`. Place it in the namespace of \a E, right after the
	enumeration; the enumerators are bit masks:

	\code
	enum class ControlState : uint32_t {
		Focused = 1u << 0,
		Clicked = 1u << 1
	};
	NGUI_FLAG_ENUM(ControlState)

	using ControlStateFlags = ngui::Flags<ControlState>;
	\endcode
*/
#define NGUI_FLAG_ENUM(E) \
	[[maybe_unused]] constexpr bool \
	NguiIsFlagEnum(E) \
	{ \
		return true; \
	} \
	constexpr ::ngui::Flags<E> \
	operator|(E first, E second) \
	{ \
		return ::ngui::Flags<E>(first) | ::ngui::Flags<E>(second); \
	}
