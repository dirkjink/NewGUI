/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/MimeType.h>

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>


namespace {


using namespace ngui;


TEST(MimeTypeTest, HaikusRulesForATypeHold)
{
	// What is tested: Strings as MIME types: "image/png", the supertype
	// "image", a type in other case, types with a second slash, a slash at
	// either end, a space, a separator (;), a control character and
	// non-ASCII, the empty one, 239 and 240 characters; then the supertype,
	// Contains() and equality of the valid ones.
	// Success criteria: As Haiku's BMimeType: the valid ones hold their type,
	// the others none, with InitCheck() failing; 239 characters are the
	// most. "image" is a supertype only and has none itself; "image/png" has
	// "image", which contains it and itself but not "text/plain"; types
	// equal without regard to case.
	// Why it matters: Applications pass types from files and messages;
	// NewGUI must accept and compare them as Haiku does.
	//
	// Given
	const std::string longest = "a/" + std::string(237, 'b');

	// Then
	for (const std::string& valid : {std::string("image/png"), std::string("image"),
			std::string("Image/PNG"), std::string("application/x-vnd.Be-elfexecutable"),
			longest}) {
		SCOPED_TRACE(valid);
		EXPECT_TRUE(BMimeType::IsValid(valid));
		EXPECT_TRUE(BMimeType(valid).InitCheck().has_value());
		EXPECT_EQ(BMimeType(valid).Type(), valid);
	}
	for (const std::string& invalid : {std::string("a/b/c"), std::string("/png"),
			std::string("image/"), std::string("image/p ng"), std::string("image/png;q=1"),
			std::string("image/\x01"), std::string("bild/\xc3\xa4"), std::string(),
			longest + "c"}) {
		SCOPED_TRACE(invalid);
		EXPECT_FALSE(BMimeType::IsValid(invalid));
		BMimeType type;
		EXPECT_FALSE(type.SetTo(invalid).has_value());
		EXPECT_FALSE(type.IsValid());
		EXPECT_FALSE(type.InitCheck().has_value());
		EXPECT_TRUE(type.Type().empty());
	}

	const BMimeType png("image/png");
	const BMimeType image("image");
	EXPECT_TRUE(image.IsSupertypeOnly());
	EXPECT_FALSE(png.IsSupertypeOnly());
	EXPECT_FALSE(image.GetSupertype().has_value());
	ASSERT_TRUE(png.GetSupertype().has_value());
	EXPECT_EQ(*png.GetSupertype(), image);
	EXPECT_TRUE(image.Contains(png));
	EXPECT_TRUE(png.Contains(png));
	EXPECT_FALSE(png.Contains(image));
	EXPECT_FALSE(image.Contains(BMimeType("text/plain")));
	EXPECT_EQ(png, BMimeType("IMAGE/Png"));
	EXPECT_EQ(png, "image/PNG");
	EXPECT_FALSE(png == "image/gif");
	EXPECT_EQ(BMimeType(), BMimeType());
	EXPECT_FALSE(BMimeType().InitCheck().has_value());
}


TEST(MimeTypeTest, TheDatabaseIsHaikus)
{
	// What is tested: What the database says of image/png, of
	// text/x-source-code, of a supertype (image), of a type in other case
	// and of a type it does not know (application/x-unknown).
	// Success criteria: As in Haiku's src/data/mime_db: image/png is
	// installed, "PNG image", extension png; source code has cpp and h among
	// its extensions; image is "Image file"; IMAGE/PNG is found too; the
	// unknown type is not installed, without description or extensions.
	// Why it matters: The descriptions and extensions are what a file list
	// shows and what guessing a file's type relies on; they are Haiku's own.
	//
	// Given
	const BMimeType png("image/png");
	const BMimeType source("text/x-source-code");
	const BMimeType unknown("application/x-unknown");

	// When
	const std::vector<std::string> sourceExtensions = source.FileExtensions();

	// Then
	EXPECT_TRUE(png.IsInstalled());
	EXPECT_EQ(png.ShortDescription(), "PNG image");
	EXPECT_EQ(png.FileExtensions(), std::vector<std::string>{"png"});
	EXPECT_NE(std::ranges::find(sourceExtensions, "cpp"), sourceExtensions.end());
	EXPECT_NE(std::ranges::find(sourceExtensions, "h"), sourceExtensions.end());
	EXPECT_EQ(BMimeType("image").ShortDescription(), "Image file");
	EXPECT_EQ(BMimeType("IMAGE/PNG").ShortDescription(), "PNG image");
	EXPECT_FALSE(unknown.IsInstalled());
	EXPECT_TRUE(unknown.ShortDescription().empty());
	EXPECT_TRUE(unknown.FileExtensions().empty());
	EXPECT_FALSE(BMimeType().IsInstalled());
}


TEST(MimeTypeTest, TheNameGivesTheType)
{
	// What is tested: GuessMimeType() for names with known extensions in
	// other case, in paths with / and \, with two extensions, a name that
	// is only a dot and an extension, a dot in the folder only, a trailing
	// dot, no extension, an unknown extension.
	// Success criteria: PNG, PDF and tar files get their types; the last
	// extension counts (archive.tar.gz is gzip); ".png" is a PNG image, as
	// Haiku takes what follows the last dot; everything else is
	// application/octet-stream, Haiku's type of a file of no known type.
	// Why it matters: NewGUI knows a file's type by its name only (no
	// attributes, no sniffing); its icon and description follow from it.
	//
	// Then
	EXPECT_EQ(BMimeType::GuessMimeType("photo.PNG"), "image/png");
	EXPECT_EQ(BMimeType::GuessMimeType("/home/user/backup.tar"), "application/x-tar");
	EXPECT_EQ(BMimeType::GuessMimeType("C:\\Users\\me\\paper.pdf"), "application/pdf");
	EXPECT_EQ(BMimeType::GuessMimeType("archive.tar.gz"), "application/x-gzip");
	EXPECT_EQ(BMimeType::GuessMimeType(".png"), "image/png");
	for (const char* none : {"folder.png/notes", "file.", "README", "data.xyz12", ""}) {
		SCOPED_TRACE(none);
		EXPECT_EQ(BMimeType::GuessMimeType(none), kFileMimeType);
	}
}


} // namespace
