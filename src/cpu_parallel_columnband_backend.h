#pragma once

#include "sim_backend.h"

#include <atomic>
#include <mutex>
#include <random>
#include <vector>

namespace godot {

// columnwise version of parallel CPU implementation
// same rule as cpu sequential but the grid is split into vertical bands of COLUMNS with one thread/band
// made to fix the column problem with the cpu parallel backend, where columns full of sand would scatter and not fall straight down
// a thread only touches a column it doesn't own when a particle at the edge of a band wants to fall diagonally across the boundary:
// we guard those shared columns by one std::mutex each
// to avoid no lost/duplicated grains due to data races
// "interior" grains (away from boundary) don't use locks
class CpuParallelBackendColumnBand : public SimBackend {
public:
  // num_threads <= 0 -> use std::thread::hardware_concurrency()
  explicit CpuParallelBackendColumnBand(int num_threads = 0) : requested_threads(num_threads) {}

  bool setup(int width, int height) override;
  void load(const std::vector<uint8_t> &cells) override;
  bool step() override;
  void read_back(std::vector<uint8_t> &out) override;
  const char *name() const override { return "cpu_parallel_columnband"; }

private:
  int width = 0;
  int height = 0;
  int requested_threads = 0;
  int num_threads = 1;

  // single buffer updated in place (threads own disjoint columns)
  std::vector<uint8_t> cells;

  //rng for each thread
  std::vector<std::mt19937> rngs;

  // one mutex per boundary between two adjacent bands (num_threads - 1 of them)
  // guards the two columns making up the boundary
  std::vector<std::mutex> boundaries;

  //convert coords 2D -> 1D
  inline int idx(int x, int y) const { return y * width + x; }
  //check if coords are in the grid
  inline bool in_grid(int x, int y) const {
    return x >= 0 && x < width && y >= 0 && y < height;
  }

  //try to move sand from one cell to another
  bool try_move(int from_x, int from_y, int to_x, int to_y);
  //rules for one sand grain (down, then diagonal), same as the sequential backend
  bool apply_sand_rules(int x, int y, std::mt19937 &rng);
  // process the columns [x_begin, x_end) (owned by thread t) in place, bottom->top to get correct gravity
  void process_band(int t, int x_begin, int x_end, std::mt19937 &rng, std::atomic<bool> &moved);
};

}
