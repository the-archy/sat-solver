#pragma once
#include "var.hpp"

#include <cstdint>

class Literal {

public:
  using valueT = std::uint32_t;

private:
  valueT value_;

public:
  constexpr explicit Literal(Variable variable, bool negative = false) noexcept
	  : value_(static_cast<valueT>((variable() << 1) | static_cast<valueT>(negative))) {}

  constexpr explicit Literal(valueT value) noexcept: value_(value) {}

  [[nodiscard]] constexpr valueT   getValue() const noexcept { return value_; }
  [[nodiscard]] constexpr Variable variable() const noexcept { return Variable(value_ >> 1); }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return !(value_ & 1u); }
  [[nodiscard]] constexpr Literal  operator!() const noexcept { return Literal {value_ ^ 1u}; }
  [[nodiscard]] constexpr bool     isNegative() const noexcept { return value_ & 1u; }

  friend constexpr bool operator==(Literal, Literal) = default;
};
