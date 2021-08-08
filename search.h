#pragma once

#include <vector>

#include "tetris_common.h"

struct Solution {
  std::vector<Action> actions;
  Situation final_situ;
  std::vector<unsigned> score_by_step;
};

Solution Solve();
