#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace astral::astro {

/// One fixed UTC moment whose planet layout is recalled in Manual Date mode.
struct HistoricalEventPreset {
    std::string_view label;
    int year{};
    int month{};
    int day{};
    int hour{};
    int minute{};
    int second{};
};

/// Returns every built-in historical sky preset.
std::span<const HistoricalEventPreset> historicalPresets();

/// Converts a preset's UTC calendar fields into a Julian day.
double julianDayForPreset(const HistoricalEventPreset& preset);

/// Finds a preset whose Julian day matches within `toleranceDays`, if any.
std::optional<std::size_t> findPresetIndexForJulianDay(double julianDay, double toleranceDays = 1.0 / 1440.0);

} // namespace astral::astro
