#include "cpu_parallel_backend.h"

#include <algorithm>
#include <thread>

namespace godot {

bool CpuParallelBackend::setup(int p_width, int p_height) {
  width = p_width;
  height = p_height;

  // pick the worker count
  num_threads = requested_threads;
  if (num_threads <= 0) {
    num_threads = std::max((int)std::thread::hardware_concurrency(), 1);
  }
  num_threads = std::min(num_threads, height); // no empty bands

  size_t n = static_cast<size_t>(width) * height;
  // can't resize a vector of atomics...
  // https://stackoverflow.com/questions/24024710/resize-a-vector-of-atomic
  cur = std::vector<std::atomic<uint8_t>>(n);
  next = std::vector<std::atomic<uint8_t>>(n);

  // supposed to be faster although with less guarantees
  //  TODO: remove if problems
  //  https://en.cppreference.com/cpp/atomic/memory_order#:~:text=The%20default%20behavior,Relaxed%20ordering%20below).
  for (size_t i = 0; i < n; ++i) {
    cur[i].store(EMPTY);
    next[i].store(EMPTY);
  }

  // different seed per thread
  rngs.assign(num_threads, std::mt19937());
  for (int t = 0; t < num_threads; ++t) {
    rngs[t].seed(t + 1);
  }

  return true;
}

void CpuParallelBackend::load(const std::vector<uint8_t> &p_cells) {
  size_t n = static_cast<size_t>(width) * height;
  for (size_t i = 0; i < n; ++i) {
    cur[i].store(p_cells[i]);
  }
}

void CpuParallelBackend::read_back(std::vector<uint8_t> &out) {
  size_t n = static_cast<size_t>(width) * height;
  out.assign(n, EMPTY);
  for (size_t i = 0; i < n; ++i) {
    out[i] = cur[i].load();
  }
}

void CpuParallelBackend::process_band(int y_begin, int y_end, std::mt19937 &rng,
                                      std::atomic<bool> &moved) {
  std::bernoulli_distribution coin_flip(0.5);
  bool local_moved = false;

  for (int y = y_begin; y < y_end; ++y) {
    for (int x = 0; x < width; ++x) {
      // cur is read-only during this phase, so relaxed loads are fine
      if (cur[idx(x, y)].load(std::memory_order_relaxed) != SAND) {
        continue;
      }

      // decide the target cell (reading the OLD buffer), default = stay put
      int tx = x;
      int ty = y;

      bool can_down =
          in_grid(x, y + 1) &&
          cur[idx(x, y + 1)].load(std::memory_order_relaxed) == EMPTY;

      if (can_down) {
        tx = x;
        ty = y + 1;
      } else {
        // see where sand can fall
        bool can_left =
            in_grid(x - 1, y + 1) &&
            cur[idx(x - 1, y + 1)].load(std::memory_order_relaxed) == EMPTY;
        bool can_right =
            in_grid(x + 1, y + 1) &&
            cur[idx(x + 1, y + 1)].load(std::memory_order_relaxed) == EMPTY;

        // if both are possible -> random
        if (can_left && can_right) {
          if (coin_flip(rng)) {
            tx = x - 1;
            ty = y + 1;
          } else {
            tx = x + 1;
            ty = y + 1;
          }
        } else if (can_left) {
          tx = x - 1;
          ty = y + 1;
        } else if (can_right) {
          tx = x + 1;
          ty = y + 1;
        }
        // nowhere to go
      }

      if (tx == x && ty == y) {
        // this cell had SAND in cur so no falling grain will target it
        next[idx(x, y)].store(SAND);
      } else {
        // try to reserve the target atomically (target was EMPTY in cur)
        // several particles may race for it ==> compare_exchange lets exactly one have it
        uint8_t expected = EMPTY;
        if (next[idx(tx, ty)].compare_exchange_strong(expected, SAND)) {
          local_moved = true; // moved source stays EMPTY in next
        } else {
          next[idx(x, y)].store(SAND); // lost the race stay put where you are
        }
      }
    }
  }

  if (local_moved) {
    moved.store(true, std::memory_order_relaxed);
  }
}

bool CpuParallelBackend::step() {
  size_t n = static_cast<size_t>(width) * height;

  // 1 => clear next
  for (size_t i = 0; i < n; ++i) {
    next[i].store(EMPTY, std::memory_order_relaxed);
  }

  std::atomic<bool> moved{false};

  // 2 => each thread owns a band of rows
  int rows_per = (height + num_threads - 1) / num_threads;
  std::vector<std::thread> workers;
  workers.reserve(num_threads);

  for (int t = 0; t < num_threads; ++t) {
    int y0 = t * rows_per;
    int y1 = std::min(height, y0 + rows_per);
    if (y0 >= y1) {
      break;
    }
    workers.emplace_back(
        [this, y0, y1, t, &moved]() { process_band(y0, y1, rngs[t], moved); });
  }
  for (std::thread &w : workers) {
    w.join();
  }

  // next is now the new state
  std::swap(cur, next);
  return moved.load(std::memory_order_relaxed);
}

}