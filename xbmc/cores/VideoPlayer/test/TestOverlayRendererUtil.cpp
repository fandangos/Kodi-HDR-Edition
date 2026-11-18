/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "cores/VideoPlayer/VideoRenderers/OverlayRendererUtil.h"

#include "PlatformDefs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <ass/ass.h>
#include <gtest/gtest.h>

TEST(TestOverlayRendererUtil, BoundedPagesPreserveImages)
{
  constexpr int limit = 8;
  // Strided bitmap with no padding after the last row, larger than a page.
  std::vector<unsigned char> bitmap(23 * 16 + 19);
  for (size_t i = 0; i < bitmap.size(); ++i)
    bitmap[i] = i % 251;

  std::array<ASS_Image, 3> images{};
  for (size_t i = 0; i < images.size(); ++i)
  {
    images[i].w = i == 0 ? 19 : 7;
    images[i].h = i == 0 ? 17 : 7;
    images[i].stride = 23;
    images[i].bitmap = bitmap.data();
    images[i].color = i == 0 ? 0x12345600 : 0xabcdef30;
    images[i].dst_x = 30 * i;
    images[i].dst_y = 40 * i;
    images[i].next = i + 1 < images.size() ? &images[i + 1] : nullptr;
  }
  images.back().color = 0xffffffff;

  std::vector<OVERLAY::SQuads> pages;
  ASSERT_TRUE(OVERLAY::convert_quad_pages(images.data(), pages, limit));
  ASSERT_GT(pages.size(), 1u);
  std::array<std::vector<unsigned char>, 2> restored;
  std::array<std::vector<int>, 2> coverage;
  for (size_t i = 0; i < restored.size(); ++i)
  {
    restored[i].resize(images[i].w * images[i].h);
    coverage[i].resize(restored[i].size());
  }

  size_t previousImage = 0;
  for (const auto& page : pages)
  {
    ASSERT_GT(page.size_x, 0);
    ASSERT_GT(page.size_y, 0);
    ASSERT_LE(page.size_x, limit);
    ASSERT_LE(page.size_y, limit);
    ASSERT_EQ(page.texture.size(), static_cast<size_t>(page.size_x * page.size_y));
    for (const auto& quad : page.quad)
    {
      const size_t index = quad.r == 0x12 ? 0 : 1;
      EXPECT_GE(index, previousImage);
      previousImage = index;
      const auto& image = images[index];
      EXPECT_EQ(quad.r, (image.color >> 24) & 0xff);
      EXPECT_EQ(quad.g, (image.color >> 16) & 0xff);
      EXPECT_EQ(quad.b, (image.color >> 8) & 0xff);
      EXPECT_EQ(quad.a, 255 - (image.color & 0xff));
      ASSERT_GE(quad.u, 0);
      ASSERT_GE(quad.v, 0);
      ASSERT_LE(quad.u + quad.w, page.size_x);
      ASSERT_LE(quad.v + quad.h, page.size_y);
      const int x = quad.x - image.dst_x;
      const int y = quad.y - image.dst_y;
      ASSERT_GE(x, 0);
      ASSERT_GE(y, 0);
      ASSERT_LE(x + quad.w, image.w);
      ASSERT_LE(y + quad.h, image.h);
      for (int row = 0; row < quad.h; ++row)
        for (int col = 0; col < quad.w; ++col)
        {
          const size_t dest = (y + row) * image.w + x + col;
          restored[index][dest] = page.texture[(quad.v + row) * page.size_x + quad.u + col];
          ++coverage[index][dest];
        }
    }
  }
  for (size_t i = 0; i < restored.size(); ++i)
    for (int y = 0; y < images[i].h; ++y)
      for (int x = 0; x < images[i].w; ++x)
      {
        EXPECT_EQ(coverage[i][y * images[i].w + x], 1);
        EXPECT_EQ(restored[i][y * images[i].w + x], bitmap[y * images[i].stride + x]);
      }

  EXPECT_FALSE(OVERLAY::convert_quad_pages(nullptr, pages, limit));
  EXPECT_TRUE(pages.empty());
  EXPECT_FALSE(OVERLAY::convert_quad_pages(&images.back(), pages, limit));
  EXPECT_FALSE(OVERLAY::convert_quad_pages(images.data(), pages, 1));
}

TEST(TestOverlayRendererUtil, TiledImagesPreserveLinearFiltering)
{
  constexpr int width = 19;
  constexpr int height = 17;
  constexpr int stride = 23;
  constexpr int limit = 8;
  std::vector<unsigned char> bitmap(stride * (height - 1) + width);

  const auto sample =
      [](const std::vector<unsigned char>& pixels, int rowStride, int w, int h, double x, double y)
  {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0;
    const double fy = y - y0;
    const auto pixel = [&](int px, int py)
    { return pixels[std::clamp(py, 0, h - 1) * rowStride + std::clamp(px, 0, w - 1)]; };
    return (1.0 - fy) * ((1.0 - fx) * pixel(x0, y0) + fx * pixel(x0 + 1, y0)) +
           fy * ((1.0 - fx) * pixel(x0, y0 + 1) + fx * pixel(x0 + 1, y0 + 1));
  };

  for (const bool opaque : {true, false})
  {
    for (size_t i = 0; i < bitmap.size(); ++i)
      bitmap[i] = opaque ? 255 : (i * 17) % 251;
    ASS_Image image{};
    image.w = width;
    image.h = height;
    image.stride = stride;
    image.bitmap = bitmap.data();
    std::vector<OVERLAY::SQuads> pages;
    ASSERT_TRUE(OVERLAY::convert_quad_pages(&image, pages, limit));

    for (const auto& page : pages)
      for (const auto& quad : page.quad)
        for (int y = 0; y < quad.h * 2; ++y)
          for (int x = 0; x < quad.w * 2; ++x)
          {
            // Fragment centres at 2x scale, in source texel-centre coordinates.
            const double dx = (x + 0.5) / 2.0 - 0.5;
            const double dy = (y + 0.5) / 2.0 - 0.5;
            const double sourceX = quad.x + dx;
            const double sourceY = quad.y + dy;
            if (sourceX < 0 || sourceX > width - 1 || sourceY < 0 || sourceY > height - 1)
              continue;
            ASSERT_DOUBLE_EQ(sample(bitmap, stride, width, height, sourceX, sourceY),
                             sample(page.texture, page.size_x, page.size_x, page.size_y,
                                    quad.u + dx, quad.v + dy))
                << "source=" << sourceX << "," << sourceY << " opaque=" << opaque;
          }
  }
}

namespace
{
uint32_t PackPixel(uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
  return a << PIXEL_ASHIFT | r << PIXEL_RSHIFT | g << PIXEL_GSHIFT | b << PIXEL_BSHIFT;
}

uint32_t Channel(uint32_t px, int shift)
{
  return (px >> shift) & 0xff;
}
} // namespace

TEST(TestOverlayRendererUtil, PreparePQPixelsMatchesPaletteConversion)
{
  // A BD-J page: a few flat colours repeated, with transparency, as straight-alpha ARGB
  const std::vector<uint32_t> colours{PackPixel(0, 0, 0, 0), PackPixel(255, 157, 157, 157),
                                      PackPixel(128, 167, 90, 40), PackPixel(255, 255, 255, 255)};
  std::vector<uint32_t> pixels;
  for (int i = 0; i < 64; ++i)
    pixels.push_back(colours[(i * 7) % colours.size()]);

  std::vector<uint32_t> expected(pixels);
  OVERLAY::ConvertPQPaletteToSRGB(expected);

  std::vector<uint32_t> prepared(pixels);
  OVERLAY::PreparePQPixels(prepared, true, false);
  EXPECT_EQ(prepared, expected);
}

TEST(TestOverlayRendererUtil, PreparePQPixelsEncodesLimitedRangeStraightAlpha)
{
  std::vector<uint32_t> pixels{PackPixel(255, 255, 255, 255), PackPixel(0, 0, 0, 0),
                               PackPixel(77, 128, 64, 200)};
  OVERLAY::PreparePQPixels(pixels, false, true);

  EXPECT_EQ(pixels[0], PackPixel(255, 235, 235, 235));
  // Straight alpha: even a transparent texel takes the whole offset, the blend scales it
  EXPECT_EQ(pixels[1], PackPixel(0, 16, 16, 16));
  EXPECT_EQ(Channel(pixels[2], PIXEL_ASHIFT), 77u);
  EXPECT_EQ(Channel(pixels[2], PIXEL_RSHIFT), 16u + (128u * 219u + 127u) / 255u);
  EXPECT_EQ(Channel(pixels[2], PIXEL_GSHIFT), 16u + (64u * 219u + 127u) / 255u);
  EXPECT_EQ(Channel(pixels[2], PIXEL_BSHIFT), 16u + (200u * 219u + 127u) / 255u);
}

TEST(TestOverlayRendererUtil, PreparePQPixelsConvertsBeforeEncoding)
{
  const std::vector<uint32_t> pixels{PackPixel(255, 157, 157, 157), PackPixel(200, 167, 90, 40)};

  std::vector<uint32_t> stepwise(pixels);
  OVERLAY::PreparePQPixels(stepwise, true, false);
  OVERLAY::PreparePQPixels(stepwise, false, true);

  std::vector<uint32_t> both(pixels);
  OVERLAY::PreparePQPixels(both, true, true);
  EXPECT_EQ(both, stepwise);

  std::vector<uint32_t> untouched(pixels);
  OVERLAY::PreparePQPixels(untouched, false, false);
  EXPECT_EQ(untouched, pixels);
}
