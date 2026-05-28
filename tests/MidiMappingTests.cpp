#include "plugin/MidiMapping.h"

#include "TestSupport.h"

#include <cmath>

void runMidiMappingTests()
{
    using astral::plugin::planetForPluckMidiNote;
    using astral::plugin::planetForTailMidiNote;
    using astral::plugin::isPluckPerformanceMidiNote;
    using astral::plugin::isTailPerformanceMidiNote;
    using astral::plugin::kMidiPitchBendSemitones;
    using astral::plugin::midiCcToNormalized;
    using astral::plugin::midiPitchWheelToSemitones;
    using astral::plugin::midiVelocityToWet;
    using astral::astro::PlanetId;

    require(planetForTailMidiNote(36) == PlanetId::Sun, "tail note 36 maps to Sun");
    require(planetForTailMidiNote(45) == PlanetId::Pluto, "tail note 45 maps to Pluto");
    require(!planetForTailMidiNote(35).has_value(), "note below tail range is ignored");
    require(planetForPluckMidiNote(60) == PlanetId::Sun, "pluck note 60 maps to Sun");
    require(planetForPluckMidiNote(69) == PlanetId::Pluto, "pluck note 69 maps to Pluto");
    require(isTailPerformanceMidiNote(40), "40 is a tail performance note");
    require(!isPluckPerformanceMidiNote(40), "40 is not a pluck performance note");
    require(isPluckPerformanceMidiNote(65), "65 is a pluck performance note");

    require(midiCcToNormalized(0) == 0.0f, "CC 0 is normalized to 0");
    require(midiCcToNormalized(127) == 1.0f, "CC 127 is normalized to 1");
    require(midiVelocityToWet(127) == 1.0f, "max velocity is full wet");
    require(midiVelocityToWet(1) > 0.0f, "minimum velocity still excites");

    require(std::abs(midiPitchWheelToSemitones(8192)) < 0.001f, "centered pitch wheel is zero semitones");
    require(
        std::abs(midiPitchWheelToSemitones(16383) - kMidiPitchBendSemitones) < 0.01f,
        "max pitch wheel is about +2 semitones");
    require(
        std::abs(midiPitchWheelToSemitones(0) + kMidiPitchBendSemitones) < 0.01f,
        "min pitch wheel is about -2 semitones");
}
