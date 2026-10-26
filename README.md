# Devis Vinil

A VST3 scratch / playback-speed plugin built with JUCE. Put it on an audio track in Ableton Live and either grab the record and scratch it, or draw a speed curve over a loop.

## Controls

Put it on an audio track. The plugin always listens to the incoming signal, keeps a window of it and lets you reshape the playback speed of that window.

- **LOOP** – length of the window, 5 to 30 s in 1 s steps. The window is locked to the project timeline (starts at 1.1.1) and the curve runs from 0 to its end.
- **Curve** – click to add a point, drag to move, right-click for a menu (delete point / add point / reset to flat), Alt-click deletes. Y axis is playback speed (0 = stop, negative = reverse), X axis is time inside the window. Hold Shift to ignore snapping.
- **SNAP** – points jump to steps of 0.5 s in time and 0.25x in speed. `SMOOTH` switches spline / straight lines. The speed always goes from -2x (reverse) to +2x.
- **Sliders** (right panel) – double-click resets to the default.
- **Turntable** – grab the record and move the mouse around the centre.
  - `WEIGHT`: how heavy the record feels under your hand (low = follows the mouse instantly, high = lazy and smooth).
  - `DRAG`: how much inertia the platter has when it follows the curve or goes back to speed.
  - Like a real record: when you let go, the platter returns to normal speed and plays on from where it is (it does not jump back to live). The loop restart puts it back in time.
  - `PREROLL`: how far behind live the head restarts each loop, so the curve can also speed up.
  - `MIX`: dry / wet.
- **Pads** (two columns, right of the sliders) – ready-made curve shapes: FLAT, SINE, SAW, SQR, STOP, START, REV, BABY, GATE, PULSE.

## Build (macOS)

```bash
git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target DevisVinil_VST3
cp -R "build/DevisVinil_artefacts/Release/VST3/Devis Vinil.vst3" ~/Library/Audio/Plug-Ins/VST3/
```

Offline DSP test (no clicks, direction, catch-up, curve, sample playback, state):

```bash
cmake -B build-test -DVINIL_BUILD_TESTS=ON -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-test --target VinilTest
./build-test/VinilTest_artefacts/Release/VinilTest
```
