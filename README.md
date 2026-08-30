# Archyho naprosto odporný SAT solver

## Sestavení:
`g++ -Iinclude -O2 -flto -march=native src/*.cpp -std=c++20 -o <output dir>/solver`

## Použití:
`./solver input`

## Vyhodnocení 
Je ve složce eval.


## Ponznámky
- Tohle asi n-tá verze, pořád jsem to přepisoval a přepisoval...
    - Staré verze jsou ztracené někde 
- Nemám absolutní tušení, jestli cokoliv dělám správně
- Pro moje zabavení jsem zvolil větší C++ abstrakci oproti "raw" přístupu jako bych ho třeba zvolil, kdybych se rozhodl pro C
    - Hlavně z důvodu, že C++ mě relativně baví a ještě jsem nedělal nějaký """větší""" projekt
        - Docela by mě zajímalo, kolik lidí se rozhodlo pro Rust...
- Chápu, že old school C programátoři by mě za moderní `#pragma once` asi zastřelili
- Nějak jsem se snažil využít lecture-log.pdf ze seminářů
- Dělal jsem aj verzi napsanou v TypeScriptu, která běžela v Bun runtime a byla dokonce o něco výkonnější - do teď nerozumím jak, asi nějaká Zig/Rust LLVM magie



