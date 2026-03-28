# Noise_VCO - Algorithm Status

## Aktualny algorytm WT (aktywny)

Wersja aktywna używa parametrów:
- `DENS` (`1..48`) - liczba punktów wewnętrznych (bez skrajnych)
- `SMOTH` (`0..100`, int) - sposób interpolacji między punktami
- `WT SIZE` (`256..2048`, int ciągły)

### Pipeline (WTA / WTB)
1. Generator zawsze tworzy pełną falę o stałej długości `2048` próbek.
2. W pamięci trzymane jest zawsze `48` punktów wewnętrznych (`DENS memory`) dla `wtA` i `wtB`.
3. `GEN` / `TRIG GEN` losuje nowy zestaw tych 48 punktów (seed zmienia się tylko wtedy).
4. Aktualne `DENS` wybiera równomiernie podzbiór punktów z pamięci 48 (bez dodatkowej reguły dla małych DENS).
5. Odtworzenie pełnej fali `2048` odbywa się przez interpolację pomiędzy wybranymi punktami:
   - `SMOTH = 0` -> interpolacja liniowa
   - `SMOTH = 100` -> interpolacja kosinusowa (maksymalnie gładka, sinusoidalna w charakterze)
   - wartości pośrednie -> blend liniowa<->kosinusowa.
6. `WT SIZE` nie przelicza nowej fali: określa tylko ile pierwszych próbek z `2048` jest aktywne
   (zoom in/out zakotwiczony od lewej, od indeksu `0`).
7. `wtMorph` jest liczone jako interpolacja `wtA <-> wtB`.
8. Każda zmiana kształtu (`GEN`/`DENS`/`SMOTH`) lub długości aktywnej (`WT SIZE`) używa crossfade
   starej->nowej tabeli przez ok. `500 ms`, żeby ograniczyć skoki.

## Legacy algorytm (odłożony na bok)

Stary algorytm został zachowany w kodzie jako funkcja:
- `generateNoiseWindowedWavetableLegacy(...)` w `src/NoiseVCO.cpp`.

### Legacy pipeline
- oversampled noise
- wyszukiwanie przejść przez zero
- wybór segmentu
- resampling do `WT_SIZE`
- normalizacja

Legacy nie jest aktualnie używany do generacji `wtA/wtB`.

## Uwaga o nazewnictwie

Parametr jest nazwany `SMOTH` (bez drugiego `O`) zgodnie z aktualnym założeniem projektu.

## Pomysły na później

- Wariant `SMOTH` z wymuszoną "ząbkowatością" segmentów:
  - między dwoma punktami `DENS` generator celowo dodaje kilka małych skoków ("ząbków"),
    co zwiększa zawartość wysokich harmonicznych,
  - `SMOTH` następnie działa jako mechanizm wygładzający te ząbki (redukcja ostrości / harmonicznych),
  - cel: większy, bardziej słyszalny zakres działania `SMOTH` bez zmiany głównej architektury modułu.

## Reverb status

- W module audio backend reverbu został przełączony z własnej implementacji "clouds-style"
  na `daisysp::ReverbSc`.
- Parametry panelu:
  - `RVB TM` (czas, prezentowany w sekundach),
  - `RVB FB` (charakter/tail feedback),
  - `RVB MIX` (dry/wet).
- Uwaga licencyjna: `ReverbSc` jest komponentem LGPL-2.1 (szczegóły w `README.md` i pliku
  `docs/THIRD_PARTY_DAISYSP_LGPL_LICENSE.txt`).
- Test 2026-03-28 (anti-dławienie, wariant "lite"):
  - bardzo stabilny, ale brzmieniowo zbyt przytłumiony/słaby,
  - wariant został cofnięty do poprzedniego, mocniejszego strojenia reverbu.

## Milestone 2026-03-28

- Bieżąca wersja modułu została uznana za działającą i stabilną odsłuchowo.
- Reverb pozostaje w wersji `DaisySP ReverbSc` (strojone mapowanie `RVB TM / RVB FB / RVB MIX`).
- Poprawa czystości oscylatora (inspiracja Surge XT), etap 1 wdrożony:
  - dodany bank `WT mipmaps` (`2048/1024/512/256/128`),
  - odczyt oscylatora wybiera poziom mipmap dynamicznie wg częstotliwości,
  - między poziomami jest płynny crossfade (brak skoków przy przełączaniu poziomu).
- Etap 2 wdrożony:
  - odczyt tabeli zmieniony z `linear` na 4-punktowy `cubic/hermite` (z fallbackiem `linear` dla bardzo małych tabel).
- Kolejny etap (na później):
  - opcjonalny oversampling rdzenia oscylatora.
- Założenie pod Daisy Seed:
  - reverb może zostać pominięty,
  - priorytetem ma być czystość i stabilność rdzenia WT VCO.

## Ostatnie commity

- `d3e7bfe` - `Switch wavetable readout to 4-point Hermite interpolation`
- `8494ce8` - `Add frequency-dependent wavetable mipmaps for cleaner highs`
- `0de530e` - `Add initial M4L prototype files for Noise_VCO`
- `0e6c0dd` - `NoiseVCO: stabilize reverb, add wet HPF and trigger de-click`
