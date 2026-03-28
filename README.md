# Noise_VCO (VCV Rack) - założenia projektu

Roboczy projekt modułu VCV Rack inspirowanego koncepcją z patcha MAX:
`Interpolating_WT_OSC.maxpat`.

Cel: stworzyć **ewoluujący Wavetable VCO**, który generuje 2 niezależne przebiegi noise,
buduje z nich 2 buforowane fale WT i wykonuje morph/interpolację między nimi.

## Repo status (GitHub-ready)

- Szkielet pluginu Rack 2 utworzony (`src/`, `res/`, `plugin.json`, `Makefile`).
- Moduł startowy `NoiseVCO` zaimplementowany jako działający punkt wyjścia.
- Dodane pliki projektu pod współpracę i publikację:
  - `.gitignore`
  - `SETUP.md`
  - `BUILD_AND_RUN.md`
  - `docs/GITHUB_SETUP.md`
  - `.github/workflows/repo-check.yml`

## Referencje

1. Patch MAX (lokalny):
   - `/Users/lazuli/Documents/PROGRAMMING/VCV_PROGRAMMING/NOISE_WT_VCO/Interpolating_WT_OSC.maxpat`
2. Referencja implementacyjna WT VCO (GitHub, zachowujemy repo):
   - [surge-synthesizer/surge-rack](https://github.com/surge-synthesizer/surge-rack/tree/main)
   - moduł: `SurgeXTOSCWavetable` (`src/VCO.h`, `src/VCO.cpp`, `src/vcoconfig/Wavetable.h`)
3. Wzorzec stylu i workflow Twoich modułów VCV:
   - `/Users/lazuli/Documents/PROGRAMMING/VCV_PROGRAMMING/KSZ_ABLETON_ARP`

## Standard projektu (na podstawie KSZ_ABLETON_ARP)

- Target: **VCV Rack 2** (`Rack2/plugins-mac-arm64`).
- Build przez zewnętrzny SDK:
  - `make -j4 RACK_DIR=/absolute/path/to/Rack-SDK`
- Typowy lokalny `RACK_DIR` używany przez Ciebie:
  - `/Users/lazuli/Documents/PROGRAMMING/TEENSY/KSZ_TEENSY_PLATFORMIO/Teensy_Chord_Gen/Rack-SDK`
- Makefile pattern:
  - `RACK_DIR ?= ../Rack-SDK`
  - `include $(RACK_DIR)/plugin.mk`
- Konwencja metadata/plugin:
  - `plugin.json` z `brand = "KSZ"` i wersjonowaniem jak w Twoich modułach
    (w `KSZ_ABLETON_ARP` aktualnie `version = "2.0.0"`).
- Styl panelu:
  - statyczne napisy przez custom `TransparentWidget` (`PanelLabel`)
  - układ elementów w `mm2px(...)`
  - podpisy grup i kontrolek bezpośrednio na panelu.
- Modulacja pokręteł:
  - pokrętło typu custom (jak `CvDepthKnob`) z:
    - submenu PPM (right-click) dla głębokości CV (np. kroki 0..100%)
    - ringiem modulacji (zakres + marker wartości mod)
    - opcjonalnym wskaźnikiem LED.

## Wizja modułu Noise_VCO

**Opis muzyczny:** oscylator WT o ciągłej, "żywej" ewolucji brzmienia, z naciskiem na organiczny
charakter i płynny morph między dwiema niezależnie wygenerowanymi falami.

**Opis techniczny:** dwa noise-source -> dwa docelowe bufory WT -> interpolacja (morph) -> stereo out.

## Zakres v1 (to implement now)

1. I/O audio/CV
   - `VOCT` input (1V/Oct).
   - `TRIG GEN` input (trigger do wygenerowania nowego zestawu WT).
   - `MORPH CV` input.
   - `WT SIZE CV` input.
   - `L OUT`, `R OUT`.

2. Kontrolki
   - `PITCH` (częstotliwość bazowa).
   - `DETUNE` (rozstrajanie instancji unison).
   - `UNISON` (0..9).
   - `OCTAVE` (-3..+3).
   - `MORPH` (manual blend A<->B, plus CV).
   - `WT SIZE` (manual + CV; zakres dyskretny: 256/512/1024/2048/4096).
   - `GEN` push button (lokalne wyzwolenie nowego zestawu WT).

3. Zachowanie GEN
   - event `GEN` uruchamiany przez:
     - przycisk `GEN`,
     - wejście `TRIG GEN` (zbocze przez `SchmittTrigger`).
   - każdy event tworzy nową parę WT (A i B).

4. Wyświetlanie
   - ekran/obszar w górnej części panelu z rysowaniem aktualnej WT:
     - minimum: bieżąca fala wynikowa (po morph),
     - docelowo: przełączane podglądy A/B/Result.

## Algorytm generacji WT (spec)

1. Dla każdej strony (`A` i `B`) generujemy surowy sygnał noise o długości:
   - `noiseLen = WT_SIZE * NOISE_OVERSCAN`,
   - gdzie `NOISE_OVERSCAN > 1` (na start stała, np. `4x`).

2. Szukamy dwóch punktów przecięcia z zerem:
   - `z0` = pierwsze sensowne przejście przez 0 (początek segmentu),
   - `z1` = kolejne przejście przez 0 przy odpowiedniej odległości od `z0`.

3. Wycinamy segment `[z0, z1]` i skalujemy (resample) do dokładnego `WT_SIZE`.

4. Znormalizowany wynik wpisujemy do bufora WT (`A` albo `B`).

5. Bufor wynikowy `R` powstaje przez interpolację:
   - `R[i] = (1 - morph) * A[i] + morph * B[i]`.

## Architektura buforów

- **Teraz (v1 hardcoded):**
  - 2 bufory docelowe: `wtA`, `wtB`
  - 1 bufor wynikowy runtime: `wtR` (lub obliczanie on-the-fly)
- **Docelowo (plan):**
  - 4 bufory:
    - 2 robocze (`workA`, `workB`) do generacji/analizy/zero-cross
    - 2 finalne (`finalA`, `finalB`) do interpolacji audio.

## Parametryzacja i CV

- `MORPH`:
  - bazowa wartość z pokrętła + modulacja z `MORPH CV`,
  - clamp do `[0, 1]`,
  - pokrętło z submenu głębokości CV + ring/LED jak w `KSZ_ABLETON_ARP`.

- `WT SIZE`:
  - parametr dyskretny mapowany na `{256, 512, 1024, 2048, 4096}`,
  - CV kwantyzowane do jednego z 5 stanów.

- `UNISON`:
  - int `0..9`,
  - `DETUNE` steruje rozjazdem głosów unison.

## Plan implementacyjny (kolejność)

1. Szkielet pluginu Rack2 (`plugin.json`, `Makefile`, `src/plugin.cpp`, `src/NoiseVCO.cpp`).
2. UI panel + etykiety (`PanelLabel`) + rozmieszczenie I/O.
3. Generator WT (noise -> zero-cross window -> resample -> wtA/wtB).
4. Oscylator odczytu WT z `VOCT`, `OCTAVE`, `PITCH`.
5. Morph runtime + stereo out L/R.
6. `GEN` (button + trigger input) i odświeżenie buforów.
7. `MORPH` i `WT SIZE` z CV depth menu + ring/LED.
8. Podgląd fali w górnym ekranie.
9. Testy odsłuchowe i stabilizacja.

## Ważne decyzje projektowe

- Startujemy od implementacji "na twardo", żeby szybko zagrało i było testowalne.
- Zachowujemy kompatybilny workflow z Twoimi repo VCV (Rack SDK external + panel labels + modulacja PPM).
- Trigger do przyszłej obwiedni jest przygotowany od razu (`TRIG GEN`), ale obwiednia nie jest częścią v1.

## Status

- [x] Definicja założeń projektu i architektury `Noise_VCO`.
- [ ] Implementacja kodu modułu VCV.
- [ ] Wstępny build i smoke test w Rack.
