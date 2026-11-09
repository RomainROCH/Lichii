// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "common/slot_vector.h"
#include "video_core/texture_cache/image_base.h"

namespace {

using VideoCommon::AliasedImage;
using VideoCommon::ImageBase;
using VideoCommon::ImageId;
using VideoCommon::ImageInfo;
using VideoCommon::ImageType;
using VideoCommon::SetModificationTick;
using VideoCore::Surface::PixelFormat;

ImageInfo Info() {
    ImageInfo info;
    info.format = PixelFormat::A8B8G8R8_UNORM;
    info.type = ImageType::e2D;
    info.size = {64, 64, 1};
    info.resources.levels = 1;
    info.resources.layers = 1;
    return info;
}

/// What TextureCache::SynchronizeAliases relies on to return early.
bool NewestAliasTickHolds(Common::SlotVector<ImageBase>& images, ImageId id) {
    const ImageBase& image = images[id];
    return std::ranges::all_of(image.aliased_images, [&](const AliasedImage& alias) {
        return images[alias.id].modification_tick <= image.newest_alias_tick;
    });
}

} // namespace

TEST_CASE("Aliases see the tick a joined image takes from its copies",
          "[video_core][texture_cache]") {
    Common::SlotVector<ImageBase> images;
    const ImageId newer = images.insert(Info(), 0x10000, 0x10000);
    const ImageId older = images.insert(Info(), 0x10000, 0x10000);
    const ImageId joined = images.insert(Info(), 0x10000, 0x10000);
    SetModificationTick(images, images[older], 3);
    SetModificationTick(images, images[newer], 5);

    // As TextureCache::JoinImages: aliases are added first, then the copies set the new image's
    // tick from its overlaps, oldest first.
    REQUIRE(AddImageAlias(images[joined], images[older], joined, older));
    REQUIRE(AddImageAlias(images[joined], images[newer], joined, newer));
    SetModificationTick(images, images[joined], images[older].modification_tick);
    SetModificationTick(images, images[joined], images[newer].modification_tick);

    for (const ImageId id : {newer, older, joined}) {
        CHECK(NewestAliasTickHolds(images, id));
    }
    // The older alias must not skip its copy from the joined image.
    CHECK(images[older].newest_alias_tick > images[older].modification_tick);
    CHECK(images[newer].newest_alias_tick <= images[newer].modification_tick);
}

TEST_CASE("Adding an alias raises both images' newest alias tick",
          "[video_core][texture_cache]") {
    Common::SlotVector<ImageBase> images;
    const ImageId a = images.insert(Info(), 0x10000, 0x10000);
    const ImageId b = images.insert(Info(), 0x10000, 0x10000);
    SetModificationTick(images, images[a], 7);
    SetModificationTick(images, images[b], 2);
    REQUIRE(AddImageAlias(images[a], images[b], a, b));
    CHECK(images[a].newest_alias_tick == 2);
    CHECK(images[b].newest_alias_tick == 7);
    SetModificationTick(images, images[b], 9);
    CHECK(images[a].newest_alias_tick == 9);
    CHECK(NewestAliasTickHolds(images, a));
    CHECK(NewestAliasTickHolds(images, b));
}

TEST_CASE("Bad overlap early collection requires full coverage", "[video_core][texture_cache]") {
    // Overlap relations are not necessarily full replacements: a new render target can cover
    // only the middle of an older one. Dropping the old GPU image would lose unique bytes.
    ImageBase old{Info(), 0x1000, 0x1000};
    old.guest_size_bytes = 0x1000;
    old.channel = 2;
    old.address_space = 7;

    ImageBase partial_right{Info(), 0x1800, 0x1800};
    partial_right.guest_size_bytes = 0x1000;
    partial_right.channel = 2;
    partial_right.address_space = 7;
    CHECK_FALSE(VideoCommon::CoversGuestGpuRange(partial_right, old));

    ImageBase same_start_short{Info(), 0x1000, 0x1000};
    same_start_short.guest_size_bytes = 0x400;
    same_start_short.channel = 2;
    same_start_short.address_space = 7;
    CHECK_FALSE(VideoCommon::CoversGuestGpuRange(same_start_short, old));

    ImageBase complete{Info(), 0x800, 0x800};
    complete.guest_size_bytes = 0x2000;
    complete.channel = 2;
    complete.address_space = 7;
    CHECK(VideoCommon::CoversGuestGpuRange(complete, old));

    complete.address_space = 8; // Identical guest VA, different mapping.
    CHECK_FALSE(VideoCommon::CoversGuestGpuRange(complete, old));
    complete.address_space = 7;
    complete.channel = 3;
    CHECK_FALSE(VideoCommon::CoversGuestGpuRange(complete, old));
}
