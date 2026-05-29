#include "cpu_parallel_columnband_backend.h"

#include <algorithm>
#include <thread>

namespace godot {

bool CpuParallelBackendColumnBand::setup(int p_width, int p_height) {
  width = p_width;
  height = p_height;

  // pick the worker count
  num_threads = requested_threads;
  if (num_threads <= 0) {
    num_threads = std::max((int)std::thread::hardware_concurrency(), 1);
  }
  num_threads = std::min(num_threads, width); // no empty band

  cells.assign(static_cast<size_t>(width) * height, EMPTY);

  // different seed per thread
  rngs.assign(num_threads, std::mt19937());
  for (int t = 0; t < num_threads; ++t) {
    rngs[t].seed(t + 1);
  }

  // one mutex per boundary between adjacent bands (none if a single thread)
  // std::mutex isn't movable, but move-assigning the whole vector is fine
  // https://stackoverflow.com/questions/24024710/resize-a-vector-of-atomic
  boundaries = std::vector<std::mutex>(num_threads > 1 ? num_threads - 1 : 0);

  return true;
}

void CpuParallelBackendColumnBand::load(const std::vector<uint8_t> &p_cells) {
  cells = p_cells;
}

void CpuParallelBackendColumnBand::read_back(std::vector<uint8_t> &out) {
  out = cells;
}

/* ---------------------------------------------------------
  SIMULATION RULES
---------------------------------------------------------- */

//try to move sand from one cell to another
bool CpuParallelBackendColumnBand::try_move(int from_x, int from_y, int to_x, int to_y) {
  if (!in_grid(to_x, to_y)) {
    return false;
  }

  int from = idx(from_x, from_y);
  int to = idx(to_x, to_y);

  if (cells[to] != EMPTY) {
    return false;
  }

  cells[from] = EMPTY;
  cells[to] = SAND;
  return true;
}

//rules for sand
//safe by the boundary lock taken around edge grains in process_band
bool CpuParallelBackendColumnBand::apply_sand_rules(int x, int y, std::mt19937 &rng) {
  if (cells[idx(x, y)] != SAND) {
    return false;
  }

  //try move down
  if (try_move(x, y, x, y + 1)) {
    return true;
  }

  //see where sand can fall
  bool can_fall_left = in_grid(x - 1, y + 1) && cells[idx(x - 1, y + 1)] == EMPTY;
  bool can_fall_right = in_grid(x + 1, y + 1) && cells[idx(x + 1, y + 1)] == EMPTY;

  //if both are possible -> random
  if (can_fall_left && can_fall_right) {
    std::bernoulli_distribution coin_flip(0.5);

    if (coin_flip(rng)) {
      return try_move(x, y, x - 1, y + 1);
    } else {
      return try_move(x, y, x + 1, y + 1);
    }
  }

  //left
  if (can_fall_left) {
    return try_move(x, y, x - 1, y + 1);
  }
  //right
  if (can_fall_right) {
    return try_move(x, y, x + 1, y + 1);
  }

  return false;
}

/* ---------------------------------------------------------
  SIMULATION FUNCTIONS
---------------------------------------------------------- */

//in place bottom->top loop over the columns assigned to this thread
// grains wanting to cross boundary take the neighbouring boundary lock
// (like at the airport lol) so the two columns at boundary are never R/W by both threads at once
void CpuParallelBackendColumnBand::process_band(int t, int x_begin, int x_end,
                                                std::mt19937 &rng,
                                                std::atomic<bool> &moved) {
  bool has_left = (t > 0);
  bool has_right = (t < num_threads - 1);

  bool local_moved = false;
  for (int y = height - 2; y >= 0; --y) {
    for (int x = x_begin; x < x_end; ++x) {
      bool needs_left = has_left && (x <= x_begin + 1);
      bool needs_right = has_right && (x >= x_end - 2);

      bool moved_here;
      if (needs_left && needs_right) {
        // https://www.reddit.com/r/cpp_questions/comments/1sjoobv/quick_question_stdscoped_lock_vs_stdlock_guard/
        // https://stackoverflow.com/questions/67356331/scoped-lock-with-repeating-arguments
        // std::scoped_lock to avoid dedlock
        std::scoped_lock lk(boundaries[t - 1], boundaries[t]);
        moved_here = apply_sand_rules(x, y, rng);
      } else if (needs_left) {
        std::scoped_lock lk(boundaries[t - 1]);
        moved_here = apply_sand_rules(x, y, rng);
      } else if (needs_right) {
        std::scoped_lock lk(boundaries[t]);
        moved_here = apply_sand_rules(x, y, rng);
      } else {
        // interior sand owns everything it touches -> lock-free!
        moved_here = apply_sand_rules(x, y, rng);
      }

      if (moved_here) {
        local_moved = true;
      }
    }
  }

  if (local_moved) {
    moved.store(true, std::memory_order_relaxed);
  }
}

//one step of the simulation
bool CpuParallelBackendColumnBand::step() {
  std::atomic<bool> moved{false};

  int cols_per_thread = (width + num_threads - 1) / num_threads;
  std::vector<std::thread> workers;
  workers.reserve(num_threads);

  for (int t = 0; t < num_threads; ++t) {
    int x0 = t * cols_per_thread;
    int x1 = std::min(width, x0 + cols_per_thread);
    if (x0 >= x1) {
      break;
    }
    //processband() contains mutexes inside
    workers.emplace_back(
        [this, x0, x1, t, &moved]() { process_band(t, x0, x1, rngs[t], moved); });
  }
  for (std::thread &w : workers) {
    w.join();
  }

  return moved.load(std::memory_order_relaxed);
}

}
