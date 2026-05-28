# Astrology Mapping

Astral Reverberations uses astrology as a musical control system, not as a precise ephemeris. The design was inspired by the `astro-svelte-app` concepts of planet longitudes, signs, aspects, elements, and qualities.

## Simulation

`AstrologyEngine` starts from J2000 and advances each planet by an approximate orbital period. A small deterministic wobble creates retrograde-like spans for inner and social planets. This keeps the behavior repeatable and DAW-safe.

## Concepts

- Planet longitude: position around a 360 degree zodiac wheel.
- Sign: one 30 degree sector.
- Element: Fire, Earth, Air, Water.
- Quality: Cardinal, Fixed, Mutable.
- Aspect: angular relationship between two planets.
- Orb: distance from exact aspect.

## Audio Mapping

- Sun and Moon shape the broad character of motion and tail behavior.
- Mercury drives delay drift and modulation movement.
- Venus and Jupiter increase diffusion, size, and lushness.
- Mars increases energy and drive.
- Saturn increases damping and restrains feedback.
- Uranus increases stereo movement.
- Neptune adds shimmer-like brightness.
- Pluto adds slow transformation.

Hard aspects, such as conjunctions, squares, and oppositions, create tail blooms and more tension. Soft aspects, such as trines and sextiles, widen and smooth the space.

## Modes

- `Now`: uses the system clock to create an offline current snapshot.
- `Manual Date`: recalls a stored Julian day.
- `Simulated Orbit`: advances a fictional sky at a musical speed controlled by `astro_speed`.

## Historical presets

The inspector preset menu recalls fixed UTC moments (JFK assassination, Moon landing, D-Day, and others). Choosing one switches to `Manual Date` and loads that moment's Julian day into the engine. Planet longitudes are still approximate musical orbits, but each preset is repeatable and distinct.

Custom dates remain available when the stored Julian day does not match a built-in preset.

