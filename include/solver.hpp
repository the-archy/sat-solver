#pragma once

#include "formula.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <vector>

struct SolverStats {
  std::size_t unitPropagations = 0;
  std::size_t decisionVertices = 0;
};

class Solver {
public:
  enum class Value : std::uint8_t { False, True, Unassigned };

  explicit Solver(const Formula& formula);
  ~Solver();

  Solver(const Solver&) = delete;
  Solver& operator=(const Solver&) = delete;
  Solver(Solver&&) noexcept;
  Solver& operator=(Solver&&) noexcept;

  [[nodiscard]] bool solve();
  [[nodiscard]] const std::vector<Value>& assignment() const noexcept;
  [[nodiscard]] const SolverStats& stats() const noexcept;

  void printAssignment(std::ostream& output) const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
