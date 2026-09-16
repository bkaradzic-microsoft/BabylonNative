#include <Babylon/Graphics/ImageFormat.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
    using Image = std::unique_ptr<bimg::ImageContainer, decltype(&bimg::imageFree)>;

    uint8_t HexNibble(char value)
    {
        return static_cast<uint8_t>(value <= '9' ? value - '0' : value - 'a' + 10);
    }

    std::vector<uint8_t> DecodeHex(std::string_view hex)
    {
        std::vector<uint8_t> bytes(hex.size() / 2);
        for (size_t index = 0; index < bytes.size(); ++index)
        {
            bytes[index] = static_cast<uint8_t>((HexNibble(hex[index * 2]) << 4) | HexNibble(hex[index * 2 + 1]));
        }
        return bytes;
    }

    Image ParsePng(bx::AllocatorI& allocator, std::string_view hex)
    {
        const auto bytes = DecodeHex(hex);
        return Image{bimg::imageParse(&allocator, bytes.data(), static_cast<uint32_t>(bytes.size())), bimg::imageFree};
    }

    void ExpectKeyedGrayscale8(
        bx::AllocatorI& allocator,
        std::string_view hex,
        uint32_t width,
        std::initializer_list<uint8_t> expectedColor,
        std::initializer_list<uint8_t> expectedAlpha)
    {
        Image image{ParsePng(allocator, hex)};
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->m_format, bimg::TextureFormat::RG8);
        EXPECT_TRUE(image->m_hasAlpha);
        ASSERT_NE(width, 0);
        ASSERT_EQ(expectedColor.size(), expectedAlpha.size());
        ASSERT_EQ(image->m_width, width);
        ASSERT_EQ(image->m_height, expectedColor.size() / width);
        ASSERT_EQ(image->m_size, expectedColor.size() * 2);

        const auto* actual = static_cast<const uint8_t*>(image->m_data);
        auto color = expectedColor.begin();
        auto alpha = expectedAlpha.begin();
        for (size_t index = 0; index < expectedColor.size(); ++index, ++color, ++alpha)
        {
            EXPECT_EQ(actual[index * 2], *color) << index;
            EXPECT_EQ(actual[index * 2 + 1], *alpha) << index;
        }
    }
}

TEST(NativeEngineImageFormats, Png16UsesBrowserHalfFloatQuantization)
{
    bx::DefaultAllocator allocator;
    const std::array<uint16_t, 16> pixels{
        32767, 56917, 58478, 65535,
        0, 65535, 32768, 32767,
        56917, 58478, 0, 0,
        65535, 1, 65534, 56917};
    auto* source = bimg::imageAlloc(&allocator, bimg::TextureFormat::RGBA16, 4, 1, 0, 1, false, false, pixels.data());
    ASSERT_NE(source, nullptr);
    source->m_parser = bimg::ImageParser::Png;
    source->m_hasAlpha = true;
    Image image{Babylon::Graphics::NormalizePngImage(allocator, source), bimg::imageFree};
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->m_format, bimg::TextureFormat::RGBA8);
    EXPECT_EQ(image->m_parser, bimg::ImageParser::Png);
    EXPECT_TRUE(image->m_hasAlpha);
    const std::array<uint8_t, 16> expected{
        128, 222, 227, 255,
        0, 255, 128, 128,
        222, 227, 0, 0,
        255, 0, 255, 222};
    ASSERT_EQ(image->m_size, expected.size());
    const auto* actual = static_cast<const uint8_t*>(image->m_data);
    for (size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(actual[index], expected[index]) << index;
    }
}

TEST(NativeEngineImageFormats, Png16GrayscaleReplicatesColorAndPreservesAlpha)
{
    bx::DefaultAllocator allocator;
    for (const auto format : {bimg::TextureFormat::R16, bimg::TextureFormat::RG16})
    {
        const std::array<uint16_t, 2> pixels{32767, 56917};
        auto* source = bimg::imageAlloc(&allocator, format, 1, 1, 0, 1, false, false, pixels.data());
        ASSERT_NE(source, nullptr);
        source->m_parser = bimg::ImageParser::Png;
        Image image{Babylon::Graphics::NormalizePngImage(allocator, source), bimg::imageFree};
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->m_format, bimg::TextureFormat::RGBA8);
        const auto* actual = static_cast<const uint8_t*>(image->m_data);
        EXPECT_EQ(actual[0], 128);
        EXPECT_EQ(actual[1], 128);
        EXPECT_EQ(actual[2], 128);
        EXPECT_EQ(actual[3], format == bimg::TextureFormat::RG16 ? 222 : 255);
    }
}

TEST(NativeEngineImageFormats, PreservesOrdinaryAndAuthoredHighPrecisionFormats)
{
    bx::DefaultAllocator allocator;
    for (const auto parser : {bimg::ImageParser::Png, bimg::ImageParser::Dds, bimg::ImageParser::Exr, bimg::ImageParser::Count})
    {
        for (const auto format : {bimg::TextureFormat::RGBA8, bimg::TextureFormat::RGBA16, bimg::TextureFormat::RGBA16F, bimg::TextureFormat::RGBA32F})
        {
            if (parser == bimg::ImageParser::Png && format == bimg::TextureFormat::RGBA16)
            {
                continue;
            }
            auto* source = bimg::imageAlloc(&allocator, format, 4, 4, 0, 1, false, true);
            ASSERT_NE(source, nullptr);
            source->m_parser = parser;
            Image image{Babylon::Graphics::NormalizePngImage(allocator, source), bimg::imageFree};
            EXPECT_EQ(image.get(), source);
            EXPECT_EQ(image->m_format, format);
            EXPECT_EQ(image->m_numMips, 3);
        }
    }
}

TEST(NativeEngineImageFormats, Png8GrayscaleReplicatesColorAndPreservesAlpha)
{
    bx::DefaultAllocator allocator;
    for (const auto format : {bimg::TextureFormat::R8, bimg::TextureFormat::RG8})
    {
        const std::array<uint8_t, 2> pixels{127, 64};
        auto* source = bimg::imageAlloc(&allocator, format, 1, 1, 0, 1, false, false, pixels.data());
        ASSERT_NE(source, nullptr);
        source->m_parser = bimg::ImageParser::Png;
        source->m_hasAlpha = format == bimg::TextureFormat::RG8;
        Image image{Babylon::Graphics::NormalizePngImage(allocator, source), bimg::imageFree};
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->m_format, bimg::TextureFormat::RGBA8);
        const auto* actual = static_cast<const uint8_t*>(image->m_data);
        EXPECT_EQ(actual[0], 127);
        EXPECT_EQ(actual[1], 127);
        EXPECT_EQ(actual[2], 127);
        EXPECT_EQ(actual[3], format == bimg::TextureFormat::RG8 ? 64 : 255);
        EXPECT_EQ(image->m_hasAlpha, format == bimg::TextureFormat::RG8);
    }
}

TEST(NativeEngineImageFormats, PngTransparencyKeyExpandsPackedGrayscaleOddWidths)
{
    bx::DefaultAllocator allocator;

    ExpectKeyedGrayscale8(
        allocator,
        "89504e470d0a1a0a0000000d4948445200000005000000020100000000b8112bf00000000274524e5300007693cd380000000c4944415478da63886058000001ac00f90e3932ac0000000049454e44ae426082",
        5,
        {0, 255, 0, 255, 255, 255, 0, 255, 0, 0},
        {0, 255, 0, 255, 255, 255, 0, 255, 0, 0});
    ExpectKeyedGrayscale8(
        allocator,
        "89504e470d0a1a0a0000000d4948445200000005000000020200000000ffb151200000000274524e530002989dac140000000e4944415478da6390766098dc00000333016f84c69eff0000000049454e44ae426082",
        5,
        {0, 85, 170, 255, 85, 170, 85, 0, 255, 170},
        {255, 255, 0, 255, 255, 0, 255, 255, 255, 0});
    ExpectKeyedGrayscale8(
        allocator,
        "89504e470d0a1a0a0000000d49484452000000030000000204000000007defd4c70000000274524e530007e8f7589b0000000e4944415478da6360ffc050f8000005ab02494e4b78130000000049454e44ae426082",
        3,
        {0, 119, 255, 119, 17, 238},
        {255, 0, 255, 0, 255, 255});
}

TEST(NativeEngineImageFormats, PngTransparencyKeyExpandsGrayscale8)
{
    bx::DefaultAllocator allocator;
    ExpectKeyedGrayscale8(
        allocator,
        "89504e470d0a1a0a0000000d49484452000000030000000108000000003e8b4b680000000274524e53007fb629a1950000000c4944415478da6360a8ff0f000201017f8b1b64610000000049454e44ae426082",
        3,
        {0, 127, 255},
        {255, 0, 255});
}

TEST(NativeEngineImageFormats, PngTransparencyKeyMatchesGrayscale16BeforeQuantization)
{
    bx::DefaultAllocator allocator;
    Image image{ParsePng(
        allocator,
        "89504e470d0a1a0a0000000d49484452000000030000000110000000006e1b972b0000000274524e5312342fd3495e0000000f4944415478da63103211325d7d160004810206a42be1790000000049454e44ae426082")};
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->m_format, bimg::TextureFormat::RG16);
    EXPECT_TRUE(image->m_hasAlpha);
    ASSERT_EQ(image->m_size, 12);
    const auto* actual = static_cast<const uint16_t*>(image->m_data);
    const std::array<uint16_t, 6> expected{0x1234, 0, 0x1235, UINT16_MAX, 0xabcd, UINT16_MAX};
    for (size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(actual[index], expected[index]) << index;
    }

    Image converted{Babylon::Graphics::NormalizePngImage(allocator, image.release()), bimg::imageFree};
    ASSERT_NE(converted, nullptr);
    const auto* rgba = static_cast<const uint8_t*>(converted->m_data);
    EXPECT_EQ(rgba[0], rgba[4]);
    EXPECT_EQ(rgba[3], 0);
    EXPECT_EQ(rgba[7], UINT8_MAX);
}

TEST(NativeEngineImageFormats, PngTransparencyKeyExpandsRgb8)
{
    bx::DefaultAllocator allocator;
    Image image{ParsePng(
        allocator,
        "89504e470d0a1a0a0000000d4948445200000003000000010802000000948283e30000000674524e530000000000006ea607910000000f4944415478da63600001462e1139000072003ec06106a20000000049454e44ae426082")};
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->m_format, bimg::TextureFormat::RGBA8);
    EXPECT_TRUE(image->m_hasAlpha);
    const std::array<uint8_t, 12> expected{
        0, 0, 0, 0,
        0, 0, 1, UINT8_MAX,
        10, 20, 30, UINT8_MAX};
    ASSERT_EQ(image->m_size, expected.size());
    const auto* actual = static_cast<const uint8_t*>(image->m_data);
    for (size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(actual[index], expected[index]) << index;
    }
}

TEST(NativeEngineImageFormats, PngTransparencyKeyMatchesRgb16BeforeQuantization)
{
    bx::DefaultAllocator allocator;
    Image image{ParsePng(
        allocator,
        "89504e470d0a1a0a0000000d4948445200000003000000011002000000c4125fa00000000674524e53123456789abc89e44ee60000001c4944415478da63103209ab98b547c834ac72d6ded5675532fedd0100448508b66978744c0000000049454e44ae426082")};
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->m_format, bimg::TextureFormat::RGBA16);
    EXPECT_TRUE(image->m_hasAlpha);
    const std::array<uint16_t, 12> expected{
        0x1234, 0x5678, 0x9abc, 0,
        0x1235, 0x5679, 0x9abd, UINT16_MAX,
        0xabcd, 0x2468, 0xfedc, UINT16_MAX};
    ASSERT_EQ(image->m_size, expected.size() * sizeof(uint16_t));
    const auto* actual = static_cast<const uint16_t*>(image->m_data);
    for (size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(actual[index], expected[index]) << index;
    }

    Image converted{Babylon::Graphics::NormalizePngImage(allocator, image.release()), bimg::imageFree};
    ASSERT_NE(converted, nullptr);
    const auto* rgba = static_cast<const uint8_t*>(converted->m_data);
    EXPECT_EQ(rgba[0], rgba[4]);
    EXPECT_EQ(rgba[1], rgba[5]);
    EXPECT_EQ(rgba[2], rgba[6]);
    EXPECT_EQ(rgba[3], 0);
    EXPECT_EQ(rgba[7], UINT8_MAX);
}

TEST(NativeEngineImageFormats, PngWithoutTransparencyKeyKeepsNativeFormats)
{
    bx::DefaultAllocator allocator;
    Image grayscale{ParsePng(
        allocator,
        "89504e470d0a1a0a0000000d49484452000000030000000108000000003e8b4b680000000c4944415478da6360a8ff0f000201017f8b1b64610000000049454e44ae426082")};
    ASSERT_NE(grayscale, nullptr);
    EXPECT_EQ(grayscale->m_format, bimg::TextureFormat::R8);
    EXPECT_FALSE(grayscale->m_hasAlpha);
    ASSERT_EQ(grayscale->m_size, 3);
    const auto* gray = static_cast<const uint8_t*>(grayscale->m_data);
    EXPECT_EQ(gray[0], 0);
    EXPECT_EQ(gray[1], 127);
    EXPECT_EQ(gray[2], 255);

    Image rgb{ParsePng(
        allocator,
        "89504e470d0a1a0a0000000d49484452000000020000000108020000007b40e8dd0000000f4944415478da63606060e012910300006b003d4c4545fc0000000049454e44ae426082")};
    ASSERT_NE(rgb, nullptr);
    EXPECT_EQ(rgb->m_format, bimg::TextureFormat::RGB8);
    EXPECT_FALSE(rgb->m_hasAlpha);
    const std::array<uint8_t, 6> expected{0, 0, 0, 10, 20, 30};
    ASSERT_EQ(rgb->m_size, expected.size());
    const auto* actual = static_cast<const uint8_t*>(rgb->m_data);
    for (size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(actual[index], expected[index]) << index;
    }
}

TEST(NativeEngineImageFormats, PngPaletteAndPerPixelAlphaPathsRemainUnchanged)
{
    bx::DefaultAllocator allocator;
    Image palette{ParsePng(
        allocator,
        "89504e470d0a1a0a0000000d49484452000000030000000108030000002c3ee48600000009504c5445010203040506070809258556f00000000374524e530080ffecf7b3180000000c4944415478da63606064020000080004081d630a0000000049454e44ae426082")};
    ASSERT_NE(palette, nullptr);
    EXPECT_EQ(palette->m_format, bimg::TextureFormat::RGBA8);
    EXPECT_TRUE(palette->m_hasAlpha);
    const std::array<uint8_t, 12> expectedPalette{
        1, 2, 3, 0,
        4, 5, 6, 128,
        7, 8, 9, UINT8_MAX};
    ASSERT_EQ(palette->m_size, expectedPalette.size());
    const auto* palettePixels = static_cast<const uint8_t*>(palette->m_data);
    for (size_t index = 0; index < expectedPalette.size(); ++index)
    {
        EXPECT_EQ(palettePixels[index], expectedPalette[index]) << index;
    }

    Image rgba{ParsePng(
        allocator,
        "89504e470d0a1a0a0000000d4948445200000002000000010806000000f4227f8a000000114944415478da63606462666061656b000000dc0096e861a3670000000049454e44ae426082")};
    ASSERT_NE(rgba, nullptr);
    EXPECT_EQ(rgba->m_format, bimg::TextureFormat::RGBA8);
    EXPECT_TRUE(rgba->m_hasAlpha);
    const std::array<uint8_t, 8> expectedRgba{1, 2, 3, 0, 4, 5, 6, 128};
    ASSERT_EQ(rgba->m_size, expectedRgba.size());
    const auto* rgbaPixels = static_cast<const uint8_t*>(rgba->m_data);
    for (size_t index = 0; index < expectedRgba.size(); ++index)
    {
        EXPECT_EQ(rgbaPixels[index], expectedRgba[index]) << index;
    }
}
