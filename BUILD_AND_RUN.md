# Build And Run

## Build
```bash
cd /Users/lazuli/Documents/PROGRAMMING/VCV_PROGRAMMING/NOISE_WT_VCO
make -j4 RACK_DIR=/absolute/path/to/Rack-SDK
```

## Package
```bash
make dist RACK_DIR=/absolute/path/to/Rack-SDK
```

Oczekiwane artefakty:
- `plugin.dylib` (lub odpowiednik platformowy)
- `dist/NoiseVCO/`
- `dist/NoiseVCO-<version>-<platform>.vcvplugin`

## Install lokalnie do Rack2
```bash
make install RACK_DIR=/absolute/path/to/Rack-SDK
```

## Smoke test
1. Otwórz VCV Rack 2 i dodaj moduł `Noise VCO`.
2. Podłącz `VOCT` (opcjonalnie), odsłuchaj `L OUT` / `R OUT`.
3. Naciśnij `GEN` oraz podaj trigger na `TRIG`, potwierdź zmianę fali na wyświetlaczu.
4. Sprawdź działanie `MORPH` i `WT SIZE` z CV.

## Clean
```bash
make clean RACK_DIR=/absolute/path/to/Rack-SDK
```
