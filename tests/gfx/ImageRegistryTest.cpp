/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/ImageRegistry.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/DumpBackend.h>


namespace {


using namespace ngui::gfx;


// Records which images a backend was asked to create and release.
class CountingBackend : public DumpBackend {
public:
			ImageHandle			CreateImage(const PixelData& pixels) override
								{
									created++;
									lastWidth = pixels.width;
									return 100 + created;
								}
			void				ReleaseImage(ImageHandle image) override
								{
									released.push_back(image);
								}

			int					created = 0;
			std::int32_t		lastWidth = 0;
			std::vector<ImageHandle> released;
};


std::vector<std::uint8_t>
Pixels(std::size_t count)
{
	return std::vector<std::uint8_t>(count, 0x7f);
}


TEST(ImageRegistryTest, PixelDataIsChecked)
{
	// What is tested: PixelData::IsValid() and Register() for valid data,
	// a stride with padding, a short last row, and invalid data.
	// Success criteria: Positive extent, a stride of at least width * 4 and
	// enough bytes are accepted; zero width, a too small stride, too few
	// bytes and a stride that would overflow are rejected.
	// Why it matters: Backends read width, height and stride of the pixels
	// they are given (specification 9); a wrong length is an out-of-bounds
	// read.
	//
	// Given
	const std::vector<std::uint8_t> bytes = Pixels(64);
	ImageRegistry registry;

	// When / Then
	EXPECT_TRUE((PixelData{2, 2, 8, bytes}).IsValid());
	EXPECT_TRUE((PixelData{2, 2, 32, std::span(bytes).first(40)}).IsValid());
	EXPECT_FALSE((PixelData{0, 2, 8, bytes}).IsValid());
	EXPECT_FALSE((PixelData{2, 2, 4, bytes}).IsValid());
	EXPECT_FALSE((PixelData{4, 4, 16, std::span(bytes).first(63)}).IsValid());
	EXPECT_FALSE((PixelData{1, 3, std::numeric_limits<std::size_t>::max() / 2, bytes}).IsValid());
	EXPECT_EQ(registry.Register({0, 1, 0, bytes}).error().code,
		ngui::ErrorCode::InvalidArgument);
}


TEST(ImageRegistryTest, ReferencesKeepImagesAlive)
{
	// What is tested: Register(), Retain(), Release() and ReferenceCount()
	// of two images, and unknown handles.
	// Success criteria: Handles are unique and non-zero; an image lives until
	// its last reference is given back; unknown handles are refused by
	// Retain() and ignored by Release().
	// Why it matters: Specification 9: an image is freed only when nothing
	// refers to it any more.
	//
	// Given
	ImageRegistry registry;
	const std::vector<std::uint8_t> bytes = Pixels(16);

	// When
	const ImageHandle a = *registry.Register({2, 2, 8, bytes});
	const ImageHandle b = *registry.Register({1, 1, 4, bytes});
	registry.Retain(a);

	// Then
	EXPECT_NE(a, 0u);
	EXPECT_NE(a, b);
	EXPECT_EQ(registry.ReferenceCount(a), 2u);
	EXPECT_EQ(registry.PixelSize(a), (Size{2, 2}));
	registry.Release(a);
	EXPECT_EQ(registry.ReferenceCount(a), 1u);
	registry.Release(a);
	EXPECT_EQ(registry.ReferenceCount(a), 0u);
	EXPECT_FALSE(registry.PixelSize(a).has_value());
	EXPECT_FALSE(registry.Retain(a));
	registry.Release(a);
	EXPECT_EQ(registry.ReferenceCount(b), 1u);
}


TEST(ImageRegistryTest, BackendResourcesFollowTheImage)
{
	// What is tested: BackendImage() for two backends, repeated, and the
	// last Release() of the image.
	// Success criteria: Each backend gets its resource once, on first use,
	// with the registered pixels; the last Release() frees it in every
	// backend; DetachBackend() makes the registry forget a backend.
	// Why it matters: Handles are frontend-wide and map to the resources of
	// each backend (specification 9), so switching the backend needs no
	// change in the Views.
	//
	// Given
	ImageRegistry registry;
	const std::vector<std::uint8_t> bytes = Pixels(36);
	CountingBackend first;
	CountingBackend second;
	const ImageHandle image = *registry.Register({3, 3, 12, bytes});

	// When
	const ImageHandle a = registry.BackendImage(image, first);
	const ImageHandle again = registry.BackendImage(image, first);
	const ImageHandle b = registry.BackendImage(image, second);
	registry.Release(image);

	// Then
	EXPECT_EQ(a, again);
	EXPECT_EQ(first.created, 1);
	EXPECT_EQ(first.lastWidth, 3);
	EXPECT_EQ(second.created, 1);
	EXPECT_EQ(first.released, (std::vector<ImageHandle>{a}));
	EXPECT_EQ(second.released, (std::vector<ImageHandle>{b}));
	EXPECT_EQ(registry.BackendImage(image, first), 0u);

	const ImageHandle other = *registry.Register({1, 1, 4, bytes});
	registry.BackendImage(other, first);
	registry.DetachBackend(first);
	registry.Release(other);
	EXPECT_EQ(first.released.size(), 1u);
}


TEST(ImageRegistryTest, DisplayListsHoldTheirImages)
{
	// What is tested: A list drawing a registered image, with the owner's
	// reference given back while the list exists, and a deserialized copy.
	// Success criteria: The list and its copy each hold one reference; the
	// image lives as long as either list exists and is freed with the last.
	// Why it matters: Specification 9: a DisplayList keeps the images it
	// uses alive; a View may drop its image right after drawing.
	//
	// Given
	ImageRegistry& registry = ImageRegistry::Default();
	const std::vector<std::uint8_t> bytes = Pixels(16);
	const ImageHandle image = *registry.Register({2, 2, 8, bytes});
	Canvas canvas({0, 0, 10, 10});
	canvas.DrawImage(image, {0, 0, 2, 2}, {0, 0, 4, 4});

	// When
	DisplayListRef list = canvas.Finish();
	registry.Release(image);
	auto copy = DisplayList::Deserialize(list->Serialize());

	// Then
	ASSERT_TRUE(copy.has_value());
	EXPECT_EQ(registry.ReferenceCount(image), 2u);
	list.reset();
	EXPECT_EQ(registry.ReferenceCount(image), 1u);
	copy->reset();
	EXPECT_EQ(registry.ReferenceCount(image), 0u);
}


} // namespace
