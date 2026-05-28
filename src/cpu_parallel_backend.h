#pragma once

#include "sim_backend.h"

#include <atomic>
#include <random>
#include <vector>

namespace godot {

// parallel CPU implementation
// push model + double buffering: read from cur, write into next
// each grain decides its move ONCE, so every thread can draw from its own std::mt19937
// when two grains want the same empty cell, we use an atomic CAS on that
// cell in next picks exactly one to keep => no sand lost, no duplication
// rows are split into columns, one std::thread per columns, no locks
class CpuParallelBackend : public SimBackend {
public:
  // num_threads <= 0 -> use std::thread::hardware_concurrency()
  explicit CpuParallelBackend(int num_threads = 0) : requested_threads(num_threads) {}

  bool setup(int width, int height) override;
  void load(const std::vector<uint8_t> &cells) override;
  bool step() override;
  void read_back(std::vector<uint8_t> &out) override;
  const char *name() const override { return "cpu_parallel"; }

private:
  int width = 0;
  int height = 0;
  int requested_threads = 0;
  int num_threads = 1;

  // double buffer, atomic for threads safety
  std::vector<std::atomic<uint8_t>> cur;
  std::vector<std::atomic<uint8_t>> next;

  //rng for each thread
  std::vector<std::mt19937> rngs;

  //convert coords 2D -> 1D
  inline int idx(int x, int y) const { return y * width + x; }
  //check if coords are in the grid
  inline bool in_grid(int x, int y) const {
    return x >= 0 && x < width && y >= 0 && y < height;
  }

  // process rows [y_begin, y_end) to decide each grain's move and claim it in next
  // TODO: maybe switch to columns? physics
  void process_band(int y_begin, int y_end, std::mt19937 &rng, std::atomic<bool> &moved);
};

}
