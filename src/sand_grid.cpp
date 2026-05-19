#include "sand_grid.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <chrono>
#include <iostream>
#include <random>


using namespace godot;

SandGrid::SandGrid() {}

/* ---------------------------------------------------------
  SETTINGS
---------------------------------------------------------- */

void SandGrid::_bind_methods() {
  ClassDB::bind_method(D_METHOD("step"), &SandGrid::step);

  ClassDB::bind_method(D_METHOD("set_grid_width", "w"), &SandGrid::set_grid_width);
  ClassDB::bind_method(D_METHOD("get_grid_width"), &SandGrid::get_grid_width);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "grid_width"), "set_grid_width", "get_grid_width");

  ClassDB::bind_method(D_METHOD("set_grid_height", "h"), &SandGrid::set_grid_height);
  ClassDB::bind_method(D_METHOD("get_grid_height"), &SandGrid::get_grid_height);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "grid_height"), "set_grid_height", "get_grid_height");
}

void SandGrid::set_grid_width(int p_w) {
  width = p_w > 0 ? p_w : 1;
  resize_grid();
}

void SandGrid::set_grid_height(int p_h) {
  height = p_h > 0 ? p_h : 1;
  resize_grid();
}

/* ---------------------------------------------------------
  GRID SETUP
---------------------------------------------------------- */

void SandGrid::resize_grid() {
  //creates a grid of size width*height with empty cells
  cells.assign(static_cast<size_t>(width) * height, EMPTY);
  //RGBA buffer
  pixel_buffer.resize(static_cast<int64_t>(width) * height * 4);
  //creates empty image and texture
  image = Image::create_empty(width, height, false, Image::FORMAT_RGBA8);
  texture = ImageTexture::create_from_image(image);
  set_texture(texture);
}

void SandGrid::randomize() {
  //fill the grid randomly with sand
  std::mt19937 rng(0);
  std::bernoulli_distribution pileouface(0.5);
  for (auto &c : cells)
    c = pileouface(rng) ? SAND : EMPTY;
}

/* ---------------------------------------------------------
  GODOT FUNCTIONS
---------------------------------------------------------- */

void SandGrid::_ready() {
  if (Engine::get_singleton()->is_editor_hint()) {
    return;
  }

  resize_grid();

  set_centered(false);
  set_position(Vector2(0, 0));
  set_scale(Vector2(3, 3));

  randomize();
  upload_to_texture();

  timing_enabled = true;
  simulation_finished = false;
  simulation_steps = 0;
  simulation_start_time = std::chrono::high_resolution_clock::now();
}

//each frame
void SandGrid::_process(double delta) {
  //we still don't use delta
  (void)delta;

  if (Engine::get_singleton()->is_editor_hint()) {
    return;
  }

  if (simulation_finished) return;

  bool moved = step();
  simulation_steps++;

  upload_to_texture();

  if (!moved){
    auto end_time = std::chrono::high_resolution_clock::now();

    double time = std::chrono::duration<double, std::milli>(end_time - simulation_start_time).count();
    std::cout << "Simulation ended after " << simulation_steps << " steps in " << time << " ms." << std::endl;

    simulation_finished = true;
  }
}

/* ---------------------------------------------------------
  SIMULATION RULES
---------------------------------------------------------- */

//rules for sand
bool SandGrid::apply_sand_rules(int x, int y){
  if (cells[idx(x,y)] != SAND){
    return false;
  }

  //try move down
  if (try_move(x,y,x,y+1)){
    return true;
  }

  //see where sand can fall
  bool can_fall_left = in_grid(x-1, y+1) && cells[idx(x-1, y+1)] == EMPTY;
  bool can_fall_right = in_grid(x+1, y+1) && cells[idx(x+1, y+1)] == EMPTY;

  //if both are possible -> random
  if (can_fall_left && can_fall_right) {
    std::mt19937 rng(0);
    std::bernoulli_distribution coin_flip(0.5);
    
    if (coin_flip(rng)){
      return try_move(x, y,x-1, y+1);
    } else {
      return try_move(x, y, x+1 , y+1);
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

//try to move sand
bool SandGrid::try_move(int from_x, int from_y, int to_x, int to_y){
  if (!in_grid(to_x, to_y)){
    return false;
  }

  int from = idx(from_x, from_y);
  int to = idx(to_x, to_y);

  if (cells[to] != EMPTY){
    return false;
  }

  cells[from] = EMPTY;
  cells[to] = SAND;

  return true;
}

/* ---------------------------------------------------------
  SIMULATION FUNCTIONS
---------------------------------------------------------- */

//one step of the simulation
bool SandGrid::step() {
  //sand has moved ?
  bool moved = false;

  for (int y = height - 2; y >= 0; --y) {
    for (int x = 0; x < width; ++x) {
      if (apply_sand_rules(x,y)) {
        moved = true;
      }
    }
  }

  return moved;
}

//benchmark
void SandGrid::run_until_stable(){
  using clock = std::chrono::high_resolution_clock;
  auto start = clock::now();

  int steps = 0;

  while (true) {
    bool moved = step();
    steps++;

    if(!moved){
      break;
    }
  }
  auto end = clock::now();

  double time = std::chrono::duration<double, std::milli>(end - start).count();

  std::cout << "Simulation ended after " << steps << " steps in " << time << " ms." << std::endl;
  
  upload_to_texture();
}

/* ---------------------------------------------------------
  RENDERING
---------------------------------------------------------- */

void SandGrid::upload_to_texture() {
  uint8_t *dst = pixel_buffer.ptrw();
  for (int i = 0; i < width * height; ++i) {
    uint8_t v = (cells[i] == SAND) ? 220 : 20;
    dst[i * 4 + 0] = v;
    dst[i * 4 + 1] = v;
    dst[i * 4 + 2] = (cells[i] == SAND) ? 160 : 20;
    dst[i * 4 + 3] = 255;
  }
  image->set_data(width, height, false, Image::FORMAT_RGBA8, pixel_buffer);
  texture->update(image);
}
