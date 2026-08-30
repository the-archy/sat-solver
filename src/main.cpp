#include "dimacs.hpp"
#include "solver.hpp"

#include <cstdlib>
#include <ctime>
#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: solver input.cnf\n";
    return EXIT_FAILURE;
  }

  try {
    const std::clock_t initStart = std::clock();
    const Formula formula = dimacs::parseFile(argv[1]);
    Solver solver{formula};
    const std::clock_t initEnd = std::clock();

    const std::clock_t solveStart = std::clock();
    const bool satisfiable = solver.solve();
    const std::clock_t solveEnd = std::clock();

    std::cout << (satisfiable ? "SAT\n" : "UNSAT\n");

    if (satisfiable) {
      solver.printAssignment(std::cout);
    } else {
      std::cout << '\n';
    }

    std::cout << (initEnd - initStart) << '\n';
    std::cout << (solveEnd - solveStart) << '\n';
    std::cout << solver.stats().unitPropagations << '\n';
    std::cout << solver.stats().decisionVertices << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
