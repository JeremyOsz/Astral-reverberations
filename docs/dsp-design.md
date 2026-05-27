# DSP Design

The DSP core is deliberately written without JUCE dependencies so it can later be reused in a VCV module, RNBO integration experiment, or Daisy firmware prototype.

## Tape-Style Delay

The delay uses a stereo circular buffer with interpolated reads. Multiple taps create a loose multi-head feel:

- main tap near the selected delay time
- shorter tap for rhythmic density
- cross-channel longer tap for stereo smear

Delay time is smoothed per sample to avoid clicks. Wow and flutter are sinusoidal modulations added to the read position.

## Feedback And Tone

Feedback is filtered with a one-pole low-pass before being written back. A soft clipper adds tape-like saturation and prevents runaway peaks. Freeze mode recirculates existing delay/reverb energy and avoids writing new dry input into the feedback path.

## Reverb

The reverb is a compact four-line feedback-delay network per channel. A Hadamard-style matrix diffuses line outputs back into the network. Damping filters reduce high-frequency buildup, and slow stereo modulation shifts read positions.

## Safety Rules

- Allocate in `prepare`, not in `processBlock`.
- Clamp all feedback values.
- Sanitize non-finite samples.
- Reset clears delay and filter state.
- Use equal-power wet/dry mixing for smoother blend changes.

