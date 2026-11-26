/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Catalog.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/LocaleRoster.h>

#include "locale/Languages.h"
#include "locale/PluralForms.h"
#include "support/MoFile.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "CatalogTest"


namespace {


using namespace ngui;
using test::MoFile;


constexpr const char* kGermanHeader = "Project-Id-Version: test\n"
	"Language: de\n"
	"Content-Type: text/plain; charset=UTF-8\n"
	"Plural-Forms: nplurals=2; plural=(n != 1);\n";


std::shared_ptr<const BCatalog>
Catalog(const std::vector<std::byte>& file)
{
	auto catalog = BCatalog::FromData(file);
	EXPECT_TRUE(catalog.has_value()) << (catalog ? "" : catalog.error().message);
	return catalog.has_value() ? std::move(*catalog) : BCatalog::Pseudo();
}


TEST(CatalogTest, TranslationsComeFromTheMoFile)
{
	// What is tested: A German .mo file with "Open", "Open" in the context
	// "Menu" and the header, read in little and in big endian: GetString()
	// with and without contexts, of a text it lacks; Language() and
	// CountItems().
	// Success criteria: "Open" is "Öffnen"; in "Menu" "Öffnen…"; in a
	// context it lacks the translation without context; a text it lacks
	// comes back as the same pointer. Both byte orders read alike; the
	// language is "de", two texts.
	// Why it matters: Applications are translated by gettext catalogs, as
	// with Haiku's BCatalog (module G7).
	//
	// Given
	const std::vector<std::pair<std::string, std::string>> entries = {{"", kGermanHeader},
		{"Open", "Öffnen"}, {"Menu\x04Open", "Öffnen…"}};
	const auto little = Catalog(MoFile(entries));
	const auto big = Catalog(MoFile(entries, true));
	const char* close = "Close";

	// When / Then
	for (const auto& catalog : {little, big}) {
		EXPECT_STREQ(catalog->GetString("Open"), "Öffnen");
		EXPECT_STREQ(catalog->GetString("Open", "Menu"), "Öffnen…");
		EXPECT_STREQ(catalog->GetString("Open", "Toolbar"), "Öffnen");
		EXPECT_EQ(catalog->GetString(close), close);
		EXPECT_EQ(catalog->Language(), "de");
		EXPECT_EQ(catalog->CountItems(), 2u);
	}
}


TEST(CatalogTest, ItReadsWhatMsgfmtWrites)
{
	// What is tested: The demo's German catalog as GNU msgfmt wrote it
	// (examples/button/locale/de/LC_MESSAGES/newgui-demo.mo, from
	// examples/button/po/de.po, little endian, with msgfmt's hash table,
	// which the reader does not need): texts in the context "Demo", one
	// with an ellipsis and one with line breaks, and the plural message.
	// Success criteria: "Quit" in "Demo" is "Beenden", "Open…" "Öffnen…",
	// the about text has its line breaks; "Clicked %d time(s)" is "%d-mal
	// geklickt" for 1 and 5; 50 texts, language "de".
	// Why it matters: Catalogs come from gettext's own tools, not from
	// NewGUI's tests (module G7).
	//
	// Given
	const auto catalog = BCatalog::Load(NGUI_DEMO_CATALOG);

	// When / Then
	ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
	EXPECT_STREQ((*catalog)->GetString("Quit", "Demo"), "Beenden");
	EXPECT_STREQ((*catalog)->GetString("Open\u2026", "Demo"), "Öffnen\u2026");
	EXPECT_STREQ((*catalog)->GetString("NewGUI\n\nHaiku's Interface Kit on SDL3 and Skia.",
		"Demo"), "NewGUI\n\nHaikus Interface Kit auf SDL3 und Skia.");
	EXPECT_STREQ((*catalog)->GetPluralString("Clicked %d time", "Clicked %d times", 1, "Demo"),
		"%d-mal geklickt");
	EXPECT_STREQ((*catalog)->GetPluralString("Clicked %d time", "Clicked %d times", 5, "Demo"),
		"%d-mal geklickt");
	EXPECT_EQ((*catalog)->CountItems(), 50u);
	EXPECT_EQ((*catalog)->Language(), "de");
}


TEST(CatalogTest, PluralsFollowTheCatalogsRule)
{
	// What is tested: Plural messages "%d file(s)" in Polish (three forms
	// by its gettext rule), in Arabic (six forms), in German without a
	// Plural-Forms header, untranslated, and with fewer forms than the rule;
	// broken rules.
	// Success criteria: Polish: 1 plik, 2 and 22 pliki, 5 and 12 plików;
	// for Arabic 0, 1, 2, 3, 11, 100 take forms 0 to 5; German without a rule
	// takes n != 1; an untranslated message the source by n; a missing
	// form the source. A rule with an unfinished formula, too many forms
	// or a nesting of 100 levels is BadData; division by zero is 0.
	// Why it matters: Counts read right only in each language's own forms
	// (gettext's ngettext).
	//
	// Given
	const auto polish = Catalog(MoFile({{"", "Language: pl\nPlural-Forms: nplurals=3; "
		"plural=(n==1 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);\n"},
		{std::string("%d file\0%d files", 16), std::string("%d plik\0%d pliki\0%d plików", 27)}}));
	const auto arabic = Catalog(MoFile({{"", "Plural-Forms: nplurals=6; plural=n==0 ? 0 : "
		"n==1 ? 1 : n==2 ? 2 : n%100>=3 && n%100<=10 ? 3 : n%100>=11 ? 4 : 5;\n"},
		{std::string("x\0y", 3), std::string("0\0" "1\0" "2\0" "3\0" "4\0" "5", 11)}}));
	const auto german = Catalog(MoFile({{"", "Language: de\n"},
		{std::string("%d file\0%d files", 16), std::string("%d Datei\0%d Dateien", 19)},
		{std::string("a\0b", 3), std::string("A", 1)}}));

	// When / Then
	EXPECT_STREQ(polish->GetPluralString("%d file", "%d files", 1), "%d plik");
	EXPECT_STREQ(polish->GetPluralString("%d file", "%d files", 2), "%d pliki");
	EXPECT_STREQ(polish->GetPluralString("%d file", "%d files", 22), "%d pliki");
	EXPECT_STREQ(polish->GetPluralString("%d file", "%d files", 5), "%d plików");
	EXPECT_STREQ(polish->GetPluralString("%d file", "%d files", 12), "%d plików");
	const std::uint64_t counts[] = {0, 1, 2, 3, 11, 100};
	for (std::size_t i = 0; i < std::size(counts); i++)
		EXPECT_EQ(arabic->GetPluralString("x", "y", counts[i]), std::to_string(i));
	EXPECT_STREQ(german->GetPluralString("%d file", "%d files", 1), "%d Datei");
	EXPECT_STREQ(german->GetPluralString("%d file", "%d files", 0), "%d Dateien");
	EXPECT_STREQ(german->GetPluralString("a", "b", 2), "b");
	EXPECT_STREQ(german->GetPluralString("dog", "dogs", 1), "dog");
	EXPECT_STREQ(german->GetPluralString("dog", "dogs", 3), "dogs");
	EXPECT_FALSE(locale::PluralForms::Parse("nplurals=2; plural=n !=").has_value());
	EXPECT_FALSE(locale::PluralForms::Parse("nplurals=17; plural=n;").has_value());
	EXPECT_FALSE(locale::PluralForms::Parse("nplurals=2; plural=" + std::string(100, '(')
		+ "n" + std::string(100, ')') + ";").has_value());
	const auto zero = locale::PluralForms::Parse("nplurals=2; plural=n/0 + n%0;");
	ASSERT_TRUE(zero.has_value());
	EXPECT_EQ(zero->FormFor(7), 0u);
}


TEST(CatalogTest, BrokenFilesAreRejected)
{
	// What is tested: FromData() of bytes too short, of a wrong magic
	// number, of revision 2, with a table beyond the end, with a string
	// beyond the end; Load() of a file that is not there.
	// Success criteria: BadData for each broken file, IoError for the
	// missing one; nothing is read outside the bytes (AddressSanitizer).
	// Why it matters: Catalogs are files on disk that may be damaged
	// (and the reader has a fuzz target).
	//
	// Given
	std::vector<std::byte> valid = MoFile({{"Open", "Öffnen"}});
	std::vector<std::byte> magic = valid;
	magic[0] = std::byte{0};
	std::vector<std::byte> revision = valid;
	revision[6] = std::byte{2};
	std::vector<std::byte> table = valid;
	table[8] = std::byte{0xff};
	std::vector<std::byte> string = valid;
	string[32] = std::byte{0xff};
	string[33] = std::byte{0xff};

	// When / Then
	for (const auto& broken : {std::vector<std::byte>(10), magic, revision, table, string}) {
		const auto catalog = BCatalog::FromData(broken);
		ASSERT_FALSE(catalog.has_value());
		EXPECT_EQ(catalog.error().code, ErrorCode::BadData);
	}
	const auto missing = BCatalog::Load("/nonexistent/catalog.mo");
	ASSERT_FALSE(missing.has_value());
	EXPECT_EQ(missing.error().code, ErrorCode::IoError);
}


TEST(CatalogTest, PseudoLocalizationLengthensAndKeepsPlaceholders)
{
	// What is tested: The pseudo catalog's translation of "Open %d files
	// with {app}" and of a plural message.
	// Success criteria: The text is in brackets, its letters accented, at
	// least a third longer in characters; "%d" and "{app}" stay as they
	// are. The plural form follows English: the singular for 1. The same
	// text gives the same pointer again.
	// Why it matters: Pseudo-localization shows untranslated texts and
	// layouts too tight for longer languages (specification 20.6).
	//
	// Given
	const auto pseudo = BCatalog::Pseudo();

	// When
	const std::string text = pseudo->GetString("Open %d files with {app}");
	const std::string one = pseudo->GetPluralString("%d file", "%d files", 1);

	// Then
	EXPECT_EQ(pseudo->Language(), "x-pseudo");
	EXPECT_TRUE(text.starts_with("[Ó"));
	EXPECT_TRUE(text.ends_with("~]"));
	EXPECT_NE(text.find("%d"), std::string::npos);
	EXPECT_NE(text.find("{app}"), std::string::npos);
	EXPECT_EQ(text.find("files"), std::string::npos);
	EXPECT_EQ(one.find("files"), std::string::npos);
	EXPECT_NE(one.find("%d"), std::string::npos);
	EXPECT_EQ(pseudo->GetString("Open"), pseudo->GetString("Open"));
}


TEST(CatalogTest, TheRosterFindsTheCatalogOfTheUsersLanguage)
{
	// What is tested: The languages of environment variables; a roster
	// with the preferred language "de-DE" loading the domain "demo" from a
	// directory with de/LC_MESSAGES/demo.mo; B_TRANSLATE() and
	// B_TRANSLATE_PLURAL() with it; then "x-pseudo", "en_US" and "ja".
	// Success criteria: NGUI_LANGUAGE wins; LANGUAGE counts unless the
	// locale is "C"; "de_DE.UTF-8@euro" becomes de_DE, then de. "de-DE"
	// finds the "de" catalog; B_TRANSLATE() translates in this file's
	// context or without one, B_TRANSLATE_PLURAL() by German forms.
	// "x-pseudo" gives the pseudo catalog, "en_US" none without an error
	// (the texts are English), "ja" none and NotFound.
	// Why it matters: The user's language decides the catalog, as gettext
	// and Haiku's BLocaleRoster do (module G7).
	//
	// Given
	const std::filesystem::path directory = std::filesystem::temp_directory_path()
		/ ("ngui-catalog-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
	std::filesystem::create_directories(directory / "de" / "LC_MESSAGES");
	{
		const std::vector<std::byte> file = MoFile({{"", kGermanHeader},
			{"CatalogTest\x04Quit", "Beenden"}, {"Reset", "Zurücksetzen"},
			{std::string("%d file\0%d files", 16), std::string("%d Datei\0%d Dateien", 19)}});
		std::ofstream out(directory / "de" / "LC_MESSAGES" / "demo.mo", std::ios::binary);
		out.write(reinterpret_cast<const char*>(file.data()),
			static_cast<std::streamsize>(file.size()));
	}
	const std::u8string path = directory.u8string();
	const std::vector<std::string> directories{std::string(path.begin(), path.end())};
	BLocaleRoster* roster = BLocaleRoster::Default();

	// When
	const auto fromNgui = locale::LanguagesFromEnvironment("x-pseudo", "fr", "", "", "de_DE");
	const auto fromLanguage = locale::LanguagesFromEnvironment("", "fr:it", "", "",
		"de_DE.UTF-8@euro");
	const auto inC = locale::LanguagesFromEnvironment("", "fr", "C", "", "de_DE");
	const auto fromLang = locale::LanguagesFromEnvironment("", "", "", "", "de_DE.UTF-8@euro");
	roster->SetPreferredLanguages({"de-DE"});
	const std::vector<std::string> preferred = roster->PreferredLanguages();
	const auto german = roster->LoadCatalog("demo", directories);
	const std::string quit = B_TRANSLATE("Quit");
	const std::string reset = B_TRANSLATE("Reset");
	const std::string other = B_TRANSLATE_CONTEXT("Quit", "Elsewhere");
	const std::string files = B_TRANSLATE_PLURAL("%d file", "%d files", 3);
	const std::string marked = B_TRANSLATE_MARK("Reset");
	roster->SetPreferredLanguages({"x-pseudo"});
	const auto pseudo = roster->LoadCatalog("demo", directories);
	const std::string pseudoLanguage = roster->Catalog()->Language();
	roster->SetPreferredLanguages({"en_US", "de"});
	const auto english = roster->LoadCatalog("demo", directories);
	const bool englishHasNone = roster->Catalog() == nullptr;
	roster->SetPreferredLanguages({"ja"});
	const auto japanese = roster->LoadCatalog("demo", directories);
	const std::string untranslated = B_TRANSLATE("Quit");
	roster->SetPreferredLanguages({});
	roster->SetCatalog(nullptr);
	std::error_code ignored;
	std::filesystem::remove_all(directory, ignored);

	// Then
	EXPECT_EQ(fromNgui, (std::vector<std::string>{"x-pseudo"}));
	EXPECT_EQ(fromLanguage, (std::vector<std::string>{"fr", "it"}));
	EXPECT_TRUE(inC.empty());
	EXPECT_EQ(fromLang, (std::vector<std::string>{"de_DE", "de"}));
	EXPECT_EQ(preferred, (std::vector<std::string>{"de_DE", "de"}));
	ASSERT_TRUE(german.has_value()) << german.error().message;
	EXPECT_EQ(quit, "Beenden");
	EXPECT_EQ(reset, "Zurücksetzen");
	EXPECT_EQ(other, "Quit");
	EXPECT_EQ(files, "%d Dateien");
	EXPECT_EQ(marked, "Reset");
	EXPECT_TRUE(pseudo.has_value());
	EXPECT_EQ(pseudoLanguage, "x-pseudo");
	EXPECT_TRUE(english.has_value());
	EXPECT_TRUE(englishHasNone);
	ASSERT_FALSE(japanese.has_value());
	EXPECT_EQ(japanese.error().code, ErrorCode::NotFound);
	EXPECT_EQ(untranslated, "Quit");
}


} // namespace
