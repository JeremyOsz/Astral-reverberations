#include "astro/HistoricalPresets.h"

#include "astro/AstrologyEngine.h"

#include <array>
#include <cmath>

namespace astral::astro {
namespace {

constexpr std::array<HistoricalEventPreset, 15> kPresets{{
    {"JFK assassination", 1963, 11, 22, 18, 30, 0},
    {"Moon landing (first step)", 1969, 7, 21, 2, 56, 15},
    {"Apollo 11 launch", 1969, 7, 16, 13, 32, 0},
    {"Woodstock opens", 1969, 8, 15, 17, 7, 0},
    {"Fall of Berlin Wall", 1989, 11, 9, 18, 53, 0},
    {"September 11 attacks", 2001, 9, 11, 13, 46, 0},
    {"Hiroshima bombing", 1945, 8, 5, 23, 15, 0},
    {"D-Day (Normandy)", 1944, 6, 6, 5, 30, 0},
    {"Pearl Harbor attack", 1941, 12, 7, 18, 18, 0},
    {"Storming of the Bastille", 1789, 7, 14, 12, 0, 0},
    {"US Declaration signed", 1776, 7, 4, 12, 0, 0},
    {"Sarajevo 1914", 1914, 6, 28, 9, 28, 0},
    {"Black Tuesday 1929", 1929, 10, 29, 14, 30, 0},
    {"Beatles on Ed Sullivan", 1964, 2, 9, 1, 0, 0},
    {"Chernobyl disaster", 1986, 4, 25, 22, 23, 0},
}};

} // namespace

std::span<const HistoricalEventPreset> historicalPresets()
{
    return kPresets;
}

double julianDayForPreset(const HistoricalEventPreset& preset)
{
    return AstrologyEngine::julianDayFromUtc(
        preset.year,
        preset.month,
        preset.day,
        preset.hour,
        preset.minute,
        preset.second);
}

std::optional<std::size_t> findPresetIndexForJulianDay(double julianDay, double toleranceDays)
{
    for (std::size_t index = 0; index < kPresets.size(); ++index) {
        const double presetDay = julianDayForPreset(kPresets[index]);
        if (std::abs(presetDay - julianDay) <= toleranceDays) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace astral::astro
