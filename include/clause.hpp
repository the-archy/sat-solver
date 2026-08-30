#pragma once
#include <cstdint>

class Clause {

public:
  using indexT = std::uint32_t;

private:
  indexT firstLiteral_;
  indexT literalCount_;

public:
  constexpr Clause(indexT firstLit, indexT litCount) noexcept
	  : firstLiteral_(firstLit), literalCount_(litCount) {}

  [[nodiscard]] constexpr indexT firstLiteral() const noexcept { return firstLiteral_; }
  [[nodiscard]] constexpr indexT literalCount() const noexcept { return literalCount_; }
  [[nodiscard]] constexpr bool   isEmpty() const noexcept { return literalCount_ == 0; }
};