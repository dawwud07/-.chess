#include "quasar/uci.hpp"

#include "quasar/bench.hpp"
#include "quasar/bitboard.hpp"
#include "quasar/eval.hpp"
#include "quasar/movegen.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef QUASAR_VERSION
#define QUASAR_VERSION "1.0.0"
#endif

namespace quasar {

namespace {

std::string lowerCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool sameMove(Move a, Move b) {
    return moveFrom(a) == moveFrom(b) && moveTo(a) == moveTo(b) &&
           movePromotion(a) == movePromotion(b);
}

std::string bestMoveLine(const SearchResult& result) {
    std::string line = "bestmove " + moveToUci(result.bestMove);
    if (result.ponderMove != MOVE_NONE) {
        line += " ponder " + moveToUci(result.ponderMove);
    }
    return line;
}

}  // namespace

Uci::Uci(bool interactive) : searcher_(64), interactive_(interactive) {}

Uci::~Uci() {
    stopSearch();
}

void Uci::loop() {
    if (interactive_) {
        std::lock_guard lock(outputMutex_);
        std::cout << "============================================================\n";
        std::cout << " Quasar " << QUASAR_VERSION << " - a modern C++20 chess engine\n";
        std::cout << "============================================================\n";
        std::cout << "Type 'help' for a list of commands.\n\n";
        std::cout << position_.toAscii() << '\n';
    }

    std::string line;
    while (true) {
        if (interactive_) {
            std::lock_guard lock(outputMutex_);
            std::cout << "quasar> " << std::flush;
        }
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        if (line == "quit") {
            stopSearch();
            break;
        }
        handleLine(line);
    }

    stopSearch();
}

void Uci::handleLine(const std::string& line) {
    std::istringstream input(line);
    std::string command;
    input >> command;
    std::string rest;
    std::getline(input, rest);
    if (!rest.empty() && rest.front() == ' ') {
        rest.erase(rest.begin());
    }

    if (command == "uci") {
        std::lock_guard lock(outputMutex_);
        std::cout << "id name Quasar " << QUASAR_VERSION << '\n';
        std::cout << "id author Quasar contributors\n";
        std::cout << "option name Hash type spin default 64 min 1 max 4096\n";
        std::cout << "uciok\n";
    } else if (command == "isready") {
        if (!searching_.load(std::memory_order_relaxed) && worker_.joinable()) {
            worker_.join();
        }
        std::lock_guard lock(outputMutex_);
        std::cout << "readyok\n";
    } else if (command == "ucinewgame") {
        stopSearch();
        searcher_.newGame();
        position_.setStartpos();
    } else if (command == "position") {
        stopSearch();
        handlePosition(rest);
    } else if (command == "go") {
        handleGo(rest);
    } else if (command == "stop") {
        stopSearch();
    } else if (command == "setoption") {
        stopSearch();
        handleSetOption(rest);
    } else if (command == "d" || command == "display") {
        printBoard();
    } else if (command == "eval") {
        printEvaluation();
    } else if (command == "perft") {
        int depth = 0;
        if (input >> depth) {
            stopSearch();
            printPerft(depth);
        } else {
            printInfoString("usage: perft <depth>");
        }
    } else if (command == "bench") {
        int depth = 8;
        if (input >> depth) {
            // parsed below
        }
        stopSearch();
        runBench(std::clamp(depth, 1, 15));
    } else if (command == "ponderhit" || command == "debug") {
        // No pondering is implemented; these commands are harmless.
    } else if (command == "help") {
        printHelp();
    } else {
        printInfoString("Unknown command: " + command);
    }
}

void Uci::handlePosition(const std::string& arguments) {
    std::istringstream input(arguments);
    std::string token;
    if (!(input >> token)) {
        printInfoString("position: missing argument");
        return;
    }

    const auto applyMoves = [&]() {
        std::string moveToken;
        while (input >> moveToken) {
            const Move parsed = parseUciMove(moveToken);
            MoveList legal;
            generateLegalMoves(position_, legal);
            bool found = false;
            for (const Move move : legal) {
                if (sameMove(move, parsed)) {
                    position_.makeMove(move);
                    found = true;
                    break;
                }
            }
            if (!found) {
                printInfoString("position: illegal move " + moveToken);
                break;
            }
        }
    };

    if (token == "startpos") {
        position_.setStartpos();
    } else if (token == "fen") {
        std::vector<std::string> fields;
        std::string field;
        bool sawMoves = false;
        while (input >> field) {
            if (field == "moves") {
                sawMoves = true;
                break;
            }
            fields.push_back(field);
        }
        if (fields.size() < 4 || fields.size() > 6) {
            printInfoString("position: expected a 4- or 6-field FEN");
            return;
        }
        std::string fen;
        for (const std::string& part : fields) {
            if (!fen.empty()) {
                fen += ' ';
            }
            fen += part;
        }
        if (fields.size() == 4) {
            fen += " 0 1";
        } else if (fields.size() == 5) {
            fen += " 1";
        }
        if (!position_.setFen(fen)) {
            printInfoString("position: invalid FEN");
            return;
        }
        if (sawMoves) {
            applyMoves();
        }
        return;
    } else {
        printInfoString("position: expected startpos or fen");
        return;
    }

    if (!(input >> token)) {
        return;
    }
    if (token != "moves") {
        printInfoString("position: expected moves");
        return;
    }
    applyMoves();
}

void Uci::handleGo(const std::string& arguments) {
    std::istringstream input(arguments);
    SearchLimits limits;
    bool hasLimit = false;
    std::string token;

    while (input >> token) {
        if (token == "wtime") {
            input >> limits.wtime;
            hasLimit = true;
        } else if (token == "btime") {
            input >> limits.btime;
            hasLimit = true;
        } else if (token == "winc") {
            input >> limits.winc;
            hasLimit = true;
        } else if (token == "binc") {
            input >> limits.binc;
            hasLimit = true;
        } else if (token == "movestogo") {
            input >> limits.movestogo;
            hasLimit = true;
        } else if (token == "depth") {
            input >> limits.depth;
            hasLimit = true;
        } else if (token == "nodes") {
            input >> limits.nodes;
            hasLimit = true;
        } else if (token == "movetime") {
            input >> limits.movetime;
            hasLimit = true;
        } else if (token == "infinite") {
            limits.infinite = true;
            hasLimit = true;
        } else if (token == "ponder") {
            limits.infinite = true;
            hasLimit = true;
        } else if (token == "perft") {
            int depth = 0;
            if (input >> depth) {
                stopSearch();
                printPerft(depth);
                return;
            }
        }
        // Unknown tokens (e.g. searchmoves or mate) are ignored.
    }

    if (!hasLimit) {
        limits.infinite = true;
    }
    startSearch(limits);
}

void Uci::handleSetOption(const std::string& arguments) {
    std::istringstream input(arguments);
    std::string token;
    std::string name;
    std::string value;
    bool readingName = true;

    while (input >> token) {
        if (token == "name") {
            readingName = true;
        } else if (token == "value") {
            readingName = false;
        } else if (readingName) {
            if (!name.empty()) {
                name += ' ';
            }
            name += token;
        } else {
            if (!value.empty()) {
                value += ' ';
            }
            value += token;
        }
    }

    const std::string option = lowerCopy(name);
    if (option == "hash") {
        try {
            const int megabytes = std::stoi(value);
            searcher_.setHash(static_cast<std::size_t>(std::clamp(megabytes, 1, 4096)));
        } catch (...) {
            printInfoString("setoption: invalid Hash value");
        }
    } else if (option == "clear hash") {
        searcher_.clear();
    } else {
        printInfoString("setoption: unsupported option " + name);
    }
}

void Uci::startSearch(const SearchLimits& limits) {
    stopSearch();
    searcher_.clearStop();
    searching_.store(true, std::memory_order_relaxed);

    Position searchPosition = position_;
    worker_ = std::thread([this, searchPosition = std::move(searchPosition), limits]() mutable {
        const SearchResult result =
            searcher_.search(searchPosition, limits,
                             [this](const SearchInfo& info) { printSearchInfo(info); });
        {
            std::lock_guard lock(outputMutex_);
            std::cout << bestMoveLine(result) << std::endl;
        }
        searching_.store(false, std::memory_order_relaxed);
    });
}

void Uci::stopSearch() {
    searcher_.stop();
    if (worker_.joinable()) {
        worker_.join();
    }
    searching_.store(false, std::memory_order_relaxed);
}

void Uci::printBoard() const {
    std::lock_guard lock(outputMutex_);
    std::cout << position_.toAscii() << '\n';
}

void Uci::printEvaluation() const {
    const int score = evaluate(position_);
    std::lock_guard lock(outputMutex_);
    std::cout << "info string evaluation " << std::showpos << score << std::noshowpos
              << " cp (side to move)" << '\n';
}

void Uci::printPerft(int depth) {
    if (depth < 0 || depth > 10) {
        printInfoString("perft: depth must be between 0 and 10");
        return;
    }

    Position copy = position_;
    const auto start = std::chrono::steady_clock::now();
    const std::uint64_t nodes = perft(copy, depth);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    const int timeMs = static_cast<int>(elapsed.count());
    const std::uint64_t nps =
        timeMs > 0 ? (nodes * 1000ULL) / static_cast<std::uint64_t>(timeMs) : 0;

    std::lock_guard lock(outputMutex_);
    std::cout << "perft " << depth << " = " << nodes << " nodes in " << timeMs << " ms ("
              << nps << " nps)\n";
}

void Uci::printSearchInfo(const SearchInfo& info) const {
    std::lock_guard lock(outputMutex_);
    std::cout << "info depth " << info.depth << " seldepth " << info.seldepth << " score ";
    if (info.score >= MATE_IN_MAX_PLY) {
        std::cout << "mate " << (MATE - info.score + 1) / 2;
    } else if (info.score <= -MATE_IN_MAX_PLY) {
        std::cout << "mate " << -((MATE + info.score + 1) / 2);
    } else {
        std::cout << "cp " << info.score;
    }
    const std::uint64_t nps =
        info.timeMs > 0 ? (info.nodes * 1000ULL) / static_cast<std::uint64_t>(info.timeMs) : 0;
    std::cout << " nodes " << info.nodes << " nps " << nps << " time " << info.timeMs
              << " hashfull " << info.hashfull << " pv";
    for (const Move move : info.pv) {
        std::cout << ' ' << moveToUci(move);
    }
    std::cout << std::endl;
}

void Uci::printHelp() const {
    std::lock_guard lock(outputMutex_);
    std::cout << "Commands:\n"
              << "  uci                  print UCI identification\n"
              << "  isready              answer with readyok\n"
              << "  position startpos|fen ... [moves ...]\n"
              << "  go [depth N] [movetime MS] [wtime ... btime ...] [infinite]\n"
              << "  stop                 stop the current search\n"
              << "  setoption name Hash value MB\n"
              << "  d                    display the current position\n"
              << "  eval                 static evaluation\n"
              << "  perft N              count legal move sequences\n"
              << "  bench [depth]        run the built-in benchmark\n"
              << "  ucinewgame           reset the engine\n"
              << "  quit                 exit\n";
}

void Uci::printInfoString(const std::string& text) const {
    std::lock_guard lock(outputMutex_);
    std::cout << "info string " << text << '\n';
}

}  // namespace quasar
