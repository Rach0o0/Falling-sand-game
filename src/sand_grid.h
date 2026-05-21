#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

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
  int height = 128;

  std::vector<uint8_t> cells; //1D list

  /* ---------------------------------------------------------
  RENDERING
  ---------------------------------------------------------- */
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
