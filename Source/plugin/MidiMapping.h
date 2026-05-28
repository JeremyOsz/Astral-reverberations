#pragma once

#include "astro/AstroTypes.h"

#include <cmath>
#include <cstdint>
#include <optional>

namespace astral::plugin {

/// First MIDI note for planet orbit-tail gates (Sun..Pluto).
constexpr int kMidiTailNoteFirst = 36;
constexpr int kMidiTailNoteLast = 45;

/// First MIDI note for one-shot reverb tank plucks (Sun..Pluto).
constexpr int kMidiPluckNoteFirst = 60;
constexpr int kMidiPluckNoteLast = 69;

/// CC numbers for macro and performance parameters (see docs/midi-control.md).
constexpr int kMidiCcMacroSubstance = 20;
constexpr int kMidiCcMacroMneme = 21;
constexpr int kMidiCcMacroChoir = 22;
constexpr int kMidiCcMacroEphemeris = 23;
constexpr int kMidiCcMacroFate = 24;
constexpr int kMidiCcMacroVoid = 25;
constexpr int kMidiCcMacroPulse = 26;
constexpr int kMidiCcMacroRoot = 27;
constexpr int kMidiCcEuclidRate = 28;
constexpr int kMidiCcEuclidWet = 29;
constexpr int kMidiCcDroneHold = 30;
constexpr int kMidiCcCaptureEnable = 31;
constexpr int kMidiCcFreeze = 32;
constexpr int kMidiCcEuclidEnable = 33;
constexpr int kMidiCcRootLock = 34;
constexpr int kMidiCcSustainPedal = 64;

constexpr float kMidiPitchBendSemitones = 2.0f;

/// Maps a tail-zone note to a planet, if in range.
std::optional<astro::PlanetId> planetForTailMidiNote(int note) noexcept;

/// Maps a pluck-zone note to a planet, if in range.
std::optional<astro::PlanetId> planetForPluckMidiNote(int note) noexcept;

/// True when the note is in the dedicated tail performance range.
bool isTailPerformanceMidiNote(int note) noexcept;

/// True when the note is in the dedicated pluck performance range.
bool isPluckPerformanceMidiNote(int note) noexcept;

/// Converts a 7-bit CC value to 0..1.
float midiCcToNormalized(int ccValue) noexcept;

/// Converts note-on velocity (1..127) to a wet/send amount.
float midiVelocityToWet(int velocity) noexcept;

/// Converts a 14-bit pitch wheel value to semitone offset (center = 0).
float midiPitchWheelToSemitones(int pitchWheelValue) noexcept;

} // namespace astral::plugin
