#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace var {

class Variable;

} // namespace var

namespace lit {

class Literal {
public:
  using value_type = std::uint32_t;

private:
  value_type value_;

  constexpr explicit Literal(value_type value) noexcept : value_(value) {}

public:
  [[nodiscard]]
  static constexpr Literal fromValue(value_type value) noexcept {
    return Literal{value};
  }

  [[nodiscard]]
  constexpr value_type val() const noexcept {
    return value_;
  }

  [[nodiscard]]
  constexpr var::Variable variable() const noexcept;

  [[nodiscard]]
  constexpr bool isNegative() const noexcept {
    return value_ & 1u;
  }

  [[nodiscard]]
  constexpr bool isPositive() const noexcept {
    return !(value_ & 1u);
  }

  [[nodiscard]]
  constexpr Literal negate() const noexcept {
    return Literal{value_ ^ 1u};
  }

  friend constexpr Literal positive(var::Variable) noexcept;
  friend constexpr Literal negative(var::Variable) noexcept;

  friend constexpr bool operator==(Literal, Literal) = default;
};

} // namespace lit

namespace var {

class Variable {
public:
  using value_type = std::uint32_t;

private:
  value_type value_;

  constexpr explicit Variable(value_type value) noexcept : value_(value) {}

  friend constexpr Variable lit::Literal::variable() const noexcept;
  friend constexpr lit::Literal lit::positive(Variable) noexcept;
  friend constexpr lit::Literal lit::negative(Variable) noexcept;

public:
  [[nodiscard]]
  static constexpr Variable fromValue(value_type value) noexcept {
    return Variable{value};
  }

  [[nodiscard]]
  constexpr value_type getValue() const noexcept {
    return value_;
  }

  friend constexpr bool operator==(Variable, Variable) = default;
};

} // namespace var

namespace lit {

constexpr var::Variable Literal::variable() const noexcept {
  return var::Variable::fromValue(value_ >> 1);
}

constexpr Literal positive(var::Variable variable) noexcept {
  return Literal{static_cast<Literal::value_type>(variable.getValue() << 1)};
}

constexpr Literal negative(var::Variable variable) noexcept {
  return Literal{static_cast<Literal::value_type>((variable.getValue() << 1) | 1u)};
}

} // namespace lit

struct Clause {
  std::uint32_t begin;
  std::uint32_t size;
};

struct Formula {
  std::uint32_t variableCount;

  std::vector<lit::Literal> literals;
  std::vector<Clause> clauses;
};

struct SolverStats {
  std::size_t unitPropagations = 0;
  std::size_t decisionVertices = 0;
};

// DPLL with two-watched-literal unit propagation (see lecture-log.pdf,
// seminar 7: "watched literals", enqueue/propagate/assume/backtrack).
//
// This is still plain chronological-backtracking DPLL -- same
// assign -> propagate -> conflict? backtrack : decide loop as before --
// just with an O(1)-amortised propagate() instead of an O(clauses) rescan,
// and a DLCS-style decision heuristic instead of "first unassigned var".
// No clause learning / non-chronological backtracking (that would be
// CDCL, seminar 8) is done here.
class Solver {
public:
  enum class Value : std::uint8_t { False, True, Unassigned };

  explicit Solver(const Formula &formula)
      : formula_(formula), literals_(formula.literals),
        assignment_(formula.variableCount, Value::Unassigned),
        watches_(static_cast<std::size_t>(formula.variableCount) * 2),
        posOccurrences_(formula.variableCount, 0),
        negOccurrences_(formula.variableCount, 0) {
    computeOccurrenceCounts();
    initWatches();
  }

  [[nodiscard]]
  bool solve() {
    return search();
  }

  [[nodiscard]]
  const std::vector<Value> &assignment() const noexcept {
    return assignment_;
  }

  [[nodiscard]]
  const SolverStats &stats() const noexcept {
    return stats_;
  }

  void printAssignment(std::ostream &out) const {
    for (std::uint32_t i = 0; i < assignment_.size(); ++i) {
      if (assignment_[i] == Value::Unassigned) {
        throw std::logic_error(
            "SAT assignment contains an unassigned variable");
      }

      if (i != 0) {
        out << ' ';
      }

      out << (assignment_[i] == Value::True
                  ? static_cast<std::int64_t>(i + 1)
                  : -static_cast<std::int64_t>(i + 1));
    }

    out << '\n';
  }

private:
  const Formula &formula_;

  // Mutable copy of the literal array: watched-literal swaps reorder the
  // first two literals of each clause's window in place.
  std::vector<lit::Literal> literals_;

  std::vector<Value> assignment_;

  // Trail of assigned literals, in assignment order.
  std::vector<lit::Literal> trail_;

  // trailLim_[k] is the trail index of the decision literal that opened
  // decision level k+1 (0 = no decisions yet / unit propagation only).
  std::vector<std::size_t> trailLim_;

  // decisionTried_[k] is false while only the positive branch of that
  // decision has been tried, true once we've flipped to the negative one.
  std::vector<bool> decisionTried_;

  // watches_[p.val()] holds indices of clauses for which literal p.negate()
  // is currently a watched literal. When p becomes true, those clauses
  // need to find a new literal to watch (or the clause becomes unit/false).
  std::vector<std::vector<std::uint32_t>> watches_;

  std::deque<lit::Literal> propQ_;

  // Static occurrence counts per variable, used by chooseVariable() as a
  // cheap DLCS-style ("Dynamic Largest Combined Sum") branching heuristic.
  std::vector<std::uint32_t> posOccurrences_;
  std::vector<std::uint32_t> negOccurrences_;

  SolverStats stats_;

  void computeOccurrenceCounts() {
    for (const lit::Literal literal : literals_) {
      const auto variable = literal.variable().getValue();

      if (literal.isPositive()) {
        ++posOccurrences_[variable];
      } else {
        ++negOccurrences_[variable];
      }
    }
  }

  void initWatches() {
    for (std::uint32_t ci = 0; ci < formula_.clauses.size(); ++ci) {
      const Clause &clause = formula_.clauses[ci];

      if (clause.size >= 2) {
        watch(literals_[clause.begin].negate(), ci);
        watch(literals_[clause.begin + 1].negate(), ci);
      }
    }
  }

  void watch(lit::Literal trigger, std::uint32_t clauseIndex) {
    watches_[trigger.val()].push_back(clauseIndex);
  }

  [[nodiscard]]
  Value valueOf(lit::Literal literal) const noexcept {
    const auto variable = literal.variable().getValue();

    const Value value = assignment_[variable];

    if (value == Value::Unassigned) {
      return Value::Unassigned;
    }

    if (literal.isPositive()) {
      return value;
    }

    return value == Value::True ? Value::False : Value::True;
  }

  // Assigns `literal` true. Returns false if it's already false under the
  // current assignment (conflict); true if it was already true, or if the
  // assignment succeeded. `fromPropagation` only affects stats bookkeeping.
  bool enqueue(lit::Literal literal, bool fromPropagation) noexcept {
    const Value current = valueOf(literal);

    if (current == Value::True) {
      return true;
    }

    if (current == Value::False) {
      return false;
    }

    const auto variable = literal.variable().getValue();

    assignment_[variable] = literal.isPositive() ? Value::True : Value::False;

    trail_.push_back(literal);
    propQ_.push_back(literal);

    if (fromPropagation) {
      ++stats_.unitPropagations;
    }

    return true;
  }

  // Fixes up clauses watching `negate(literal)` now that `literal` just
  // became true. Returns false and empties propQ_ on conflict.
  [[nodiscard]]
  bool propagate() {
    while (!propQ_.empty()) {
      const lit::Literal literal = propQ_.front();
      propQ_.pop_front();

      std::vector<std::uint32_t> pending;
      pending.swap(watches_[literal.val()]);

      for (std::size_t i = 0; i < pending.size(); ++i) {
        if (!propagateClause(pending[i], literal)) {
          for (std::size_t k = i + 1; k < pending.size(); ++k) {
            watches_[literal.val()].push_back(pending[k]);
          }

          propQ_.clear();
          return false;
        }
      }
    }

    return true;
  }

  // `literal` just became true, so watchedNegation = literal.negate() is a
  // watched literal of this clause that just became false. Try to find a
  // replacement watched literal; if none exists, the other watched literal
  // must be enqueued (unit propagation) or we have a conflict.
  [[nodiscard]]
  bool propagateClause(std::uint32_t clauseIndex, lit::Literal literal) {
    const Clause &clause = formula_.clauses[clauseIndex];
    const lit::Literal watchedNegation = literal.negate();

    // Ensure the falsified watched literal sits at offset 1, so offset 0
    // holds the other watched literal.
    if (literals_[clause.begin] == watchedNegation) {
      std::swap(literals_[clause.begin], literals_[clause.begin + 1]);
    }

    const lit::Literal other = literals_[clause.begin];

    if (valueOf(other) == Value::True) {
      // Clause already satisfied by the other watched literal. We're still
      // watching `watchedNegation`, so re-register under its trigger,
      // which is `literal` (== watchedNegation.negate()).
      watch(literal, clauseIndex);
      return true;
    }

    for (std::uint32_t i = 2; i < clause.size; ++i) {
      
      lit::Literal &candidate = literals_[clause.begin + i];

      if (valueOf(candidate) != Value::False) {
        std::swap(literals_[clause.begin + 1], candidate);
        watch(literals_[clause.begin + 1].negate(), clauseIndex);
        return true;
      }
    }

    // No replacement literal available: clause is unit on `other`, or
    // false if `other` is already assigned false. Still watching
    // `watchedNegation`, so re-register under trigger `literal`.
    watch(literal, clauseIndex);
    return enqueue(other, /*fromPropagation=*/true);
  }

  // Removes the most recent literal from the trail without touching
  // decision-level bookkeeping.
  void undoOne() noexcept {
    const auto literal = trail_.back();
    trail_.pop_back();
    assignment_[literal.variable().getValue()] = Value::Unassigned;
  }

  // On conflict: undo the current decision level and try its other
  // branch if untried; otherwise pop the level and keep going up. Returns
  // false once every decision level has been exhausted (UNSAT).
  [[nodiscard]]
  bool backtrack() noexcept {
    while (!trailLim_.empty()) {
      const std::size_t levelStart = trailLim_.back();
      const lit::Literal decisionLiteral = trail_[levelStart];

      while (trail_.size() > levelStart) {
        undoOne();
      }

      if (!decisionTried_.back()) {
        decisionTried_.back() = true;
        propQ_.clear();

        // The decision variable is unassigned again, so this always
        // succeeds; it re-enters propQ_ for the caller's propagate().
        enqueue(decisionLiteral.negate(), /*fromPropagation=*/false);

        return true;
      }

      trailLim_.pop_back();
      decisionTried_.pop_back();
    }

    return false;
  }

  [[nodiscard]]
  bool search() {
    // Seed unit clauses from the original formula before any decisions.
    for (const Clause &clause : formula_.clauses) {
      if (clause.size == 0) {
        return false;
      }

      if (clause.size == 1 &&
          !enqueue(literals_[clause.begin], /*fromPropagation=*/false)) {
        return false;
      }
    }

    while (true) {
      if (!propagate()) {
        if (!backtrack()) {
          return false;
        }

        continue;
      }

      if (trail_.size() == assignment_.size()) {
        return true;
      }

      const auto decisionLiteral = chooseDecisionLiteral();

      ++stats_.decisionVertices;

      trailLim_.push_back(trail_.size());
      decisionTried_.push_back(false);

      enqueue(decisionLiteral, /*fromPropagation=*/false);
    }
  }

  // DLCS-style heuristic: among unassigned variables, pick the one with
  // the highest combined literal occurrence count in the original formula,
  // then branch first on whichever polarity occurs more often for it.
  [[nodiscard]]
  lit::Literal chooseDecisionLiteral() const {
    std::uint32_t bestVariable = assignment_.size();
    std::uint32_t bestScore = 0;

    for (std::uint32_t i = 0; i < assignment_.size(); ++i) {
      if (assignment_[i] != Value::Unassigned) {
        continue;
      }

      const std::uint32_t score = posOccurrences_[i] + negOccurrences_[i];

      if (bestVariable == assignment_.size() || score > bestScore) {
        bestVariable = i;
        bestScore = score;
      }
    }

    if (bestVariable == assignment_.size()) {
      throw std::logic_error(
          "chooseDecisionLiteral(): no unassigned variables");
    }

    const auto variable = var::Variable::fromValue(bestVariable);

    return posOccurrences_[bestVariable] >= negOccurrences_[bestVariable]
               ? lit::positive(variable)
               : lit::negative(variable);
  }
};

namespace dimacs {

Formula parse(const char *path) {
  std::ifstream file(path);

  if (!file) {
    throw std::runtime_error(std::string{"Failed to open DIMACS file: "} +
                             path);
  }

  std::string line;

  bool headerFound = false;

  std::uint32_t variableCount = 0;
  std::uint32_t clauseCount = 0;

  Formula formula{.variableCount = 0, .literals = {}, .clauses = {}};

  std::size_t clauseBegin = 0;

  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }

    const std::string_view view{line};

    if (view.front() == 'c') {
      continue;
    }

    if (view.front() == 'p') {
      if (headerFound) {
        throw std::runtime_error("Duplicate DIMACS header");
      }

      char p;
      std::string type;

      std::istringstream iss{line};

      if (!(iss >> p >> type >> variableCount >> clauseCount)) {
        throw std::runtime_error("Malformed DIMACS header");
      }

      if (p != 'p' || type != "cnf") {
        throw std::runtime_error("Only DIMACS CNF is supported");
      }

      formula.variableCount = variableCount;

      formula.clauses.reserve(clauseCount);

      headerFound = true;

      continue;
    }

    if (!headerFound) {
      throw std::runtime_error("CNF data encountered before DIMACS header");
    }

    std::istringstream iss{line};
    std::int64_t value;

    while (iss >> value) {
      // 0 terminates the current clause.
      if (value == 0) {
        const auto size = formula.literals.size() - clauseBegin;

        if (clauseBegin > std::numeric_limits<std::uint32_t>::max()) {
          throw std::runtime_error("Formula is too large");
        }

        if (size > std::numeric_limits<std::uint32_t>::max()) {
          throw std::runtime_error("Clause is too large");
        }

        formula.clauses.push_back(
            {.begin = static_cast<std::uint32_t>(clauseBegin),
             .size = static_cast<std::uint32_t>(size)});

        if (formula.clauses.size() > clauseCount) {
          throw std::runtime_error("More clauses than specified "
                                   "in DIMACS header");
        }

        clauseBegin = formula.literals.size();

        continue;
      }

      const bool negative = value < 0;

      const auto magnitude = negative
                                 ? static_cast<std::uint64_t>(-(value + 1)) + 1
                                 : static_cast<std::uint64_t>(value);

      if (magnitude == 0) {
        throw std::runtime_error("Invalid DIMACS literal");
      }

      const auto variable = magnitude - 1;

      if (variable >= variableCount) {
        throw std::runtime_error("DIMACS literal references variable "
                                 "outside declared range");
      }

      if (variable > (std::numeric_limits<std::uint32_t>::max() >> 1)) {
        throw std::runtime_error("Variable index is too large");
      }

      const auto encoded =
          (variable << 1) | static_cast<std::uint64_t>(negative);

      formula.literals.push_back(
          lit::Literal::fromValue(static_cast<std::uint32_t>(encoded)));
    }

    // Detect garbage such as:
    //
    // 1 -2 hello 0
    //
    // instead of silently accepting the valid prefix.
    if (!iss.eof()) {
      throw std::runtime_error("Invalid token in DIMACS clause");
    }
  }

  if (!headerFound) {
    throw std::runtime_error("Missing DIMACS header");
  }

  // A clause must always end in 0.
  if (clauseBegin != formula.literals.size()) {
    throw std::runtime_error("Last DIMACS clause is not terminated by 0");
  }

  if (formula.clauses.size() != clauseCount) {
    throw std::runtime_error("Number of clauses does not match "
                             "DIMACS header");
  }

  return formula;
}

} // namespace dimacs

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: solver input.cnf\n";
    return EXIT_FAILURE;
  }

  try {
    // Loading + initialisation.
    const std::clock_t initStart = std::clock();
    const Formula formula = dimacs::parse(argv[1]);
    Solver solver{formula};
    const std::clock_t initEnd = std::clock();

    // Solving.
    const std::clock_t solveStart = std::clock();
    const bool satisfiable = solver.solve();
    const std::clock_t solveEnd = std::clock();

    const auto initTime = (initEnd - initStart);

    const auto solveTime = (solveEnd - solveStart);

    // Required output.
    std::cout << (satisfiable ? "SAT\n" : "UNSAT\n");

    if (satisfiable) {
      solver.printAssignment(std::cout);
    } else {
      std::cout << '\n';
    }

    std::cout << initTime << '\n';
    std::cout << solveTime << '\n';
    std::cout << solver.stats().unitPropagations << '\n';
    std::cout << solver.stats().decisionVertices << '\n';

    return EXIT_SUCCESS;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
