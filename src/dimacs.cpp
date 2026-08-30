#include "dimacs.hpp"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <istream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

using cstrp_t = const char*;

bool isWhitespace(char character) noexcept {
  return std::isspace(static_cast<unsigned char>(character)) != 0;
}

void skipWhitespace(cstrp_t& cursor, cstrp_t end) noexcept {
  while (cursor != end && isWhitespace(*cursor)) { ++cursor; }
}

[[nodiscard]] bool onlyWhitespace(cstrp_t cursor, cstrp_t end) noexcept {
  skipWhitespace(cursor, end);
  return cursor == end;
}

[[nodiscard]] std::string_view readToken(cstrp_t& cursor, cstrp_t end) noexcept {
  skipWhitespace(cursor, end);
  cstrp_t begin = cursor;
  while (cursor != end && !isWhitespace(*cursor)) { ++cursor; }
  return {begin, static_cast<std::size_t>(cursor - begin)};
}

[[nodiscard]] bool readUnsigned(cstrp_t& cursor, cstrp_t end, std::uint32_t& value) noexcept {
  skipWhitespace(cursor, end);

  if (cursor == end) { return false; }

  if (*cursor == '+') { ++cursor; }

  const auto [next, error] = std::from_chars(cursor, end, value);

  if (error != std::errc {}) { return false; }

  cursor = next;
  return true;
}

[[nodiscard]] bool readLiteral(cstrp_t& cursor, cstrp_t end, std::int64_t& value) noexcept {
  skipWhitespace(cursor, end);

  if (cursor == end) { return false; }

  if (*cursor == '+') { ++cursor; }

  const auto [next, error] = std::from_chars(cursor, end, value);

  if (error != std::errc {}) { return false; }

  cursor = next;
  return true;
}

} // namespace

namespace dimacs {

Formula parse(std::istream& input) {

  std::string            line;
  bool                   headerFound         = false;
  std::uint32_t          variableCount       = 0;
  std::uint32_t          expectedClauseCount = 0;
  std::optional<Formula> formula;
  std::size_t            clauseBegin = 0;

  while (std::getline(input, line)) {

	cstrp_t       cursor = line.data();
	cstrp_t const end    = cursor + line.size();
	skipWhitespace(cursor, end);

	if (cursor == end || *cursor == 'c') { continue; }

	if (*cursor == 'p') {

	  if (headerFound) { throw std::runtime_error("Duplicate DIMACS header"); }

	  ++cursor;

	  if (cursor == end || !isWhitespace(*cursor)) {
		throw std::runtime_error("Malformed DIMACS CNF header");
	  }

	  const std::string_view type = readToken(cursor, end);

	  if (type != "cnf" || !readUnsigned(cursor, end, variableCount) ||
	      !readUnsigned(cursor, end, expectedClauseCount) || !onlyWhitespace(cursor, end)) {
		throw std::runtime_error("Malformed DIMACS CNF header");
	  }

	  if (variableCount > (std::numeric_limits<std::uint32_t>::max() >> 1)) {
		throw std::runtime_error("DIMACS formula has too many variables");
	  }

	  formula.emplace(variableCount);
	  formula->reserveClauses(expectedClauseCount);
	  headerFound = true;
	  continue;
	}

	if (!headerFound) { throw std::runtime_error("CNF data encountered before DIMACS header"); }

	while (true) {

	  skipWhitespace(cursor, end);

	  if (cursor == end) { break; }

	  std::int64_t value;

	  if (!readLiteral(cursor, end, value)) {
		throw std::runtime_error("Invalid token in DIMACS clause");
	  }

	  if (value == 0) {
		formula->closeClause(clauseBegin);

		if (formula->clauseCount() > expectedClauseCount) {
		  throw std::runtime_error("More clauses than declared in DIMACS header");
		}

		clauseBegin = formula->literalCount();
		continue;
	  }

	  const bool          negative      = value < 0;
	  const std::uint64_t magnitude     = negative ? static_cast<std::uint64_t>(-(value + 1)) + 1
	                                               : static_cast<std::uint64_t>(value);
	  const std::uint64_t variableIndex = magnitude - 1;

	  if (variableIndex >= variableCount) {
		throw std::runtime_error("DIMACS literal references a variable outside the declared range");
	  }

	  formula->appendLiteral(
		  Literal {Variable {static_cast<Variable::valueT>(variableIndex)}, negative}
	  );
	}
  }

  if (input.bad()) { throw std::runtime_error("Failed while reading DIMACS input"); }

  if (!headerFound) { throw std::runtime_error("Missing DIMACS header"); }

  if (clauseBegin != formula->literalCount()) {
	throw std::runtime_error("Last DIMACS clause is not terminated by 0");
  }

  if (formula->clauseCount() != expectedClauseCount) {
	throw std::runtime_error("Number of clauses does not match DIMACS header");
  }

  return std::move(*formula);
}

Formula parseFile(const std::filesystem::path& path) {
  std::ifstream file {path};

  if (!file) { throw std::runtime_error("Failed to open DIMACS file: " + path.string()); }

  return parse(file);
}

} // namespace dimacs
