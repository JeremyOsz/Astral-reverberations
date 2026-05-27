# VST/JUCE Basics

This project uses JUCE because it lets one C++ plugin body build to VST3 and, later, other formats such as standalone or AU. The portable audio work lives in `Source/dsp`; the JUCE layer adapts DAW calls into that core.

## Main Classes

- `AudioProcessor`: the plugin object the host calls for audio, state, parameters, and lifecycle.
- `AudioProcessorValueTreeState`: JUCE's common parameter/state helper. Parameters are exposed to the host for automation and saved in sessions.
- `AudioProcessorEditor`: the plugin UI. It should control parameters, not run DSP.

## Lifecycle

- `prepareToPlay(sampleRate, blockSize)` allocates buffers and resets DSP. Allocations are safe here because audio is not running yet.
- `processBlock(buffer, midi)` is the real-time callback. Avoid heap allocation, file I/O, network calls, locks, logging, and slow system calls here.
- `getStateInformation` and `setStateInformation` save and restore parameter state plus non-parameter plugin state such as astro mode and manual date.

## Real-Time Safety

The plugin computes current or simulated astrology values outside any network dependency. The DSP engine allocates only during `prepare`; `processBlock` only reads parameters, advances the deterministic engine, and processes audio samples.

## Debugging

Build the standalone target first when possible. It is easier to attach a debugger to the standalone app than to a DAW. After that, load the VST3 in a test host and verify bypass, parameter automation, and session restore.

