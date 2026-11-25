#include "quasar/bench.hpp"
#include "quasar/bitboard.hpp"
#include "quasar/eval.hpp"
#include "quasar/movegen.hpp"
#include "quasar/position.hpp"
#include "quasar/uci.hpp"
#include "quasar/zobrist.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#ifndef QUASAR_VERSION
#define QUASAR_VERSION "1.0.0"
#endif

namespace {

void printUsage() {
    std::cout << "Quasar " << QUASAR_VERSION << " - a modern C++20 chess engine\n\n"
              << "Usage:\n"
              << "  quasar                 start the UCI engine\n"
              << "  quasar --repl          start an interactive console\n"
              << "  quasar bench [depth]   run the built-in benchmark\n"
              << "  quasar perft [depth]   run perft from the start position\n"
              << "  quasar --version       print the version\n"
              << "  quasar --help          show this help\n";
}

int parseDepth(const char* text, int fallback) {
    try {
        return std::stoi(text);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";

    if (command == "--help" || command == "-h") {
        printUsage();
        return EXIT_SUCCESS;
    }
    if (command == "--version" || command == "-v") {
        std::cout << "Quasar " << QUASAR_VERSION << '\n';
        return EXIT_SUCCESS;
    }

    quasar::bb::init();
    quasar::zobrist::init();
    quasar::initEval();

    if (command == "bench") {
        const int depth = argc > 2 ? parseDepth(argv[2], 8) : 8;
        quasar::runBench(depth);
        return EXIT_SUCCESS;
    }

    if (command == "perft") {
        const int depth = argc > 2 ? parseDepth(argv[2], 5) : 5;
        quasar::Position position;
        const auto start = std::chrono::steady_clock::now();
        const std::uint64_t nodes = quasar::perft(position, depth);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        const auto timeMs = static_cast<std::uint64_t>(elapsed.count());
        const std::uint64_t nps = timeMs > 0 ? (nodes * 1000ULL) / timeMs : 0;
        std::cout << "perft " << depth << " = " << nodes << " nodes in " << timeMs
                  << " ms (" << nps << " nps)\n";
        return EXIT_SUCCESS;
    }

    if (command == "--repl" || command == "repl") {
        quasar::Uci uci(true);
        uci.loop();
        return EXIT_SUCCESS;
    }

    if (!command.empty()) {
        printUsage();
        return EXIT_FAILURE;
    }

    quasar::Uci uci(false);
    uci.loop();
    return EXIT_SUCCESS;
}
