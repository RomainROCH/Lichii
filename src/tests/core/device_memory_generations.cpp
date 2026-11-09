// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <optional>
#include <catch2/catch_test_macros.hpp>

#include "core/device_memory.h"
#include "video_core/host1x/gpu_device_memory_manager.h"

// Regression for the owner-pointer + generation translation cache: a destroyed
// manager can be constructed at the same address as the previous manager.
// Its generation must differ even before the first Map/Unmap.
TEST_CASE("Device memory translation generation changes across manager lifetimes",
          "[device_memory_generations]") {
    Core::DeviceMemory physical;
    std::optional<Tegra::MaxwellDeviceMemoryManager> manager;

    manager.emplace(physical);
    const auto* const slot = std::addressof(*manager);
    const u64 first_generation = manager->TranslationGeneration();

    manager.reset();
    manager.emplace(physical);
    REQUIRE(std::addressof(*manager) == slot);
    const u64 second_generation = manager->TranslationGeneration();
    REQUIRE(second_generation != first_generation);

    manager.reset();
    manager.emplace(physical);
    REQUIRE(std::addressof(*manager) == slot);
    REQUIRE(manager->TranslationGeneration() != second_generation);
    REQUIRE(manager->TranslationGeneration() != first_generation);
}
