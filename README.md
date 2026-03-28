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

## GitHub

- Repo: [KrzysztofSzpetmanski/VCV_NOISE_WT_VCO](https://github.com/KrzysztofSzpetmanski/VCV_NOISE_WT_VCO)
- Domyślny branch: `main`
- Szybki start:
  - `git clone https://github.com/KrzysztofSzpetmanski/VCV_NOISE_WT_VCO.git`
  - `cd VCV_NOISE_WT_VCO`
  - `git checkout -b codex/<nazwa-zmiany>`
- Publikacja zmian:
  - `git add .`
  - `git commit -m "..."`
  - `git push -u origin codex/<nazwa-zmiany>`

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
   - `DENS CV` input.
   - `SMOTH CV` input.
   - `L OUT`, `R OUT`.

2. Kontrolki
   - `PITCH` (częstotliwość bazowa).
   - `DETUNE` (rozstrajanie instancji unison).
   - `UNISON` (0..9).
   - `OCTAVE` (-3..+3).
   - `MORPH` (manual blend A<->B, plus CV).
   - `WT SIZE` (manual + CV; zakres int ciągły: 256..2048).
   - `DENS` (1..48, liczba punktów wewnętrznych generacji WT).
   - `SMOTH` (0..100 INT, charakter interpolacji między punktami DENS).
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

Aktualna implementacja używa algorytmu `DENS/SMOTH`:

1. Skrajne próbki w `wtA/wtB` są zawsze wymuszone na `0`.
2. `DENS` definiuje liczbę punktów wewnętrznych (bez skrajnych) równomiernie rozłożonych po oknie.
3. Wartości punktów wewnętrznych są pobierane z wygenerowanego szumu.
4. Wszystkie brakujące próbki są odtwarzane między punktami:
   - `SMOTH=0` -> linie proste
   - `SMOTH=100` -> przebiegi maksymalnie gładkie (kosinusowe/sinusoidalne w charakterze)
   - wartości pośrednie -> blend liniowa<->gładka.
5. Na końcu fala jest normalizowana.
6. `wtA` i `wtB` są przeliczane na wrapie głównego phasora tylko gdy zmieni się:
   `WT SIZE`, `DENS`, `SMOTH` albo gdy zajdzie `GEN`.
7. Każda taka zmiana ma crossfade tabeli przez ok. `200 ms`, żeby ograniczyć trzaski/skoki.
8. `SEED` (noise source) zmienia się tylko po `GEN` / `TRIG GEN`.

Szczegóły aktualnego algorytmu + opis legacy:
- `docs/STATUS.md`

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
  - parametr int w zakresie `256..2048`,
  - CV działa w tym samym zakresie (z głębokością CV w menu pokrętła).

- `UNISON`:
  - int `0..9`,
  - `DETUNE` steruje rozjazdem głosów unison.

- `DENS` i `SMOTH`:
  - oba mają wejścia CV (`DENS CV`, `SMOTH CV`),
  - oba mają menu głębokości CV w pokrętle (jak pozostałe modulowane kontrolki).

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
- [x] Implementacja kodu modułu VCV (Rack 2.x, brand `KSZ`).
- [x] Build i deploy lokalny do Rack2 (`plugins-mac-arm64`).
- [x] Anti-alias etap 1: mipmap WT zależny od częstotliwości.
- [x] Anti-alias etap 2: odczyt 4-punktowy `cubic/hermite`.

## Ostatnie commity

- `d3e7bfe` - `Switch wavetable readout to 4-point Hermite interpolation`
- `8494ce8` - `Add frequency-dependent wavetable mipmaps for cleaner highs`
- `0de530e` - `Add initial M4L prototype files for Noise_VCO`

## Reverb backend

- Aktualny reverb w module jest oparty o `daisysp::ReverbSc` (`src/reverbsc.h`, `src/reverbsc.cpp`).
- Źródło: DaisySP (Electro-Smith), klasa `ReverbSc`.
- Licencja tego komponentu: **LGPL-2.1** (nagłówek w plikach źródłowych).
- Kopia tekstu licencji dołączona w repo:
  - `docs/THIRD_PARTY_DAISYSP_LGPL_LICENSE.txt`
