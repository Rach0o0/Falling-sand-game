#pragma once

#include "sim_backend.h"

#include <atomic>
#include <vector>

namespace godot {

// block cellular automaton with Margolus neighborhood on multi CPU thread
// the grid is cut into independent 2b2 blocks and each block is updated on its own
// the block partition is shiftedz on even/odd phases so particles can cross
// block boundaries (otherwise sand would get stuck on every even? row)
// one frame update = two phases
class CpuMargolusBackend : public SimBackend {
public:
  // num_threads <= 0 -> use std::thread::hardware_concurrency()
  explicit CpuMargolusBackend(int num_threads = 0) : requested_threads(num_threads) {}

  bool setup(int width, int height) override;
  void load(const std::vector<uint8_t> &cells) override;
  bool step() override;
  void read_back(std::vector<uint8_t> &out) override;
  const char *name() const override { return "cpu_margolus"; }

private:
  int width = 0;
  int height = 0;
  int requested_threads = 0;
  int num_threads = 1;

  // single buffer updated in place
  std::vector<uint8_t> cells;

  //convert coords 2D -> 1D
  inline int idx(int x, int y) const { return y * width + x; }
  //check if coords are in the grid
  inline bool in_grid(int x, int y) const {
    return x >= 0 && x < width && y >= 0 && y < height;
  }

  //out of bounds==> say it's a wall
  inline uint8_t get_cell(int x, int y) const {
    return in_grid(x, y) ? cells[idx(x, y)] : (uint8_t)WOOD;
  }
  inline void set_cell(int x, int y, uint8_t c) {
    if (in_grid(x, y)) {
      cells[idx(x, y)] = c;
    }
  }

  // update one 2b2 block starting top left at (ox,oy)
  bool process_block(int ox, int oy);
  // process columns of blocks [bx_begin, bx_end) owned by one thread in phase
  void process_band(int phase, int bx_begin, int bx_end, std::atomic<bool> &moved);
  // run one phase across all threads
  void run_phase(int phase, std::atomic<bool> &moved);
};

}
