#include "astro/HistoricalPresets.h"

#include "astro/AstrologyEngine.h"

#include <array>
#include <cmath>

namespace astral::astro {
namespace {

constexpr std::array<HistoricalEventPreset, 30> kPresets{{
    {"Battle of Hastings", 1066, 10, 14, 12, 0, 0, 50, "Dm"},
    {"Magna Carta sealed", 1215, 6, 15, 12, 0, 0, 48, "C"},
    {"Black Death in London", 1348, 3, 20, 12, 0, 0, 45, "Am"},
    {"Fall of Constantinople", 1453, 5, 29, 12, 0, 0, 43, "G"},
    {"Gutenberg Bible printed", 1455, 2, 23, 12, 0, 0, 50, "Dm"},
    {"Fall of Granada", 1492, 1, 2, 12, 0, 0, 40, "E"},
    {"Treaty of Westphalia", 1648, 10, 24, 12, 0, 0, 50, "Dm"},
    {"Great Fire of London", 1666, 9, 2, 12, 0, 0, 47, "B"},
    {"Lisbon earthquake", 1755, 11, 1, 12, 0, 0, 50, "Dm"},
    {"US Declaration signed", 1776, 7, 4, 12, 0, 0, 41, "F"},
    {"Storming of the Bastille", 1789, 7, 14, 12, 0, 0, 38, "Dm"},
    {"Battle of Waterloo", 1815, 6, 18, 12, 0, 0, 43, "G"},
    {"Darwin publishes Origin", 1859, 11, 24, 12, 0, 0, 43, "G"},
    {"Meiji Restoration", 1868, 1, 3, 12, 0, 0, 40, "E"},
    {"Einstein relativity paper", 1905, 6, 30, 12, 0, 0, 48, "C"},
    {"Sarajevo 1914", 1914, 6, 28, 9, 28, 0, 44, "Gm"},
    {"October Revolution", 1917, 11, 7, 12, 0, 0, 50, "Dm"},
    {"Black Tuesday 1929", 1929, 10, 29, 14, 30, 0, 37, "C#m"},
    {"Pearl Harbor attack", 1941, 12, 7, 18, 18, 0, 41, "Fm"},
    {"D-Day (Normandy)", 1944, 6, 6, 5, 30, 0, 46, "Bb"},
    {"Hiroshima bombing", 1945, 8, 5, 23, 15, 0, 42, "F#m"},
    {"Sputnik launched", 1957, 10, 4, 19, 28, 34, 46, "Bb"},
    {"JFK assassination", 1963, 11, 22, 18, 30, 0, 38, "Dm"},
    {"Beatles on Ed Sullivan", 1964, 2, 9, 1, 0, 0, 43, "G"},
    {"Apollo 11 launch", 1969, 7, 16, 13, 32, 0, 43, "G"},
    {"Moon landing (first step)", 1969, 7, 21, 2, 56, 15, 36, "C"},
    {"Woodstock opens", 1969, 8, 15, 17, 7, 0, 40, "E"},
    {"Chernobyl disaster", 1986, 4, 25, 22, 23, 0, 50, "Dm"},
    {"Fall of Berlin Wall", 1989, 11, 9, 18, 53, 0, 46, "Bb"},
    {"September 11 attacks", 2001, 9, 11, 13, 46, 0, 52, "Em"},
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
