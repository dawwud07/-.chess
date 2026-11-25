#pragma once

#include "position.hpp"
#include "search.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace quasar {

class Uci {
public:
    explicit Uci(bool interactive = false);
    ~Uci();

    Uci(const Uci&) = delete;
    Uci& operator=(const Uci&) = delete;

    void loop();

private:
    void handleLine(const std::string& line);
    void handlePosition(const std::string& arguments);
    void handleGo(const std::string& arguments);
    void handleSetOption(const std::string& arguments);
    void startSearch(const SearchLimits& limits);
    void stopSearch();

    void printBoard() const;
    void printEvaluation() const;
    void printPerft(int depth);
    void printSearchInfo(const SearchInfo& info) const;
    void printHelp() const;
    void printInfoString(const std::string& text) const;

    Position position_;
    Searcher searcher_;
    std::thread worker_;
    std::atomic<bool> searching_{false};
    mutable std::mutex outputMutex_;
    bool interactive_ = false;
};

}  // namespace quasar
