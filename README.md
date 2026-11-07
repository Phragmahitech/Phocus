# Phocus

Suppresseur dynamique de résonances (VST3, JUCE 8, C++17) par Phragma.

- Moteur : `Source/ResonanceEngine.h` (STFT, détection par rapport à l'enveloppe lissée du spectre, réduction lissée en fréquence et dans le temps, reconstruction normalisée). Sans dépendance, testé seul (`Tests/EngineTests.cpp`).
- Courbe de sensibilité : `Source/SensitivityCurve.h` (6 bandes).
- FFT réelle : `Source/RealFFT.h`.
- Interface : `Source/PluginEditor.cpp` (couleurs du skin Phragma).

Compilation : `cmake -S . -B build -DPHOCUS_BUILD_TESTS=ON && cmake --build build --config Release`.
Voir INSTALLATION.txt.
