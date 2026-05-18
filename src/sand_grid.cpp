#include "sand_grid.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/vector2.hpp>


#include <random>

using namespace godot;

SandGrid::SandGrid() {}

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
}

//each frame
void SandGrid::_process(double delta) {
  (void)delta;

  if (Engine::get_singleton()->is_editor_hint()) {
    return;
  }

  step();
  upload_to_texture();
}

// first test
void SandGrid::step() {
  for (int y = height - 2; y >= 0; --y) {
    for (int x = 0; x < width; ++x) {
      if (cells[idx(x, y)] == SAND && cells[idx(x, y + 1)] == EMPTY) {
        cells[idx(x, y)] = EMPTY;
        cells[idx(x, y + 1)] = SAND;
      }
    }
  }
}

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
