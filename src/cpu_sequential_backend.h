#pragma once

#include "sim_backend.h"

#include <random>

namespace godot {

// baseling sequential CPU implementation
// scan rows bottom->top, columns left->right
// move down or diagonal
class CpuSequentialBackend : public SimBackend {
public:
  bool setup(int width, int height) override;
  void load(const std::vector<uint8_t> &cells) override;
  bool step() override;
  void read_back(std::vector<uint8_t> &out) override;
  const char *name() const override { return "cpu_sequential"; }

private:
  int width = 0;
  int height = 0;
  std::vector<uint8_t> cells;
  std::mt19937 rng;

  //convert coords 2D -> 1D
  inline int idx(int x, int y) const { return y * width + x; }
  //check if coords are in the grid
  inline bool in_grid(int x, int y) const {
    return x >= 0 && x < width && y >= 0 && y < height;
  }

  bool apply_sand_rules(int x, int y);
  bool try_move(int from_x, int from_y, int to_x, int to_y);
};

}
