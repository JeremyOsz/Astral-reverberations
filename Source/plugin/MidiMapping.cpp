#include "plugin/MidiMapping.h"

namespace astral::plugin {
namespace {

std::optional<astro::PlanetId> planetForRangedMidiNote(int note, int first, int last) noexcept
{
    if (note < first || note > last) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(note - first);
    if (index >= static_cast<std::size_t>(astro::PlanetId::Count)) {
        return std::nullopt;
    }
    return static_cast<astro::PlanetId>(index);
}

} // namespace

std::optional<astro::PlanetId> planetForTailMidiNote(int note) noexcept
{
    return planetForRangedMidiNote(note, kMidiTailNoteFirst, kMidiTailNoteLast);
}

std::optional<astro::PlanetId> planetForPluckMidiNote(int note) noexcept
{
    return planetForRangedMidiNote(note, kMidiPluckNoteFirst, kMidiPluckNoteLast);
}

bool isTailPerformanceMidiNote(int note) noexcept
{
    return planetForTailMidiNote(note).has_value();
}

bool isPluckPerformanceMidiNote(int note) noexcept
{
    return planetForPluckMidiNote(note).has_value();
}

float midiCcToNormalized(int ccValue) noexcept
{
    return static_cast<float>(std::clamp(ccValue, 0, 127)) / 127.0f;
}

float midiVelocityToWet(int velocity) noexcept
{
    return static_cast<float>(std::clamp(velocity, 1, 127)) / 127.0f;
}

float midiPitchWheelToSemitones(int pitchWheelValue) noexcept
{
    const int centered = std::clamp(pitchWheelValue, 0, 16383) - 8192;
    return (static_cast<float>(centered) / 8192.0f) * kMidiPitchBendSemitones;
}

} // namespace astral::plugin
