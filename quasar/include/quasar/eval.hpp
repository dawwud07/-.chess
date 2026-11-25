#pragma once

#include "position.hpp"

namespace quasar {

void initEval();

// Static evaluation from the point of view of the side to move, in centipawns.
int evaluate(const Position& pos);

}  // namespace quasar
