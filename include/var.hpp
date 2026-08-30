#pragma once
#include <cstdint>

class Variable {
public:
  using valueT = std::uint32_t;

private:
  valueT value_;

public:
  constexpr explicit Variable(valueT value) noexcept: value_(value) {}

  [[nodiscard]] constexpr valueT operator()() const noexcept { return value_; }

  friend constexpr bool operator==(Variable, Variable) = default;
};
