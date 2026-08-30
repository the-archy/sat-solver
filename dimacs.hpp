#pragma once

#include "formula.hpp"

#include <filesystem>
#include <iosfwd>

namespace dimacs {

// Parses DIMACS CNF input. Throws std::runtime_error if the input is invalid.
[[nodiscard]] Formula parse(std::istream& input);

// Opens and parses a DIMACS CNF file.
[[nodiscard]] Formula parseFile(const std::filesystem::path& path);

} // namespace dimacs
