#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstdint>
#include <vector>

namespace godot {

/*
MAIN CLASS : SandGrid 
It is a sprite2D
*/
class SandGrid : public Sprite2D {
  GDCLASS(SandGrid, Sprite2D) //this class is named SandGrid, heritates from Sprite2D

public:
  //types of cells
  enum Cell : uint8_t {
    EMPTY = 0,
    SAND = 1,
    //wall, water, ...
  };

  SandGrid();
  ~SandGrid() override = default;

  //GODOT functions : 
  void _ready() override; //called when node is ready in the scene
  void _process(double delta) override; //called each frame

  void step(); //one step of the simulation

  //SETTINGS
  void set_grid_width(int p_w);
  int get_grid_width() const { return width; }
  void set_grid_height(int p_h);
  int get_grid_height() const { return height; }

protected:
  static void _bind_methods(); 

private:
  int width = 256;
  int height = 256;

  std::vector<uint8_t> cells; //1D list
  PackedByteArray pixel_buffer;
  Ref<Image> image;
  Ref<ImageTexture> texture;

  /*
  cells -> pixel_buffer -> image -> texture -> Sprite2D showd texture
  */

  void resize_grid();
  void randomize();
  void upload_to_texture();

  //convert 2D coords in 1D
  inline int idx(int x, int y) const { return y * width + x; }
};

}
