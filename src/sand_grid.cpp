#include "sand_grid.h"

#include "cpu_margolus_backend.h"
#include "cpu_parallel_backend.h"
#include "cpu_parallel_columnband_backend.h"
#include "cpu_sequential_backend.h"
#include "gpu_backend.h"
#include "gpu_margolus_backend.h"
#include "sim_backend.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <algorithm>
#include <chrono>
#include <random>

#include <thread>
using namespace godot;

SandGrid::SandGrid() {}

/* ---------------------------------------------------------
  BINDINGS / SETTINGS
---------------------------------------------------------- */

void SandGrid::_bind_methods() {
  ClassDB::bind_method(D_METHOD("set_grid_width", "w"),
                       &SandGrid::set_grid_width);
  ClassDB::bind_method(D_METHOD("get_grid_width"), &SandGrid::get_grid_width);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "grid_width"), "set_grid_width",
               "get_grid_width");

  ClassDB::bind_method(D_METHOD("set_grid_height", "h"),
                       &SandGrid::set_grid_height);
  ClassDB::bind_method(D_METHOD("get_grid_height"), &SandGrid::get_grid_height);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "grid_height"), "set_grid_height",
               "get_grid_height");

  // allow change from godot editor
  ClassDB::bind_method(D_METHOD("set_method", "m"), &SandGrid::set_method);
  ClassDB::bind_method(D_METHOD("get_method"), &SandGrid::get_method);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "method", PROPERTY_HINT_ENUM,
                            "CPU Sequential,CPU Parallel,GPU,CPU Parallel "
                            "ColumnBand, GPU Margolus,CPU Margolus"),
               "set_method", "get_method");

  ClassDB::bind_method(D_METHOD("run_benchmark", "method", "width", "height",
                                "fill", "seed", "steps"),
                       &SandGrid::run_benchmark);

  ClassDB::bind_method(D_METHOD("run_benchmark_ex", "method", "width", "height",
                                "scenario", "seed", "steps", "threads", "wg_x",
                                "wg_y", "repeats"),
                       &SandGrid::run_benchmark_ex);

  BIND_ENUM_CONSTANT(CPU_SEQUENTIAL);
  BIND_ENUM_CONSTANT(CPU_PARALLEL);
  BIND_ENUM_CONSTANT(GPU);
  BIND_ENUM_CONSTANT(CPU_PARALLEL_COLUMN_BAND);
  BIND_ENUM_CONSTANT(GPU_MARGOLUS);
  BIND_ENUM_CONSTANT(CPU_MARGOLUS);
}

void SandGrid::set_grid_width(int p_w) {
  width = p_w > 0 ? p_w : 1;
  resize_grid();
}

void SandGrid::set_grid_height(int p_h) {
  height = p_h > 0 ? p_h : 1;
  resize_grid();
}

void SandGrid::set_method(int p_method) { method = (Method)p_method; }

/* ---------------------------------------------------------
  GRID SETUP
---------------------------------------------------------- */

void SandGrid::resize_grid() {
  // creates a grid of size width*height with empty cells
  cells.assign(static_cast<size_t>(width) * height, EMPTY);
  // RGBA buffer
  pixel_buffer.resize(static_cast<int64_t>(width) * height * 4);
  // creates empty image and texture
  image = Image::create_empty(width, height, false, Image::FORMAT_RGBA8);
  texture = ImageTexture::create_from_image(image);
  set_texture(texture);
}

void SandGrid::fill_random(std::vector<uint8_t> &out, int w, int h, double fill,
                           int seed) {
  // fill the grid randomly with sand
  out.assign(static_cast<size_t>(w) * h, EMPTY);
  std::mt19937 rng((uint32_t)seed);
  std::bernoulli_distribution pileouface(fill);
  for (auto &c : out) {
    c = pileouface(rng) ? SAND : EMPTY;
  }
}

void SandGrid::fill_test(std::vector<uint8_t> &out, int w, int h, double fill,
                         int seed) {
  out.assign(static_cast<size_t>(w) * h, EMPTY);

  int xmid = w / 2;
  for (int y = 0; y < h; y++) {
    out[y * w + xmid] = SAND;
    out[y * w + xmid + 1] = SAND;
    out[y * w + xmid - 1] = SAND;
    // out[y * w + xmid-3] = SAND;
    // out[y * w + xmid+3] = SAND;
  }
}

void SandGrid::fill_wood_hole_test(std::vector<uint8_t> &out, int w, int h) {
  out.assign(static_cast<size_t>(w) * h, EMPTY);

  int wall_y = h / 2;
  int hole_x = w / 2;
  int hole_radius = 2;

  for (int y = 5; y < wall_y - 5; y++) {
    for (int x = w / 2 - 20; x <= w / 2 + 20; x++) {
      if (x >= 0 && x < w) {
        out[y * w + x] = SAND;
      }
    }
  }

  for (int x = 0; x < w; x++) {
    if (x < hole_x - hole_radius || x > hole_x + hole_radius) {
      out[wall_y * w + x] = WOOD;
      out[(wall_y + 1) * w + x] = WOOD;
    }
  }
}

/* ---------------------------------------------------------
  BENCHMARK SCENARIOS
---------------------------------------------------------- */

void SandGrid::fill_checkerboard(std::vector<uint8_t> &out, int w, int h) {
  out.assign(static_cast<size_t>(w) * h, EMPTY);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (((x + y) & 1) == 0) {
        out[y * w + x] = SAND;
      }
    }
  }
}
void SandGrid::fill_full_upper_half(std::vector<uint8_t> &out, int w, int h) {
  out.assign(static_cast<size_t>(w) * h, EMPTY);
  for (int y = 0; y < h / 2; ++y) {
    for (int x = 0; x < w; ++x) {
      out[y * w + x] = SAND;
    }
  }
}
void SandGrid::fill_hourglass(std::vector<uint8_t> &out, int w, int h,
                              int seed) {
  out.assign(static_cast<size_t>(w) * h, EMPTY);

  int cx = w / 2;
  int neck_y = h / 2;
  int neck_width = 4;
  std::mt19937 rng((uint32_t)seed);
  std::bernoulli_distribution keep(0.9);

  for (int y = 0; y < neck_y; ++y) {
    // linear interpolatopn of way down the top chamber (0 at top, 1 at the neck)
    double t = (neck_y > 1) ? (double)y / (double)(neck_y - 1) : 1.0;
    int gap_half = (int)((1.0-t) *(w/2-neck_width)+neck_width);
    int left = cx - gap_half;
    int right = cx + gap_half;
    for (int x = 0; x < w; ++x) {
      if (x < left || x > right) {//WALL
        out[y * w + x] = WOOD;
      } else if (y > 2) {
        // leave the top few rows open, fill the rest of the funnel with sand
        out[y * w + x] = keep(rng) ? SAND : EMPTY;
      }
    }
  }

  for (int y = neck_y; y < h; ++y) {
    double t = (neck_y > 1) ? (double)y / (double)(neck_y - 1) : 1.0;
    int gap_half = (int)((t-1.0)*(w/2-neck_width)+neck_width);
    int left = cx - gap_half;
    int right = cx + gap_half;
    for (int x = 0; x < w; ++x) {
      if (x < left || x > right) {//WALL
        out[y * w + x] = WOOD;
      }
    }
  }

  // solid floor of the top chamber with just the neck open
  for (int x = 0; x < w; ++x) {
    if (x < cx - neck_width || x > cx + neck_width) {
      out[neck_y * w + x] = WOOD;
    }
  }
}

void SandGrid::fill_single_source(std::vector<uint8_t> &out, int w, int h) {
  out.assign(static_cast<size_t>(w) * h, EMPTY);
  int cx = w / 2;
  int r = std::max(1, w / 32);
  int block_h = std::max(4, h / 4);
  for (int y = 0; y < block_h && y < h; ++y) {
    for (int dx = -r; dx <= r; ++dx) {
      int x = cx + dx;
      if (x >= 0 && x < w) {
        out[y * w + x] = SAND;
      }
    }
  }
}
void SandGrid::fill_scenario(int scenario, std::vector<uint8_t> &out, int w,
                             int h, double fill, int seed) {
  switch (scenario) {
  case SCN_CHECKERBOARD:
    fill_checkerboard(out, w, h);
    break;
  case SCN_FULL_UPPER_HALF:
    fill_full_upper_half(out, w, h);
    break;
  case SCN_HOURGLASS:
    fill_hourglass(out, w, h, seed);
    break;
  case SCN_SINGLE_SOURCE:
    fill_single_source(out, w, h);
    break;
  case SCN_RANDOM_DENSE:
  default:
    fill_random(out, w, h, fill, seed);
    break;
  }
}
const char *SandGrid::scenario_name(int scenario) {
  switch (scenario) {
  case SCN_CHECKERBOARD:
    return "checkerboard";
  case SCN_FULL_UPPER_HALF:
    return "full_upper_half";
  case SCN_HOURGLASS:
    return "hourglass";
  case SCN_SINGLE_SOURCE:
    return "single_source";
  case SCN_RANDOM_DENSE:
    return "random_dense";
  default:
    return "unknown";
  }
}
void SandGrid::randomize() {
  // fill_random(cells, width, height, 0.10, 0);
  // fill_random(cells, width, height, 0.50, 0);
  // fill_random(cells, width, height, 0.90, 0);
  // fill_test(cells,width,height,0,0);
  // fill_wood_hole_test(cells, width, height);
  fill_scenario(4, cells, width, height, 0.5, 0);
}

std::unique_ptr<SimBackend> SandGrid::make_backend(Method m) {
  switch (m) {
  case GPU:
    return std::make_unique<GpuBackend>();
  case CPU_PARALLEL:
    // 0 -> use all cores
    return std::make_unique<CpuParallelBackend>(0);
  case CPU_PARALLEL_COLUMN_BAND:
    // 0 -> use all cores
    return std::make_unique<CpuParallelBackendColumnBand>(0);
  case GPU_MARGOLUS:
    return std::make_unique<GpuMargolusBackend>();
  case CPU_MARGOLUS:
    // 0 -> use all cores
    return std::make_unique<CpuMargolusBackend>(0);
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

  // create CPU grid
  resize_grid();

  set_scale(Vector2(scale, scale));
  set_centered(false);
  fit_window_to_grid(); // make the whole scaled grid visible

  // fill the grid and (re)build the backend for the current method
  start_simulation();

  UtilityFunctions::print(
      "[SandGrid] controls: p = pause, m = switch method, s = slow mode");
}

// build the backend for the current method and reload a fresh grid
// also used when switching method at runtime "m"
void SandGrid::start_simulation() {
  // set up the grid (deterministic, so every method starts from the same state)
  randomize();

  simulation_finished = false;
  simulation_steps = 0;
  simulation_start_time = std::chrono::high_resolution_clock::now();

  backend = make_backend(method);
  if (!backend->setup(width, height)) {
    UtilityFunctions::print("[SandGrid] backend ", backend->name(),
                            " setup failed / FALLBACK to CPU sequential!!!");
    method = CPU_SEQUENTIAL;
    backend = make_backend(method);
    backend->setup(width, height);
  }
  backend->load(cells);

  UtilityFunctions::print("[SandGrid] running backend: ", backend->name(), " (",
                          width, "x", height, ")");

  Ref<Texture2D> disp = backend->get_display_texture();
  use_display_texture = disp.is_valid();
  if (use_display_texture) {
    set_texture(disp);
  } else {
    // switching back from GPU left the sprite pointing at the GPU texture,
    // so we needed to rebind our CPU ImageTexture before refreshing it
    set_texture(texture);
    upload_to_texture();
  }
}

// resize the OS window so the whole grid fits
void SandGrid::fit_window_to_grid() {
  Vector2 s = get_scale(); // need this cuz we used set_scale
  Vector2i win_size((int)(width * s.x), (int)(height * s.y));
  if (Window *w = get_window()) {
    w->set_size(win_size);
  }
}

void SandGrid::_process(double delta) {
  // we still don't use delta
  (void)delta;

  if (Engine::get_singleton()->is_editor_hint()) {
    return;
  }
  if (simulation_finished || backend == nullptr) {
    return;
  }
  if (paused) {
    return; //"p": stay on the current frame
  }

  bool moved = backend->step();
  simulation_steps++;
  //"s": artificially slow down so we can watch the simulation evolve
  if (slow_mode) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

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
    double time = std::chrono::duration<double, std::milli>(
                      end_time - simulation_start_time)
                      .count();
    UtilityFunctions::print("Simulation ended after ", simulation_steps,
                            " steps in ", time, " ms.");
    simulation_finished = true;
  }
}

// keyboard controls for the main game
void SandGrid::_input(const Ref<InputEvent> &event) {
  if (Engine::get_singleton()->is_editor_hint()) {
    return; // ignore in editor
  }

  Ref<InputEventKey> key = event;
  // only react on the initial key press
  if (key.is_null() || !key->is_pressed() || key->is_echo()) {
    return;
  }

  switch (key->get_keycode()) {
  case KEY_P:
    paused = !paused;
    UtilityFunctions::print("[SandGrid] ", paused ? "paused" : "resumed");
    break;
  case KEY_S:
    slow_mode = !slow_mode;
    UtilityFunctions::print("[SandGrid] slow mode ", slow_mode ? "ON" : "OFF");
    break;
  case KEY_M:
    // cycle to the next method and restart from the same initial grid
    method = (Method)(((int)method + 1) % 6);
    start_simulation();
    break;
  default:
    break;
  }
}

/* ---------------------------------------------------------
  RENDERING
---------------------------------------------------------- */

void SandGrid::upload_to_texture() {
  uint8_t *dst = pixel_buffer.ptrw();
  for (int i = 0; i < width * height; ++i) {
    if (cells[i] == SAND) {
      dst[i * 4 + 0] = 220;
      dst[i * 4 + 1] = 220;
      dst[i * 4 + 2] = 160;
    } else if (cells[i] == WOOD) {
      dst[i * 4 + 0] = 120;
      dst[i * 4 + 1] = 70;
      dst[i * 4 + 2] = 30;
    } else {
      dst[i * 4 + 0] = 20;
      dst[i * 4 + 1] = 20;
      dst[i * 4 + 2] = 20;
    }

    dst[i * 4 + 3] = 255;
  }

  image->set_data(width, height, false, Image::FORMAT_RGBA8, pixel_buffer);
  texture->update(image);
}

/* ---------------------------------------------------------
  BENCHMARK HARNESS
---------------------------------------------------------- */

double SandGrid::run_benchmark(int p_method, int w, int h, double fill,
                               int seed, int steps) {
  std::unique_ptr<SimBackend> b;
  if ((Method)p_method == GPU) {
    b = std::make_unique<GpuBackend>(true);
  } else if ((Method)p_method == GPU_MARGOLUS) {
    b = std::make_unique<GpuMargolusBackend>(true);
  } else {
    b = make_backend((Method)p_method);
  }
  if (!b->setup(w, h)) {
    UtilityFunctions::print("[benchmark] backend setup failed for method ",
                            p_method);
    return -1.0;
  }

  std::vector<uint8_t> initial;
  // fill_random(initial, w, h, fill, seed);
  fill_test(initial, w, h, fill, seed);
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
  double cells_per_s =
      (steps > 0 && ms > 0.0) ? (double)w * h * steps / (ms / 1000.0) : 0.0;
  UtilityFunctions::printraw("[benchmark] ", b->name(), " ", w, "x", h,
                             " fill=", fill, " steps=", steps, " -> ", ms,
                             " ms (", ms / steps, " ms/step, ",
                             cells_per_s / 1e6, " Mcells/s)");

  // correctness verification based on number of cells
  std::vector<uint8_t> final_cells;
  b->read_back(final_cells);
  long n_particles_start = 0;
  long n_particles_end = 0;
  // TODO: change this if we add other particles that can disappear
  for (uint8_t c : initial)
    n_particles_start += (c != EMPTY);
  for (uint8_t c : final_cells)
    n_particles_end += (c != EMPTY);

  if (n_particles_start != n_particles_end) {
    UtilityFunctions::print(
        " || INCORRECT loss of particles: ", n_particles_start, " -> ",
        n_particles_end, " grains");
  } else {
    UtilityFunctions::print(
        " || CORRECT conservation of particles: ", n_particles_start, " -> ",
        n_particles_end, " grains");
  }

  return ms;
}

Dictionary SandGrid::run_benchmark_ex(int p_method, int w, int h, int scenario,
                                      int seed, int steps, int threads,
                                      int wg_x, int wg_y, int repeats) {
  Dictionary result;
  if (repeats < 1) {
    repeats = 1;
  }

  std::unique_ptr<SimBackend> b;
  switch ((Method)p_method) {
  case GPU:
    b = std::make_unique<GpuBackend>(true, wg_x, wg_y);
    break;
  case GPU_MARGOLUS:
    b = std::make_unique<GpuMargolusBackend>(true, wg_x, wg_y);
    break;
  case CPU_PARALLEL:
    b = std::make_unique<CpuParallelBackend>(threads);
    break;
  case CPU_PARALLEL_COLUMN_BAND:
    b = std::make_unique<CpuParallelBackendColumnBand>(threads);
    break;
  case CPU_MARGOLUS:
    b = std::make_unique<CpuMargolusBackend>(threads);
    break;
  case CPU_SEQUENTIAL:
  default:
    b = std::make_unique<CpuSequentialBackend>();
    break;
  }

  if (!b->setup(w, h)) {
    UtilityFunctions::print("[bench] backend setup failed for method ",
                            p_method);
    result["ok"] = false;
    return result;
  }

  std::vector<uint8_t> initial;
  fill_scenario(scenario, initial, w, h, 0.5, seed);

  b->load(initial);
  for (int s = 0; s < steps; ++s) {
    b->step();
  }

  using clock = std::chrono::high_resolution_clock;
  Array run_ms;
  std::vector<double> times;
  times.reserve(repeats);
  std::vector<uint8_t> final_cells;
  for (int r = 0; r < repeats; ++r) {
    b->load(initial);
    auto start = clock::now();
    for (int s = 0; s < steps; ++s) {
      b->step();
    }
    auto end = clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    times.push_back(ms);
    run_ms.append(ms);
    if (r == repeats - 1) {
      b->read_back(final_cells); // keep the last run's final grid
    }
  }

  // median, robust to slowdowns on pc
  std::vector<double> sorted = times;
  std::sort(sorted.begin(), sorted.end());
  double median_ms = sorted[sorted.size() / 2];
  if (sorted.size() % 2 == 0) {
    median_ms =
        0.5 * (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]);
  }
  double min_ms = sorted.front();
  double sum = 0.0;
  for (double t : times) {
    sum += t;
  }
  double mean_ms = sum / (double)times.size();

  double ms_per_frame = (steps > 0) ? median_ms / steps : 0.0;
  double mcells_per_s = (median_ms > 0.0)
                            ? (double)w * h * steps / (median_ms / 1000.0) / 1e6
                            : 0.0;

  // conservation
  long n_start = 0;
  long n_end = 0;
  for (uint8_t c : initial) {
    n_start += (c == SAND);
  }
  for (uint8_t c : final_cells) {
    n_end += (c == SAND);
  }
  bool conserved = (n_start == n_end);

  // lr bias
  long fair_L = -1;
  long fair_R = -1;
  double fair_bias = -1.0;
  if (scenario == SCN_SINGLE_SOURCE && !final_cells.empty()) {
    int xsrc = w / 2;
    long L = 0;
    long R = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        if (final_cells[y * w + x] == SAND) {
          if (x < xsrc) {
            L++;
          } else if (x > xsrc) {
            R++;
          }
        }
      }
    }
    fair_L = L;
    fair_R = R;
    long diff = (L > R) ? (L - R) : (R - L);
    fair_bias = (L + R > 0) ? (double)diff / (double)(L + R) : 0.0;
  }

  //build dict
  result["ok"] = true;
  result["method_id"] = p_method;
  result["method_name"] = String(b->name());
  result["scenario_id"] = scenario;
  result["scenario_name"] = String(scenario_name(scenario));
  result["width"] = w;
  result["height"] = h;
  result["threads"] = threads;
  result["wg_x"] = wg_x;
  result["wg_y"] = wg_y;
  result["steps"] = steps;
  result["repeats"] = repeats;
  result["run_ms"] = run_ms;
  result["median_ms"] = median_ms;
  result["min_ms"] = min_ms;
  result["mean_ms"] = mean_ms;
  result["ms_per_frame"] = ms_per_frame;
  result["mcells_per_s"] = mcells_per_s;
  result["particles_init"] = (int64_t)n_start;
  result["particles_final"] = (int64_t)n_end;
  result["conserved"] = conserved;
  result["fair_L"] = (int64_t)fair_L;
  result["fair_R"] = (int64_t)fair_R;
  result["fair_bias"] = fair_bias;

  UtilityFunctions::print(
      "[bench] ", b->name(), " scn=", scenario_name(scenario), " ", w, "x", h,
      " thr=", threads, " wg=", wg_x, "x", wg_y, " -> ", ms_per_frame,
      " ms/frame (", mcells_per_s, " Mcells/s) ", conserved ? "OK" : "LOSS");

  return result;
}
