#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/core/binder_common.hpp>

#include <cstdint>
#include <memory>
#include <vector>
#include <chrono>

#include "sim_backend.h"

namespace godot {

// step logic is now handled by a SimBackend (abstraction for the methods, to make benchmarking easy )
class SandGrid : public Sprite2D {
  GDCLASS(SandGrid, Sprite2D) //this class is named SandGrid, heritates from Sprite2D

public:
  // concurrency implementation/algorithm enum to switch without recompiling
  enum Method {
    CPU_SEQUENTIAL = 0,
    CPU_PARALLEL = 1, // push w/atomic CAS
    GPU = 2,
    CPU_PARALLEL_COLUMN_BAND = 3, // in-place sweep, columns split across threads
  };

  /* ---------------------------------------------------------
  CONSTRUCTOR / DESTRUCTOR
  ---------------------------------------------------------- */
  SandGrid();
  ~SandGrid() override = default;

  /* ---------------------------------------------------------
  GODOT FUNCTIONS
  ---------------------------------------------------------- */
  void _ready() override; //called when node is ready in the scene
  void _process(double delta) override; //called each frame

  /* ---------------------------------------------------------
  SETTINGS
  ---------------------------------------------------------- */
  void set_grid_width(int p_w);
  int get_grid_width() const { return width; }
  void set_grid_height(int p_h);
  int get_grid_height() const { return height; }
  void set_method(int p_method);
  int get_method() const { return (int)method; }

  // headless benchmark for proper times, runs fixed steps of "method"
  // with gridsize w*h, fill ratio, fixed seed
  // print timing and returns ms spent in the main loop
  double run_benchmark(int p_method, int w, int h, double fill, int seed, int steps);

protected:
  static void _bind_methods();

private:
  int width = 128;
  int height = 64;
  Method method = CPU_PARALLEL_COLUMN_BAND;

  /* ---------------------------------------------------------
  CPU
  ---------------------------------------------------------- */
  std::vector<uint8_t> cells; //1D list for now, baseline for cpu
  std::unique_ptr<SimBackend> backend;
  bool use_display_texture = false; // true when GPU

  // rendering
  PackedByteArray pixel_buffer;
  Ref<Image> image;
  Ref<ImageTexture> texture;

  /*
  cells -> pixel_buffer -> image -> texture -> Sprite2D showd texture
  */

  void upload_to_texture();

  /* ---------------------------------------------------------
  TIMER
  ---------------------------------------------------------- */
  bool simulation_finished = false;
  std::chrono::high_resolution_clock::time_point simulation_start_time;
  int simulation_steps = 0;

  /* ---------------------------------------------------------
  GRID SETUP
  ---------------------------------------------------------- */
  void resize_grid();
  void randomize();
  static std::unique_ptr<SimBackend> make_backend(Method m);
  static void fill_random(std::vector<uint8_t> &out, int w, int h, double fill, int seed);
  void fill_test(std::vector<uint8_t> &out, int w, int h, double fill, int seed);

  /* ---------------------------------------------------------
  UTILS
  ---------------------------------------------------------- */

  //convert coords 2D -> 1D
  //TODO: consider concurrent data structures, think of cache and array locality
  inline int idx(int x, int y) const { return y * width + x; }
};

} // namespace godot

VARIANT_ENUM_CAST(godot::SandGrid::Method);
