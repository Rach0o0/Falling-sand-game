#pragma once

#include "sim_backend.h"

//includes for RenderingDevice
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/array.hpp>

namespace godot {


// GPU compute shader implementation (for now uses sand_compute.glsl)
// idea: double buffered textures switched each frame
// gridsize passed to shader via a push cte so any W/H works

// TODO: for now, step() returns true unconditionally, use fixed step count for benchmark
class GpuMargolusBackend : public SimBackend {
public:
  // force_local is needed to have a "private local RenderingDevice"
  // concretely, it allows to submit/sync and get accurate timing (kind of like CUDA synchronize()) 
  // for the benchmark
  // when false, uses the "global" render device to display result
  explicit GpuMargolusBackend(bool force_local = false, int wg_x = 16, int wg_y = 16)
      : force_local(force_local), wg_x(wg_x), wg_y(wg_y) {}

  bool setup(int width, int height) override;
  void load(const std::vector<uint8_t> &cells) override;
  bool step() override;
  void read_back(std::vector<uint8_t> &out) override;
  Ref<Texture2D> get_display_texture() override;
  const char *name() const override { return "gpu margolus"; }

  ~GpuMargolusBackend() override;

private:
  //RenderingDevice = Godot object that allows us to speak to GPU
  RenderingDevice *rd = nullptr;
  bool force_local = false; // request a private local device (benchmark timing)
  bool owns_rd = false;     // true when we actually created/own a local device
  RID shader;
  RID pipeline;
  RID gpu_textures[2]; //0 -> actual grid; 1 -> next grid
  RID uniform_sets[2];
  int current_texture_index = 0;
  int width = 0;
  int height = 0;

  //changed in glsl local_size_x y
  int wg_x = 16;
  int wg_y = 16;

  // Margolus parameters
  int phase = 0; //0 -> normal 2x2, 1 -> shifted 2x2
  int step_count = 0;

  //empty gpu texture creation
  RID create_gpu_texture();
  //initialize
  void initialize_gpu_texture(RID texture_rid, const std::vector<uint8_t> &cells);
  //load and compile shader glsl
  bool create_compute_pipeline();
  //create uniform set
  RID create_uniform_set(RID input_texture, RID output_texture);
};

}
