#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/array.hpp>

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

#include <cstdint>
#include <vector>
#include <chrono>

namespace godot {

class SandGrid : public Sprite2D {
  GDCLASS(SandGrid, Sprite2D) //this class is named SandGrid, heritates from Sprite2D

public:
  /* ---------------------------------------------------------
  CELL TYPES
  ---------------------------------------------------------- */
  enum Cell : uint8_t {
    EMPTY = 0,
    SAND = 1,
    //wall, water, ...
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
  SIMULATION FUNCTIONS
  ---------------------------------------------------------- */
  bool step(); //one step of the simulation, returns true if at least one sand particle moved
  void run_until_stable(); //run simulation until no particle moves + print time
  
  /* ---------------------------------------------------------
  SETTINGS
  ---------------------------------------------------------- */
  void set_grid_width(int p_w);
  int get_grid_width() const { return width; }
  void set_grid_height(int p_h);
  int get_grid_height() const { return height; }

protected:
  static void _bind_methods(); 

private:
  int width = 256;
  int height = 256;

  /* ---------------------------------------------------------
  CPU 
  ---------------------------------------------------------- */
  std::vector<uint8_t> cells; //1D list
  PackedByteArray pixel_buffer;
  Ref<Image> image;
  Ref<ImageTexture> texture;

  /*
  cells -> pixel_buffer -> image -> texture -> Sprite2D showd texture
  */

  void upload_to_texture();


  /* ---------------------------------------------------------
  GPU 
  ---------------------------------------------------------- */
  //RenderingDevice = Godot object that allows us to speak to GPU
  RenderingDevice *rd = nullptr;

  RID shader;
  RID pipeline;

  RID gpu_textures[2]; //0 -> actual grid; 1 -> next grid
  RID uniform_sets[2];
  int current_texture_index = 0;

  bool gpu_enabled = true;

  //slow down simulation
  int frame_counter = 0;
  int frames_per_update = 1;

  //empty gpu texture creation
  RID create_gpu_texture();
  //initialize
  void initialize_gpu_texture(RID texture_rid);
  //load and compile shader glsl
  bool create_compute_pipeline();
  //create uniform set
  RID create_uniform_set(RID input_texture, RID output_texture);
  //one step
  void run_gpu_step();
  //update display
  void update_display_texture();
  //setup GPU 
  bool setup_gpu();


  /* ---------------------------------------------------------
  TIMER
  ---------------------------------------------------------- */
  bool timing_enabled = false;
  bool simulation_finished = false;

  std::chrono::high_resolution_clock::time_point simulation_start_time;

  int simulation_steps = 0;

  /* ---------------------------------------------------------
  GRID SETUP
  ---------------------------------------------------------- */
  void resize_grid();
  void randomize();

  /* ---------------------------------------------------------
  SIMULATION RULES
  ---------------------------------------------------------- */
  bool apply_sand_rules(int x, int y);
  bool try_move(int from_x, int from_y, int to_x, int to_y);

  /* ---------------------------------------------------------
  UTILS
  ---------------------------------------------------------- */

  //convert coords 2D -> 1D
  //TODO: consider concurrent data structures, think of cache and array locality
  inline int idx(int x, int y) const { return y * width + x; }

  //check if coords are in the grid
  inline bool in_grid(int x, int y) const { return x >= 0 && x < width && y >= 0 && y < height;}
};

}
