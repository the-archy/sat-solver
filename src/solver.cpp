#include "solver.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

// Iterative watched-literal DPLL, following lecture-log.pdf, seminar 7
// ("Zaklady implementace DPLL v solverech", a simplified MiniSat). The private
// methods below map 1:1 to the pseudocode there: search / assume / enqueue /
// propagate / propagate-clause / undo-one / backtrack. Plain chronological
// backtracking only -- no clause learning / non-chronological backtracking
// (that is CDCL, seminar 8).
class Solver::Impl {
public:
  explicit Impl(const Formula& formula)
      : formula_(formula), literals_(formula.literals()),
        assignment_(formula.variableCount(), Value::Unassigned),
        watches_(static_cast<std::size_t>(formula.variableCount()) * 2),
        posOccurrences_(formula.variableCount(), 0),
        negOccurrences_(formula.variableCount(), 0) {
    computeOccurrenceCounts();
    initWatches();
  }

  [[nodiscard]] bool solve() {
    return search();
  }

  [[nodiscard]] const std::vector<Value>& assignment() const noexcept {
    return assignment_;
  }

  [[nodiscard]] const SolverStats& stats() const noexcept {
    return stats_;
  }

  void printAssignment(std::ostream& output) const {
    for (std::size_t i = 0; i < assignment_.size(); ++i) {
      if (assignment_[i] == Value::Unassigned) {
        throw std::logic_error("SAT assignment contains an unassigned variable");
      }

      if (i != 0) {
        output << ' ';
      }

      output << (assignment_[i] == Value::True
                     ? static_cast<std::int64_t>(i + 1)
                     : -static_cast<std::int64_t>(i + 1));
    }

    output << '\n';
  }

private:
  const Formula& formula_;
  // Solver-local copy of the literal pool; the two watched literals of each
  // clause are the first two entries of its window and get swapped in place.
  std::vector<Literal> literals_;
  std::vector<Value> assignment_;          // "assigns": True / False / Unassigned per variable
  std::vector<Literal> trail_;             // literals assigned True, in assignment order
  std::vector<std::size_t> trailLimits_;   // "trail-lim": trail index of each decision literal
  // Per decision level, stands in for the log's per-variable "tries" counter:
  // has the opposite phase of this decision been tried yet?
  std::vector<bool> decisionTried_;
  // watches_[l] holds every clause in which neg(l) is a watched literal.
  std::vector<std::vector<std::uint32_t>> watches_;
  std::deque<Literal> propagationQueue_;   // "prop-q": literals set True, awaiting propagate()
  // Static literal-occurrence counts for chooseDecisionLiteral() (DLCS-style).
  std::vector<std::uint32_t> posOccurrences_;
  std::vector<std::uint32_t> negOccurrences_;
  SolverStats stats_;
  // (The log's per-variable "levels" array is only needed for CDCL conflict
  // analysis, so plain DPLL omits it.)

  void computeOccurrenceCounts() {
    for (const Literal literal : literals_) {
      const auto variable = literal.variable()();

      if (static_cast<bool>(literal)) {
        ++posOccurrences_[variable];
      } else {
        ++negOccurrences_[variable];
      }
    }
  }

  // Register every clause of >= 2 literals under neg() of its first two literals.
  void initWatches() {
    const auto& clauses = formula_.clauses();

    for (std::size_t i = 0; i < clauses.size(); ++i) {
      const Clause& clause = clauses[i];

      if (clause.literalCount() >= 2) {
        const auto clauseIndex = static_cast<std::uint32_t>(i);
        const auto first = clause.firstLiteral();

        watch(!literals_[first], clauseIndex);
        watch(!literals_[first + 1], clauseIndex);
      }
    }
  }

  void watch(Literal trigger, std::uint32_t clauseIndex) {
    watches_[trigger.getValue()].push_back(clauseIndex);
  }

  [[nodiscard]] Value valueOf(Literal literal) const noexcept {
    const Value value = assignment_[literal.variable()()];

    if (value == Value::Unassigned) {
      return Value::Unassigned;
    }

    if (static_cast<bool>(literal)) {
      return value;
    }

    return value == Value::True ? Value::False : Value::True;
  }

  // "enqueue": assign the literal True (return false on conflict, true if it was
  // already True), pushing it onto the trail and the propagation queue.
  // fromPropagation only drives the unit-propagation counter (output line 5).
  [[nodiscard]] bool enqueue(Literal literal, bool fromPropagation) noexcept {
    const Value current = valueOf(literal);

    if (current == Value::True) {
      return true;
    }

    if (current == Value::False) {
      return false;
    }

    assignment_[literal.variable()()] =
        static_cast<bool>(literal) ? Value::True : Value::False;
    trail_.push_back(literal);
    propagationQueue_.push_back(literal);

    if (fromPropagation) {
      ++stats_.unitPropagations;
    }

    return true;
  }

  // "propagate": drain prop-q; for each literal p revisit every clause in
  // watches_[p]. On conflict, return the not-yet-visited clauses to watches_[p],
  // clear prop-q and report failure.
  [[nodiscard]] bool propagate() {
    while (!propagationQueue_.empty()) {
      const Literal literal = propagationQueue_.front();
      propagationQueue_.pop_front();

      std::vector<std::uint32_t> pending;
      pending.swap(watches_[literal.getValue()]);

      for (std::size_t i = 0; i < pending.size(); ++i) {
        if (!propagateClause(pending[i], literal)) {
          for (std::size_t remaining = i + 1; remaining < pending.size();
               ++remaining) {
            watches_[literal.getValue()].push_back(pending[remaining]);
          }

          propagationQueue_.clear();
          return false;
        }
      }
    }

    return true;
  }

  // "propagate-clause": neg(literal) is a watched literal of this clause and has
  // just become False. Move it to slot 1, then try to swap in a non-False
  // replacement from slot 2 onward. If none exists, the other watch (slot 0) is
  // forced -- enqueue it, or report the conflict.
  [[nodiscard]] bool propagateClause(std::uint32_t clauseIndex,
                                     Literal literal) {
    const Clause& clause = formula_.clauses()[clauseIndex];
    const auto first = clause.firstLiteral();
    const Literal watchedNegation = !literal;

    if (literals_[first] == watchedNegation) {
      std::swap(literals_[first], literals_[first + 1]);
    }

    const Literal other = literals_[first];

    if (valueOf(other) == Value::True) {
      // Clause already satisfied. Keep watching neg(literal) by re-adding it to
      // watches_[literal]; the log's pseudocode omits this re-add and so loses
      // the watch.
      watch(literal, clauseIndex);
      return true;
    }

    for (std::uint32_t offset = 2; offset < clause.literalCount(); ++offset) {
      Literal& candidate = literals_[first + offset];

      if (valueOf(candidate) != Value::False) {
        std::swap(literals_[first + 1], candidate);
        watch(!literals_[first + 1], clauseIndex);
        return true;
      }
    }

    watch(literal, clauseIndex);
    return enqueue(other, true);
  }

  // "undo-one": pop the newest trail literal and unassign it. Unlike the log,
  // trail-lim is popped by backtrack(), not here.
  void undoOne() noexcept {
    const Literal literal = trail_.back();
    trail_.pop_back();
    assignment_[literal.variable()()] = Value::Unassigned;
  }

  // "backtrack": walk decision levels from the top. At each level undo the trail
  // down to and including the decision literal; if its opposite phase is still
  // untried, enqueue that phase and return true. If every level is exhausted the
  // formula is UNSAT. Note we unassign the decision literal *before*
  // enqueue(!decisionLiteral), so that call actually takes (the log's pseudocode
  // leaves it assigned, which would make the enqueue fail).
  [[nodiscard]] bool backtrack() noexcept {
    while (!trailLimits_.empty()) {
      const std::size_t levelStart = trailLimits_.back();
      const Literal decisionLiteral = trail_[levelStart];

      while (trail_.size() > levelStart) {
        undoOne();
      }

      if (!decisionTried_.back()) {
        decisionTried_.back() = true;
        propagationQueue_.clear();
        return enqueue(!decisionLiteral, false);
      }

      trailLimits_.pop_back();
      decisionTried_.pop_back();
    }

    return false;
  }

  [[nodiscard]] bool search() {
    // Not in the log's search() pseudocode: reject empty clauses and seed unit
    // clauses into prop-q. initWatches() only watches clauses of >= 2 literals,
    // so units would otherwise never be propagated.
    for (const Clause& clause : formula_.clauses()) {
      if (clause.isEmpty()) {
        return false;
      }

      if (clause.literalCount() == 1 &&
          !enqueue(literals_[clause.firstLiteral()], false)) {
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

      // "assume p": open a new decision level, then enqueue the chosen literal.
      const Literal decision = chooseDecisionLiteral();
      ++stats_.decisionVertices;
      trailLimits_.push_back(trail_.size());
      decisionTried_.push_back(false);

      if (!enqueue(decision, false)) {
        return false;
      }
    }
  }

  // Static DLCS-style branching: among unassigned variables pick the one with
  // the most literal occurrences in the original formula, then branch on its
  // more frequent polarity. The log describes MiniSat's dynamic activity/VSIDS
  // heuristic instead; it lets any DPLL heuristic be used, and this one is cheap
  // but never updated as clauses get satisfied.
  [[nodiscard]] Literal chooseDecisionLiteral() const {
    std::uint32_t bestVariable =
        static_cast<std::uint32_t>(assignment_.size());
    std::uint32_t bestScore = 0;

    for (std::uint32_t variable = 0; variable < assignment_.size();
         ++variable) {
      if (assignment_[variable] != Value::Unassigned) {
        continue;
      }

      const std::uint32_t score =
          posOccurrences_[variable] + negOccurrences_[variable];

      if (bestVariable == assignment_.size() || score > bestScore) {
        bestVariable = variable;
        bestScore = score;
      }
    }

    if (bestVariable == assignment_.size()) {
      throw std::logic_error("No unassigned decision variable");
    }

    return Literal{
        Variable{bestVariable},
        posOccurrences_[bestVariable] < negOccurrences_[bestVariable]};
  }
};

Solver::Solver(const Formula& formula) : impl_(std::make_unique<Impl>(formula)) {}
Solver::~Solver() = default;
Solver::Solver(Solver&&) noexcept = default;
Solver& Solver::operator=(Solver&&) noexcept = default;

bool Solver::solve() {
  return impl_->solve();
}

const std::vector<Solver::Value>& Solver::assignment() const noexcept {
  return impl_->assignment();
}

const SolverStats& Solver::stats() const noexcept {
  return impl_->stats();
}

void Solver::printAssignment(std::ostream& output) const {
  impl_->printAssignment(output);
}
