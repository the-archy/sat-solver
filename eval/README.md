# Experimentální vyhodnocení

- Systém:
    - Windows 11 Pro (fuj, já vím)
        - Byl jsem přes léto v Praze, většinu napsal na noťasu, kde mám Linux a nechtělo se mi předělávat configy na Linuxu kvůli updatům
    - 32 GB RAM
    - AMD Ryzen 5 3600

- Solver zkompilován s `g++  -Iinclude -O2  -march=native src/*.cpp -std=c++20 -o solver`
    - Díky bohu za `scoop`, díky kterému jsem jednoduše nainstaloval GCC 

- Benchmark proběhl spuštěním `python3 scripts/benchmark.py --solver ./solver.exe --sample 100 --timeout 10`
    - tzn. 100 náhodných instancí z každé kategorie (`pidgeon-hole` má jen 5, bere se celý), timeout 10 s na instanci, běh sériový (žádná paralelizace, ať se časy nezkreslují)
    - u každé `SAT` odpovědi skript navíc ověřil, že vrácený model splňuje formuli

## Soubory

- `results.csv` – jeden řádek na instanci, sloupce:
  `category, file, expected, got, status, wall_s, solver_init, solver_solve, unit_props, decisions`
    - `status`: `ok` | `wrong` (jiný výsledek než očekávaný) | `bad-model` (SAT, ale model neplatí) | `timeout` | `error`
    - `wall_s` – reálný čas procesu měřený skriptem (přenositelné napříč OS)
    - `solver_init` / `solver_solve` – 3. a 4. řádek výstupu solveru (`clock_t`; na Windows ms, na Linuxu µs – proto u rychlých instancí bývá `0`)
    - `unit_props` / `decisions` – 5. a 6. řádek výstupu solveru
- `summary.md` – agregovaná tabulka po kategoriích, přepíše se při každém běhu skriptu
- `plot_time.png`, `plot_solved.png` – grafy, vzniknou jen s přepínačem `--plots` a nainstalovaným `matplotlib`

## Výsledky (vzorek 100 / kategorie, timeout 10 s)

| kategorie | vyřešeno | timeout | medián wall [s] | max wall [s] | medián rozhodnutí | medián unit-prop |
|---|---|---|---|---|---|---|
| 20vars/sat | 100/100 | 0 | 0.012 | 0.016 | 8 | 27 |
| 50vars/sat | 100/100 | 0 | 0.012 | 0.016 | 31 | 366 |
| 50vars/unsat | 100/100 | 0 | 0.007 | 0.013 | 63 | 1 082 |
| 100vars/sat | 100/100 | 0 | 0.015 | 0.622 | 308 | 7 208 |
| 100vars/unsat | 100/100 | 0 | 0.019 | 0.035 | 1 274 | 33 568 |
| 200vars/sat | 99/100 | 1 | 0.469 | 9.75 | 69 885 | 2 666 066 |
| 200vars/unsat | 99/99 | 0 | 2.227 | 8.97 | 344 945 | 13 491 319 |
| coloring/30vertices | 100/100 | 0 | 0.007 | 0.008 | 13 | 80 |
| coloring/100vertices | 100/100 | 0 | 0.007 | 0.010 | 46 | 1 678 |
| coloring/200vertices | 100/100 | 0 | 0.043 | 0.940 | 1 997 | 256 475 |
| pidgeon-hole/unsat | 4/5 | 1 | 0.226 | 6.09 | 205 562 | 2 126 016 |


## Pozorování

- **Náhodné 3-SAT**: do ~100 proměnných je řešení prakticky okamžité (jednotky až desítky ms). Na 200 proměnných je vidět exponenciální růst – medián skočí na stovky ms (SAT) resp. jednotky s (UNSAT) a jedna `200vars/sat` instance nedoběhne v 10 s.
- **UNSAT je při stejné velikosti dražší než SAT** – solver musí prohledat celý strom, ne jen najít jednu větev. `200vars/unsat` má medián ~345 tis. rozhodnutí proti ~70 tis. u `200vars/sat`.
- **Počet rozhodovacích vrcholů a unit-propagací roste řádově s velikostí** a je dobrým prediktorem času (viz `decisions` vs `wall_s` v `results.csv`).
- **Barvení grafu**: 30 i 100 vrcholů triviální, 200 vrcholů medián 43 ms, ale s dlouhým chvostem (max 0.94 s). Všechny instance v datasetu jsou splnitelné.
- **Pigeonhole**: čistý exponenciální výbuch. `hole6` pod 1 ms, dál to strmě roste a `hole10` timeoutuje. To odpovídá teorii – rezoluční zamítnutí PHP má exponenciální délku (lecture-log, Věta 6) – takže DPLL bez učení klauzulí tu principiálně nemá šanci.
- **`solver_solve` v tickách** je na Windows kvůli ms rozlišení `clock_t` u malých instancí `0`; směrodatná metrika je `wall_s`. Pro smysluplné CPU-časy pustit na Linuxu.
- **`max wall` u malých kategorií** je občas výrazný outlier (0.62 s u `100vars/sat`) – studený start prvního procesu / cache, ne skutečná cena řešení; medián to nezkresluje.

## Závěr

Solver spolehlivě a rychle zvládá náhodné 3-SAT do ~100 proměnných a všechny velikosti barvení grafu. Na 200 proměnných je už znát exponenciální charakter DPLL a objevují se ojedinělé timeouty; pigeonhole je mimo dosah. Největší prostor pro zlepšení je rozhodovací heuristika – teď je to statický DLCS spočtený jednou při načtení; dynamická aktivitní heuristika (VSIDS, viz lecture-log 7. seminář) by pomohla hlavně na `200vars` a `coloring/200vertices`.
