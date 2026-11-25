/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// The fonts of the system over DirectWrite: the system's font fallback
// (IDWriteFontFallback, Windows 8.1) for characters, the system's font
// collection for the list; the font files through the local font file
// loader, as paths for HarfBuzz and the backends.

#include <ngui/text/FontSource.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dwrite_2.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#endif


namespace ngui::text {


#ifdef _WIN32


namespace {


// Holds a COM interface and releases it.
template<typename T>
class Com {
public:
	Com() = default;
	Com(const Com&) = delete;
	Com& operator=(const Com&) = delete;

	~Com()
	{
		if (fPointer != nullptr)
			fPointer->Release();
	}

	T* operator->() const
	{
		return fPointer;
	}

	T* Get() const
	{
		return fPointer;
	}

	// For the out parameter of a COM call; releases what it holds.
	T** Out()
	{
		if (fPointer != nullptr)
			fPointer->Release();
		fPointer = nullptr;
		return &fPointer;
	}

	// The same object as interface \a U, or an empty holder.
	template<typename U>
	Com<U> As() const
	{
		Com<U> other;
		if (fPointer != nullptr)
			(void)fPointer->QueryInterface(__uuidof(U), reinterpret_cast<void**>(other.Out()));
		return other;
	}

	Com(Com&& other) noexcept
		:
		fPointer(std::exchange(other.fPointer, nullptr))
	{
	}

private:
	T* fPointer = nullptr;
};


std::wstring
Wide(std::string_view text)
{
	if (text.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
		static_cast<int>(text.size()), nullptr, 0);
	std::wstring wide(static_cast<std::size_t>(length), L'\0');
	(void)MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
		wide.data(), length);
	return wide;
}


std::string
Utf8(std::wstring_view text)
{
	if (text.empty())
		return {};
	const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
		static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string utf8(static_cast<std::size_t>(length), '\0');
	(void)WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
		utf8.data(), length, nullptr, nullptr);
	return utf8;
}


// The English name in \a names, else the first.
std::string
Name(IDWriteLocalizedStrings* names)
{
	if (names == nullptr || names->GetCount() == 0)
		return {};
	UINT32 index = 0;
	BOOL exists = FALSE;
	if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists)
		index = 0;
	UINT32 length = 0;
	if (FAILED(names->GetStringLength(index, &length)))
		return {};
	std::wstring name(length + 1, L'\0');
	if (FAILED(names->GetString(index, name.data(), length + 1)))
		return {};
	name.resize(length);
	return Utf8(name);
}


// The file and face of \a font, if it is a single local file.
std::optional<FontLocation>
Location(IDWriteFont* font)
{
	Com<IDWriteFontFace> face;
	if (FAILED(font->CreateFontFace(face.Out())))
		return std::nullopt;
	UINT32 count = 0;
	if (FAILED(face->GetFiles(&count, nullptr)) || count != 1)
		return std::nullopt;
	Com<IDWriteFontFile> file;
	if (FAILED(face->GetFiles(&count, file.Out())))
		return std::nullopt;
	Com<IDWriteFontFileLoader> loader;
	if (FAILED(file->GetLoader(loader.Out())))
		return std::nullopt;
	const Com<IDWriteLocalFontFileLoader> local = loader.As<IDWriteLocalFontFileLoader>();
	if (local.Get() == nullptr)
		return std::nullopt;
	const void* key = nullptr;
	UINT32 keySize = 0;
	UINT32 length = 0;
	if (FAILED(file->GetReferenceKey(&key, &keySize))
		|| FAILED(local->GetFilePathLengthFromKey(key, keySize, &length))) {
		return std::nullopt;
	}
	std::wstring path(length + 1, L'\0');
	if (FAILED(local->GetFilePathFromKey(key, keySize, path.data(), length + 1)))
		return std::nullopt;
	path.resize(length);
	return FontLocation{Utf8(path), face->GetIndex()};
}


// One character for IDWriteFontFallback::MapCharacters(): the text it
// analyzes, left to right, in the user's locale.
class CharacterSource final : public IDWriteTextAnalysisSource {
public:
	explicit CharacterSource(std::wstring text)
		:
		fText(std::move(text))
	{
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
	{
		if (id == __uuidof(IUnknown) || id == __uuidof(IDWriteTextAnalysisSource)) {
			*object = this;
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	// on the stack: no counting
	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return 1;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		return 1;
	}

	HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** text,
		UINT32* length) override
	{
		if (position >= fText.size()) {
			*text = nullptr;
			*length = 0;
		} else {
			*text = fText.data() + position;
			*length = static_cast<UINT32>(fText.size()) - position;
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR** text,
		UINT32* length) override
	{
		if (position == 0 || position > fText.size()) {
			*text = nullptr;
			*length = 0;
		} else {
			*text = fText.data();
			*length = position;
		}
		return S_OK;
	}

	DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override
	{
		return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
	}

	HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position, UINT32* length,
		const WCHAR** locale) override
	{
		*locale = fLocale;
		*length = static_cast<UINT32>(fText.size()) - std::min(position,
			static_cast<UINT32>(fText.size()));
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 position, UINT32* length,
		IDWriteNumberSubstitution** substitution) override
	{
		*substitution = nullptr;
		*length = static_cast<UINT32>(fText.size()) - std::min(position,
			static_cast<UINT32>(fText.size()));
		return S_OK;
	}

	UINT32 Length() const
	{
		return static_cast<UINT32>(fText.size());
	}

	// The user's locale: Han characters come in the fonts of its language.
	void UseUserLocale()
	{
		if (GetUserDefaultLocaleName(fLocale, LOCALE_NAME_MAX_LENGTH) == 0)
			fLocale[0] = L'\0';
	}

private:
	std::wstring fText;
	WCHAR fLocale[LOCALE_NAME_MAX_LENGTH] = {};
};


class DirectWriteSource final : public FontSource {
public:
	DirectWriteSource(Com<IDWriteFactory2> factory, Com<IDWriteFontFallback> fallback,
		Com<IDWriteFontCollection> fonts)
		:
		fFactory(std::move(factory)),
		fFallback(std::move(fallback)),
		fFonts(std::move(fonts))
	{
	}

	std::optional<FontLocation> Fallback(char32_t codePoint,
		const FontFamilyStyle& like) const override
	{
		std::wstring text;
		if (codePoint >= 0x10000) {
			const char32_t value = codePoint - 0x10000;
			text.push_back(static_cast<wchar_t>(0xd800 + (value >> 10)));
			text.push_back(static_cast<wchar_t>(0xdc00 + (value & 0x3ff)));
		} else
			text.push_back(static_cast<wchar_t>(codePoint));
		CharacterSource source(std::move(text));
		source.UseUserLocale();

		DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
		if (like.face.Has(BFontFace::Bold))
			weight = DWRITE_FONT_WEIGHT_BOLD;
		else if (like.face.Has(BFontFace::Heavy))
			weight = DWRITE_FONT_WEIGHT_HEAVY;
		else if (like.face.Has(BFontFace::Light))
			weight = DWRITE_FONT_WEIGHT_LIGHT;
		const DWRITE_FONT_STYLE style = like.face.Has(BFontFace::Italic)
			? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
		const std::wstring family = Wide(like.family);

		std::scoped_lock lock(fLock);
		UINT32 mapped = 0;
		Com<IDWriteFont> font;
		FLOAT scale = 1;
		if (FAILED(fFallback->MapCharacters(&source, 0, source.Length(), fFonts.Get(),
				family.empty() ? nullptr : family.c_str(), weight, style,
				DWRITE_FONT_STRETCH_NORMAL, &mapped, font.Out(), &scale))
			|| font.Get() == nullptr) {
			return std::nullopt;
		}
		return Location(font.Get());
	}

	std::vector<SourceFont> List() const override
	{
		std::scoped_lock lock(fLock);
		std::vector<SourceFont> list;
		std::set<std::pair<std::string, std::string>> seen;
		const UINT32 families = fFonts->GetFontFamilyCount();
		for (UINT32 i = 0; i < families; i++) {
			Com<IDWriteFontFamily> family;
			Com<IDWriteLocalizedStrings> familyNames;
			if (FAILED(fFonts->GetFontFamily(i, family.Out()))
				|| FAILED(family->GetFamilyNames(familyNames.Out()))) {
				continue;
			}
			const std::string familyName = Name(familyNames.Get());
			const UINT32 count = family->GetFontCount();
			for (UINT32 k = 0; k < count; k++) {
				Com<IDWriteFont> font;
				Com<IDWriteLocalizedStrings> faceNames;
				if (FAILED(family->GetFont(k, font.Out()))
					|| font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE
					|| FAILED(font->GetFaceNames(faceNames.Out()))) {
					continue;
				}
				const std::string style = Name(faceNames.Get());
				if (familyName.empty() || style.empty()
					|| seen.contains({familyName, style})) {
					continue;
				}
				std::optional<FontLocation> location = Location(font.Get());
				if (!location.has_value())
					continue;
				seen.emplace(familyName, style);

				BFontFaceFlags face;
				const DWRITE_FONT_WEIGHT weight = font->GetWeight();
				if (weight >= DWRITE_FONT_WEIGHT_HEAVY)
					face |= BFontFace::Heavy;
				else if (weight >= DWRITE_FONT_WEIGHT_BOLD)
					face |= BFontFace::Bold;
				else if (weight <= DWRITE_FONT_WEIGHT_LIGHT)
					face |= BFontFace::Light;
				if (font->GetStyle() != DWRITE_FONT_STYLE_NORMAL)
					face |= BFontFace::Italic;
				if (face.IsEmpty())
					face = BFontFace::Regular;
				const Com<IDWriteFont1> font1 = font.As<IDWriteFont1>();
				const bool fixed = font1.Get() != nullptr && font1->IsMonospacedFont() != FALSE;
				list.push_back({std::move(*location), familyName, style, face, fixed});
			}
		}
		std::ranges::sort(list, [](const SourceFont& a, const SourceFont& b) {
			return std::tie(a.family, a.style) < std::tie(b.family, b.style);
		});
		return list;
	}

private:
	mutable std::mutex fLock;
	Com<IDWriteFactory2> fFactory;
	Com<IDWriteFontFallback> fFallback;
	Com<IDWriteFontCollection> fFonts;
};


} // namespace


std::expected<std::unique_ptr<FontSource>, Error>
CreateDirectWriteSource()
{
	Com<IDWriteFactory2> factory;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
			reinterpret_cast<IUnknown**>(factory.Out())))) {
		return Failure(ErrorCode::IoError, "DirectWrite 2 (Windows 8.1) cannot be started");
	}
	Com<IDWriteFontFallback> fallback;
	Com<IDWriteFontCollection> fonts;
	if (FAILED(factory->GetSystemFontFallback(fallback.Out()))
		|| FAILED(factory->GetSystemFontCollection(fonts.Out(), FALSE))) {
		return Failure(ErrorCode::IoError, "DirectWrite has no system fonts");
	}
	return std::make_unique<DirectWriteSource>(std::move(factory), std::move(fallback),
		std::move(fonts));
}


#else


std::expected<std::unique_ptr<FontSource>, Error>
CreateDirectWriteSource()
{
	return Failure(ErrorCode::NotSupported, "no DirectWrite on this system");
}


#endif


} // namespace ngui::text
