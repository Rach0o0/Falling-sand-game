#include "cpu_sequential_backend.h"

namespace godot {

bool CpuSequentialBackend::setup(int p_width, int p_height) {
  width = p_width;
  height = p_height;
  rng.seed(0);
  cells.assign(static_cast<size_t>(width) * height, EMPTY);
  return true;
}

void CpuSequentialBackend::load(const std::vector<uint8_t> &p_cells) {
  cells = p_cells;
}

void CpuSequentialBackend::read_back(std::vector<uint8_t> &out) {
  out = cells;
}

/* ---------------------------------------------------------
  SIMULATION RULES
---------------------------------------------------------- */

//try to move sand
bool CpuSequentialBackend::try_move(int from_x, int from_y, int to_x, int to_y) {
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
bool CpuSequentialBackend::apply_sand_rules(int x, int y) {
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

//one step of the simulation
bool CpuSequentialBackend::step() {
  //sand has moved ?
  bool moved = false;
  for (int y = height - 2; y >= 0; --y) {
    for (int x = 0; x < width; ++x) {
      if (apply_sand_rules(x, y)) {
        moved = true;
      }
    }
  }
  return moved;
}

}
