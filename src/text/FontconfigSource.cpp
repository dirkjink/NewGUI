/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// The fonts of the system over fontconfig, its library loaded at run time
// (like SDL loads libdbus): only the few functions below, declared here
// after fontconfig's stable ABI (libfontconfig.so.1, fontconfig.h), so
// that building needs no fontconfig headers.

#include <ngui/text/FontSource.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(__unix__) && !defined(__APPLE__)
#include <dlfcn.h>
#define NGUI_WITH_FONTCONFIG 1
#endif


namespace ngui::text {


#ifdef NGUI_WITH_FONTCONFIG


namespace {


// fontconfig.h
using FcChar8 = unsigned char;
using FcChar32 = unsigned int;
using FcBool = int;
struct FcConfig;
struct FcPattern;
struct FcCharSet;
struct FcObjectSet;
struct FcFontSet {
	int				nfont;
	int				sfont;
	FcPattern**		fonts;
};
enum FcResult { FcResultMatch = 0 };
enum FcMatchKind { FcMatchPattern = 0 };

constexpr const char* kFamily = "family";
constexpr const char* kStyle = "style";
constexpr const char* kFile = "file";
constexpr const char* kIndex = "index";
constexpr const char* kWeight = "weight";
constexpr const char* kSlant = "slant";
constexpr const char* kSpacing = "spacing";
constexpr const char* kCharSet = "charset";
constexpr const char* kFontFormat = "fontformat";
constexpr int kWeightLight = 50;
constexpr int kWeightRegular = 80;
constexpr int kWeightBold = 200;
constexpr int kWeightHeavy = 210;
constexpr int kSlantItalic = 100;
constexpr int kMono = 100;


// The functions of the library, loaded once per process and never
// unloaded: fontconfig keeps global state (interned strings, the default
// languages) that only FcFini() frees, which a library must not call while
// other code of the process may use fontconfig.
struct Fontconfig {
	void*			library = nullptr;
	FcConfig*		(*InitLoadConfigAndFonts)() = nullptr;
	FcConfig*		(*ConfigCreate)() = nullptr;
	FcBool			(*ConfigParseAndLoad)(FcConfig*, const FcChar8*, FcBool) = nullptr;
	FcBool			(*ConfigBuildFonts)(FcConfig*) = nullptr;
	void			(*ConfigDestroy)(FcConfig*) = nullptr;
	FcPattern*		(*PatternCreate)() = nullptr;
	void			(*PatternDestroy)(FcPattern*) = nullptr;
	FcBool			(*PatternAddString)(FcPattern*, const char*, const FcChar8*) = nullptr;
	FcBool			(*PatternAddInteger)(FcPattern*, const char*, int) = nullptr;
	FcBool			(*PatternAddCharSet)(FcPattern*, const char*, const FcCharSet*) = nullptr;
	FcResult		(*PatternGetString)(const FcPattern*, const char*, int, FcChar8**) = nullptr;
	FcResult		(*PatternGetInteger)(const FcPattern*, const char*, int, int*) = nullptr;
	FcResult		(*PatternGetCharSet)(const FcPattern*, const char*, int, FcCharSet**)
		= nullptr;
	FcCharSet*		(*CharSetCreate)() = nullptr;
	FcBool			(*CharSetAddChar)(FcCharSet*, FcChar32) = nullptr;
	FcBool			(*CharSetHasChar)(const FcCharSet*, FcChar32) = nullptr;
	void			(*CharSetDestroy)(FcCharSet*) = nullptr;
	FcBool			(*ConfigSubstitute)(FcConfig*, FcPattern*, FcMatchKind) = nullptr;
	void			(*DefaultSubstitute)(FcPattern*) = nullptr;
	FcFontSet*		(*FontSort)(FcConfig*, FcPattern*, FcBool, FcCharSet**, FcResult*)
		= nullptr;
	FcFontSet*		(*FontList)(FcConfig*, FcPattern*, FcObjectSet*) = nullptr;
	void			(*FontSetDestroy)(FcFontSet*) = nullptr;
	FcObjectSet*	(*ObjectSetCreate)() = nullptr;
	FcBool			(*ObjectSetAdd)(FcObjectSet*, const char*) = nullptr;
	void			(*ObjectSetDestroy)(FcObjectSet*) = nullptr;

	// Loads the library and its functions; false if it or one is missing.
	bool Load()
	{
		library = dlopen("libfontconfig.so.1", RTLD_NOW | RTLD_LOCAL);
		if (library == nullptr)
			return false;
		bool complete = true;
		const auto bind = [&](auto& function, const char* name) {
			// POSIX: a function's address passes through dlsym()'s void*.
			void* symbol = dlsym(library, name);
			complete = complete && symbol != nullptr;
			function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(symbol);
		};
		bind(InitLoadConfigAndFonts, "FcInitLoadConfigAndFonts");
		bind(ConfigCreate, "FcConfigCreate");
		bind(ConfigParseAndLoad, "FcConfigParseAndLoad");
		bind(ConfigBuildFonts, "FcConfigBuildFonts");
		bind(ConfigDestroy, "FcConfigDestroy");
		bind(PatternCreate, "FcPatternCreate");
		bind(PatternDestroy, "FcPatternDestroy");
		bind(PatternAddString, "FcPatternAddString");
		bind(PatternAddInteger, "FcPatternAddInteger");
		bind(PatternAddCharSet, "FcPatternAddCharSet");
		bind(PatternGetString, "FcPatternGetString");
		bind(PatternGetInteger, "FcPatternGetInteger");
		bind(PatternGetCharSet, "FcPatternGetCharSet");
		bind(CharSetCreate, "FcCharSetCreate");
		bind(CharSetAddChar, "FcCharSetAddChar");
		bind(CharSetHasChar, "FcCharSetHasChar");
		bind(CharSetDestroy, "FcCharSetDestroy");
		bind(ConfigSubstitute, "FcConfigSubstitute");
		bind(DefaultSubstitute, "FcDefaultSubstitute");
		bind(FontSort, "FcFontSort");
		bind(FontList, "FcFontList");
		bind(FontSetDestroy, "FcFontSetDestroy");
		bind(ObjectSetCreate, "FcObjectSetCreate");
		bind(ObjectSetAdd, "FcObjectSetAdd");
		bind(ObjectSetDestroy, "FcObjectSetDestroy");
		return complete;
	}
};


// The library, or nullptr if it or a function is missing.
const Fontconfig*
Library()
{
	static const Fontconfig* const library = []() -> const Fontconfig* {
		static Fontconfig functions;
		return functions.Load() ? &functions : nullptr;
	}();
	return library;
}


class FontconfigSource final : public FontSource {
public:
	FontconfigSource(const Fontconfig* functions, FcConfig* config)
		:
		fFc(functions),
		fConfig(config)
	{
	}

	~FontconfigSource() override
	{
		fFc->ConfigDestroy(fConfig);
	}

	std::optional<FontLocation> Fallback(char32_t codePoint,
		const FontFamilyStyle& like) const override
	{
		// fontconfig's configuration is shared; one query at a time
		std::scoped_lock lock(fLock);
		FcPattern* pattern = fFc->PatternCreate();
		if (!like.family.empty()) {
			(void)fFc->PatternAddString(pattern, kFamily,
				reinterpret_cast<const FcChar8*>(like.family.c_str()));
		}
		int weight = kWeightRegular;
		if (like.face.Has(BFontFace::Bold))
			weight = kWeightBold;
		else if (like.face.Has(BFontFace::Heavy))
			weight = kWeightHeavy;
		else if (like.face.Has(BFontFace::Light))
			weight = kWeightLight;
		(void)fFc->PatternAddInteger(pattern, kWeight, weight);
		if (like.face.Has(BFontFace::Italic))
			(void)fFc->PatternAddInteger(pattern, kSlant, kSlantItalic);
		FcCharSet* wanted = fFc->CharSetCreate();
		(void)fFc->CharSetAddChar(wanted, codePoint);
		(void)fFc->PatternAddCharSet(pattern, kCharSet, wanted);
		fFc->CharSetDestroy(wanted);
		(void)fFc->ConfigSubstitute(fConfig, pattern, FcMatchPattern);
		fFc->DefaultSubstitute(pattern);

		FcResult result = FcResultMatch;
		FcFontSet* sorted = fFc->FontSort(fConfig, pattern, 0, nullptr, &result);
		fFc->PatternDestroy(pattern);
		std::optional<FontLocation> found;
		for (int i = 0; sorted != nullptr && i < sorted->nfont && !found.has_value(); i++) {
			const FcPattern* font = sorted->fonts[i];
			FcCharSet* charSet = nullptr;
			if (fFc->PatternGetCharSet(font, kCharSet, 0, &charSet) != FcResultMatch
				|| !fFc->CharSetHasChar(charSet, codePoint)) {
				continue;
			}
			found = _Location(font);
		}
		if (sorted != nullptr)
			fFc->FontSetDestroy(sorted);
		return found;
	}

	std::vector<SourceFont> List() const override
	{
		std::scoped_lock lock(fLock);
		FcPattern* pattern = fFc->PatternCreate();
		FcObjectSet* objects = fFc->ObjectSetCreate();
		for (const char* object : {kFamily, kStyle, kFile, kIndex, kWeight, kSlant, kSpacing,
				kFontFormat}) {
			(void)fFc->ObjectSetAdd(objects, object);
		}
		FcFontSet* fonts = fFc->FontList(fConfig, pattern, objects);
		fFc->ObjectSetDestroy(objects);
		fFc->PatternDestroy(pattern);

		std::vector<SourceFont> list;
		std::set<std::pair<std::string, std::string>> seen;
		for (int i = 0; fonts != nullptr && i < fonts->nfont; i++) {
			const FcPattern* font = fonts->fonts[i];
			std::optional<FontLocation> location = _Location(font);
			const std::string family = _String(font, kFamily);
			const std::string style = _String(font, kStyle);
			if (!location.has_value() || family.empty() || style.empty()
				|| !seen.emplace(family, style).second) {
				continue;
			}
			int weight = kWeightRegular;
			int slant = 0;
			int spacing = 0;
			(void)fFc->PatternGetInteger(font, kWeight, 0, &weight);
			(void)fFc->PatternGetInteger(font, kSlant, 0, &slant);
			(void)fFc->PatternGetInteger(font, kSpacing, 0, &spacing);
			BFontFaceFlags face;
			if (weight >= kWeightHeavy)
				face |= BFontFace::Heavy;
			else if (weight >= kWeightBold)
				face |= BFontFace::Bold;
			else if (weight <= kWeightLight)
				face |= BFontFace::Light;
			if (slant >= kSlantItalic)
				face |= BFontFace::Italic;
			if (face.IsEmpty())
				face = BFontFace::Regular;
			list.push_back({std::move(*location), family, style, face, spacing >= kMono});
		}
		if (fonts != nullptr)
			fFc->FontSetDestroy(fonts);
		std::ranges::sort(list, [](const SourceFont& a, const SourceFont& b) {
			return std::tie(a.family, a.style) < std::tie(b.family, b.style);
		});
		return list;
	}

private:
	std::string _String(const FcPattern* font, const char* object) const
	{
		FcChar8* value = nullptr;
		if (fFc->PatternGetString(font, object, 0, &value) != FcResultMatch || value == nullptr)
			return {};
		return reinterpret_cast<const char*>(value);
	}

	// The file and face of \a font, if the text subsystem can read it: an
	// OpenType font with TrueType or CFF outlines (not Type 1 or bitmaps).
	std::optional<FontLocation> _Location(const FcPattern* font) const
	{
		const std::string format = _String(font, kFontFormat);
		const std::string file = _String(font, kFile);
		if ((format != "TrueType" && format != "CFF") || file.empty())
			return std::nullopt;
		int index = 0;
		(void)fFc->PatternGetInteger(font, kIndex, 0, &index);
		// the upper 16 bits name an instance of a variable font
		return FontLocation{file, static_cast<std::uint32_t>(index) & 0xffffu};
	}

	mutable std::mutex fLock;
	const Fontconfig* fFc;
	FcConfig* fConfig;
};


} // namespace


std::expected<std::unique_ptr<FontSource>, Error>
CreateFontconfigSource(std::string_view configFile)
{
	const Fontconfig* functions = Library();
	if (functions == nullptr)
		return Failure(ErrorCode::NotSupported, "libfontconfig.so.1 cannot be loaded");
	FcConfig* config = nullptr;
	if (configFile.empty())
		config = functions->InitLoadConfigAndFonts();
	else {
		config = functions->ConfigCreate();
		const std::string file(configFile);
		if (config != nullptr
			&& (!functions->ConfigParseAndLoad(config,
					reinterpret_cast<const FcChar8*>(file.c_str()), 1)
				|| !functions->ConfigBuildFonts(config))) {
			functions->ConfigDestroy(config);
			config = nullptr;
		}
	}
	if (config == nullptr)
		return Failure(ErrorCode::IoError, "fontconfig cannot read its configuration");
	return std::make_unique<FontconfigSource>(functions, config);
}


#else


std::expected<std::unique_ptr<FontSource>, Error>
CreateFontconfigSource(std::string_view)
{
	return Failure(ErrorCode::NotSupported, "no fontconfig on this system");
}


#endif


std::vector<SourceFont>
FontSource::List() const
{
	return {};
}


std::unique_ptr<FontSource>
SystemFontSource()
{
	if (auto fontconfig = CreateFontconfigSource())
		return std::move(*fontconfig);
	return nullptr;
}


} // namespace ngui::text
