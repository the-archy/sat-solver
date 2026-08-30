#!/usr/bin/env python3
"""Experimentální vyhodnocení SAT solveru na dodaném datasetu.

Spustí přeložený solver na vzorku formulí z ./benchmarks, ověří správnost
(u SAT kontroluje model, u UNSAT porovnává s očekávaným štítkem z názvu
adresáře) a zapíše:

  eval/results.csv   - jeden řádek na instanci

Agregace / tabulky / grafy se dělají zvlášť nad tímhle CSV (viz eval/README.md).

Příklad:
  python scripts/benchmark.py --solver ./solver --sample 100 --timeout 10
  python scripts/benchmark.py --solver ./solver --sample 0 --timeout 30
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from pathlib import Path
from random import Random


# --------------------------------------------------------------------------
# discovery
# --------------------------------------------------------------------------

def expected_result(path: Path) -> str | None:
    """SAT / UNSAT / None podle segmentu cesty (unsat má přednost před sat)."""
    parts = {p.lower() for p in path.parts}
    if "unsat" in parts:
        return "UNSAT"
    if "sat" in parts:
        return "SAT"
    return None


def discover(root: Path) -> list[tuple[str, str | None, list[Path]]]:
    """Vrátí [(kategorie, očekávaný_výsledek, [soubory]), ...]."""
    leaves: dict[Path, list[Path]] = {}
    for cnf in sorted(root.rglob("*.cnf")):
        leaves.setdefault(cnf.parent, []).append(cnf)
    out = []
    for directory, files in sorted(leaves.items()):
        label = directory.relative_to(root).as_posix()
        out.append((label, expected_result(directory), files))
    return out


# --------------------------------------------------------------------------
# DIMACS + verifikace modelu
# --------------------------------------------------------------------------

def parse_cnf(path: Path) -> list[list[int]]:
    clauses: list[list[int]] = []
    current: list[int] = []
    with path.open() as handle:
        for line in handle:
            line = line.strip()
            if not line or line[0] in "cp":
                continue
            for token in line.split():
                value = int(token)
                if value == 0:
                    clauses.append(current)
                    current = []
                else:
                    current.append(value)
    if current:
        clauses.append(current)
    return clauses


def model_satisfies(clauses: list[list[int]], model: set[int]) -> bool:
    return all(any(lit in model for lit in clause) for clause in clauses)


# --------------------------------------------------------------------------
# spuštění jedné instance
# --------------------------------------------------------------------------

@dataclass
class Record:
    category: str
    file: str
    expected: str | None
    got: str
    status: str            # ok | wrong | bad-model | timeout | error
    wall_s: float
    solver_init: int | None
    solver_solve: int | None
    unit_props: int | None
    decisions: int | None


def run_one(solver: Path, cnf: Path, expected: str | None, timeout: float,
            verify: bool) -> Record:
    started = time.perf_counter()
    try:
        proc = subprocess.run(
            [str(solver), str(cnf)],
            capture_output=True, text=True, timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return Record(cnf.parent.name, cnf.name, expected, "-", "timeout",
                      timeout, None, None, None, None)
    wall = time.perf_counter() - started

    lines = proc.stdout.splitlines()
    if proc.returncode != 0 or len(lines) < 6 or lines[0] not in ("SAT", "UNSAT"):
        return Record(cnf.parent.name, cnf.name, expected, "-", "error",
                      wall, None, None, None, None)

    got = lines[0]
    try:
        init_t = int(lines[2])
        solve_t = int(lines[3])
        uprop = int(lines[4])
        decisions = int(lines[5])
    except ValueError:
        return Record(cnf.parent.name, cnf.name, expected, got, "error",
                      wall, None, None, None, None)

    status = "ok"
    if expected is not None and got != expected:
        status = "wrong"
    elif got == "SAT" and verify:
        model = {int(x) for x in lines[1].split()}
        if not model_satisfies(parse_cnf(cnf), model):
            status = "bad-model"

    return Record(cnf.parent.name, cnf.name, expected, got, status,
                  wall, init_t, solve_t, uprop, decisions)


# --------------------------------------------------------------------------
# výstup
# --------------------------------------------------------------------------

def write_csv(records: list[Record], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(asdict(records[0])))
        writer.writeheader()
        for rec in records:
            writer.writerow(asdict(rec))


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def resolve_solver(hint: str | None) -> Path:
    candidates = [hint] if hint else ["./solver", "./solver.exe",
                                      "build/solver", "build/sat-solver"]
    for cand in candidates:
        if cand and Path(cand).is_file():
            return Path(cand).resolve()
    sys.exit(f"solver nenalezen (zkoušené: {candidates}); použij --solver PATH")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--solver", help="cesta k přeloženému solveru")
    ap.add_argument("--benchmarks", default="benchmarks", type=Path)
    ap.add_argument("--sample", type=int, default=50,
                    help="počet instancí na kategorii (0 = vše)")
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--out", default="eval/results.csv", type=Path,
                    help="cílový CSV soubor")
    ap.add_argument("--no-verify", action="store_true",
                    help="nekontrolovat SAT modely proti formuli")
    ap.add_argument("--categories", nargs="*",
                    help="filtr na podřetězec názvu kategorie")
    args = ap.parse_args()

    solver = resolve_solver(args.solver)
    rng = Random(args.seed)

    groups = discover(args.benchmarks)
    if not groups:
        sys.exit(f"žádné .cnf pod {args.benchmarks}")
    if args.categories:
        groups = [g for g in groups
                  if any(sub in g[0] for sub in args.categories)]

    records: list[Record] = []
    started = time.perf_counter()
    for label, expected, files in groups:
        chosen = list(files)
        if args.sample and len(chosen) > args.sample:
            chosen = rng.sample(chosen, args.sample)
        chosen.sort()
        print(f"[{label}] {len(chosen)} instancí (očekáváno {expected or '?'})",
              flush=True)
        for cnf in chosen:
            rec = run_one(solver, cnf, expected, args.timeout,
                          verify=not args.no_verify)
            rec.category = label
            records.append(rec)
            if rec.status != "ok":
                print(f"    {rec.status:9} {cnf.name}", flush=True)
    elapsed = time.perf_counter() - started

    if not records:
        sys.exit("žádné instance ke spuštění")

    write_csv(records, args.out)

    counts = {s: sum(r.status == s for r in records)
              for s in ("ok", "wrong", "bad-model", "timeout", "error")}
    print()
    print(f"{len(records)} instancí za {elapsed:.1f} s -> {args.out}")
    print("  " + "  ".join(f"{k}={v}" for k, v in counts.items() if v))

    bad = counts["wrong"] + counts["bad-model"]
    if bad:
        sys.exit(f"POZOR: {bad} nesprávných výsledků")


if __name__ == "__main__":
    main()
