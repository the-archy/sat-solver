#pragma once

#include "clause.hpp"
#include "lit.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

class Formula {
public:
  using countT = std::uint32_t;

private:
  countT               variableCount_ = 0;
  std::vector<Literal> literals_;
  std::vector<Clause>  clauses_;

public:
  constexpr explicit Formula(countT variableCount) noexcept
      : variableCount_(variableCount) {}

  [[nodiscard]] constexpr countT      variableCount() const noexcept { return variableCount_; }
  [[nodiscard]] constexpr std::size_t literalCount() const noexcept { return literals_.size(); }
  [[nodiscard]] constexpr std::size_t clauseCount() const noexcept { return clauses_.size(); }
  [[nodiscard]] const std::vector<Literal>& literals() const noexcept { return literals_; }
  [[nodiscard]] const std::vector<Clause>&  clauses() const noexcept { return clauses_; }

  void reserveClauses(std::size_t count) { clauses_.reserve(count); }

  void appendLiteral(Literal literal) { literals_.push_back(literal); }

  void closeClause(std::size_t firstLiteralIdx) {
    
    if (firstLiteralIdx > literals_.size()) {
      throw std::logic_error("Clause starts outside the formula");
    }

    const std::size_t literalCount = literals_.size() - firstLiteralIdx;

    if (firstLiteralIdx > std::numeric_limits<Clause::indexT>::max() ||
        literalCount > std::numeric_limits<Clause::indexT>::max()) {
      throw std::length_error("Formula or clause is too large");
    }

    clauses_.emplace_back(
        static_cast<Clause::indexT>(firstLiteralIdx),
        static_cast<Clause::indexT>(literalCount));
  }

  void addClause(std::span<const Literal> clauseLiterals) {
    const auto first = literals_.size();

    if (first > std::numeric_limits<Clause::indexT>::max() ||
        clauseLiterals.size() >
            std::numeric_limits<Clause::indexT>::max() - first) {
      throw std::length_error("Formula or clause is too large");
    }

    literals_.insert(
        literals_.end(),
        clauseLiterals.begin(),
        clauseLiterals.end()
    );

    closeClause(first);
  }
};
