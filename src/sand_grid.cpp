#include "sand_grid.h"

#include "cpu_sequential_backend.h"
#include "gpu_backend.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <chrono>
#include <random>

using namespace godot;

SandGrid::SandGrid() {}

/* ---------------------------------------------------------
  BINDINGS / SETTINGS
---------------------------------------------------------- */

void SandGrid::_bind_methods() {
  ClassDB::bind_method(D_METHOD("set_grid_width", "w"), &SandGrid::set_grid_width);
  ClassDB::bind_method(D_METHOD("get_grid_width"), &SandGrid::get_grid_width);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "grid_width"), "set_grid_width", "get_grid_width");

  ClassDB::bind_method(D_METHOD("set_grid_height", "h"), &SandGrid::set_grid_height);
  ClassDB::bind_method(D_METHOD("get_grid_height"), &SandGrid::get_grid_height);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "grid_height"), "set_grid_height", "get_grid_height");

  //allow change from godot editor
  ClassDB::bind_method(D_METHOD("set_method", "m"), &SandGrid::set_method);
  ClassDB::bind_method(D_METHOD("get_method"), &SandGrid::get_method);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "method", PROPERTY_HINT_ENUM,
                            "CPU Sequential,CPU Parallel,GPU"),
               "set_method", "get_method");

  ClassDB::bind_method(
      D_METHOD("run_benchmark", "method", "width", "height", "fill", "seed", "steps"),
      &SandGrid::run_benchmark);

  BIND_ENUM_CONSTANT(CPU_SEQUENTIAL);
  BIND_ENUM_CONSTANT(CPU_PARALLEL);
  BIND_ENUM_CONSTANT(GPU);
}

void SandGrid::set_grid_width(int p_w) {
  width = p_w > 0 ? p_w : 1;
  resize_grid();
}

void SandGrid::set_grid_height(int p_h) {
  height = p_h > 0 ? p_h : 1;
  resize_grid();
}

void SandGrid::set_method(int p_method) {
  method = (Method)p_method;
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

void SandGrid::fill_random(std::vector<uint8_t> &out, int w, int h, double fill, int seed) {
  //fill the grid randomly with sand
  out.assign(static_cast<size_t>(w) * h, EMPTY);
  std::mt19937 rng((uint32_t)seed);
  std::bernoulli_distribution pileouface(fill);
  for (auto &c : out) {
    c = pileouface(rng) ? SAND : EMPTY;
  }
}

void SandGrid::randomize() {
  fill_random(cells, width, height, 0.10, 0);
}

std::unique_ptr<SimBackend> SandGrid::make_backend(Method m) {
  switch (m) {
    case GPU:
      return std::make_unique<GpuBackend>();
    case CPU_PARALLEL:
      UtilityFunctions::print("[SandGrid] CPU_PARALLEL not implemented yet!!!!!!!!!!!!");
      return std::make_unique<CpuSequentialBackend>();
    case CPU_SEQUENTIAL:
    default:
      return std::make_unique<CpuSequentialBackend>();
  }
}

/* ---------------------------------------------------------
  GODOT FUNCTIONS
---------------------------------------------------------- */

void SandGrid::_ready() {
  if (Engine::get_singleton()->is_editor_hint()) {
    return;
  }

  //create CPU grid
  resize_grid();

  //set up the grid
  randomize();
  set_scale(Vector2(3.0, 3.0));

  simulation_finished = false;
  simulation_steps = 0;
  simulation_start_time = std::chrono::high_resolution_clock::now();

  backend = make_backend(method);
  if (!backend->setup(width, height)) {
    UtilityFunctions::print("[SandGrid] backend ", backend->name(), " setup failed / FALLBACK to CPU sequential!!!");
    method = CPU_SEQUENTIAL;
    backend = make_backend(method);
    backend->setup(width, height);
  }
  backend->load(cells);

  UtilityFunctions::print("[SandGrid] running backend: ",backend->name()," (", width, "x", height, ")");

  Ref<Texture2D> disp = backend->get_display_texture();
  use_display_texture = disp.is_valid();
  if (use_display_texture) {
    set_texture(disp);
  } else {
    upload_to_texture();
  }
}

void SandGrid::_process(double delta) {
  //we still don't use delta
  (void)delta;

  if (Engine::get_singleton()->is_editor_hint()) {
    return;
  }
  if (simulation_finished || backend == nullptr) {
    return;
  }

  bool moved = backend->step();
  simulation_steps++;

  if (use_display_texture) {
    // GPU sends the computed texture, render it directly
    Ref<Texture2D> disp = backend->get_display_texture();
    if (disp.is_valid()) {
      set_texture(disp);
    }
  } else {
    backend->read_back(cells);
    upload_to_texture();
  }

  if (!moved) {
    auto end_time = std::chrono::high_resolution_clock::now();
    double time = std::chrono::duration<double, std::milli>(end_time - simulation_start_time).count();
    UtilityFunctions::print("Simulation ended after ", simulation_steps, " steps in ", time, " ms.");
    simulation_finished = true;
  }
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

/* ---------------------------------------------------------
  BENCHMARK HARNESS
---------------------------------------------------------- */

double SandGrid::run_benchmark(int p_method, int w, int h, double fill, int seed, int steps) {
  std::unique_ptr<SimBackend> b;
  if ((Method)p_method == GPU) {
    b = std::make_unique<GpuBackend>(true);
  } else {
    b = make_backend((Method)p_method);
  }
  if (!b->setup(w, h)) {
    UtilityFunctions::print("[benchmark] backend setup failed for method ", p_method);
    return -1.0;
  }

  std::vector<uint8_t> initial;
  fill_random(initial, w, h, fill, seed);
  b->load(initial);

  // warmup caches & compilation stuff outside the timed block
  b->step();
  b->load(initial);

  using clock = std::chrono::high_resolution_clock;
  auto start = clock::now();
  for (int s = 0; s < steps; ++s) {
    b->step();
  }
  auto end = clock::now();

  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  double cells_per_s = (steps > 0 && ms > 0.0) ? (double)w * h * steps / (ms / 1000.0) : 0.0;
  UtilityFunctions::print("[benchmark] ", b->name(), " ", w, "x", h, " fill=", fill, " steps=", steps, " -> ", ms, " ms (", ms / steps, " ms/step, ", cells_per_s / 1e6, " Mcells/s)");
  return ms;
}
