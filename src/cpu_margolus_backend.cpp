#include "cpu_margolus_backend.h"

#include <algorithm>
#include <thread>

namespace godot {

bool CpuMargolusBackend::setup(int p_width, int p_height) {
  width = p_width;
  height = p_height;

  // pick the worker count
  num_threads = requested_threads;
  if (num_threads <= 0) {
    num_threads = std::max((int)std::thread::hardware_concurrency(), 1);
  }
  // a block spans 2 columns, so at most one thread per block column
  int max_threads = std::max(1, (width + 1) / 2);
  num_threads = std::min(num_threads, max_threads);

  cells.assign(static_cast<size_t>(width) * height, EMPTY);

  return true;
}

void CpuMargolusBackend::load(const std::vector<uint8_t> &p_cells) {
  cells = p_cells;
}

void CpuMargolusBackend::read_back(std::vector<uint8_t> &out) {
  out = cells;
}

/* ---------------------------------------------------------
  SIMULATION RULES
---------------------------------------------------------- */

// update one 2b2 block
// deterministic => left/right comes from phase alternation
bool CpuMargolusBackend::process_block(int ox, int oy) {
  int a = get_cell(ox, oy);
  int b = get_cell(ox + 1, oy);
  int c = get_cell(ox, oy + 1);
  int d = get_cell(ox + 1, oy + 1);

  int a0 = a, b0 = b, c0 = c, d0 = d;

  // visu:  a b
  //        c d

  // vertical fall
  if (a == SAND && c == EMPTY) { a = EMPTY; c = SAND; }
  if (b == SAND && d == EMPTY) { b = EMPTY; d = SAND; }

  // diagonal fall
  if (a == SAND && c != EMPTY && d == EMPTY) { a = EMPTY; d = SAND; }
  if (b == SAND && d != EMPTY && c == EMPTY) { b = EMPTY; c = SAND; }

  if (a == a0 && b == b0 && c == c0 && d == d0) {
    return false;
  }

  set_cell(ox, oy, (uint8_t)a);
  set_cell(ox + 1, oy, (uint8_t)b);
  set_cell(ox, oy + 1, (uint8_t)c);
  set_cell(ox + 1, oy + 1, (uint8_t)d);
  return true;
}

/* ---------------------------------------------------------
  SIMULATION FUNCTIONS
---------------------------------------------------------- */

// process the columns of blocks assigned to one thread for the given phase
// the block origin starts at -phase and steps by x++2 y++2
void CpuMargolusBackend::process_band(int phase, int bx_begin, int bx_end,
                                      std::atomic<bool> &moved) {
  bool local_moved = false;
  for (int oy = -phase; oy < height; oy += 2) {
    for (int bx = bx_begin; bx < bx_end; ++bx) {
      int ox = -phase + 2 * bx; //topleft corner of blck column
      if (process_block(ox, oy)) {
        local_moved = true;
      }
    }
  }
  if (local_moved) {
    moved.store(true, std::memory_order_relaxed);
  }
}

// blocks of the same phase are disjoint, so we can split the block columns across
// threads with no synchrnoziation
void CpuMargolusBackend::run_phase(int phase, std::atomic<bool> &moved) {
  // block columns of this phase
  // =((last valid col-1st block col)/block col step) +1;
  int n_block_cols = (width - 1 + phase) / 2 + 1;

  int threads_here = std::min(num_threads, n_block_cols);
  int cols_per_thread = (n_block_cols + threads_here - 1) / threads_here;

  std::vector<std::thread> workers;
  workers.reserve(threads_here);
  for (int t = 0; t < threads_here; ++t) {
    int bx0 = t * cols_per_thread;
    int bx1 = std::min(n_block_cols, bx0 + cols_per_thread);
    if (bx0 >= bx1) {
      break;
    }
    workers.emplace_back(
        [this, phase, bx0, bx1, &moved]() { process_band(phase, bx0, bx1, moved); });
  }
  for (std::thread &w : workers) {
    w.join();
  }
}

//one step of the simulation = two phases (0,0) et(1,1)
bool CpuMargolusBackend::step() {
  std::atomic<bool> moved{false};
  run_phase(0, moved);
  run_phase(1, moved);
  return moved.load(std::memory_order_relaxed);
}

}
