#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstdint>
#include <vector>

namespace godot {

class SandGrid : public Sprite2D {
  GDCLASS(SandGrid, Sprite2D)

public:
  enum Cell : uint8_t {
    EMPTY = 0,
    SAND = 1,
  };

  SandGrid();
  ~SandGrid() override = default;

  void _ready() override;
  void _process(double delta) override;

  void step();

  void set_grid_width(int p_w);
  int get_grid_width() const { return width; }
  void set_grid_height(int p_h);
  int get_grid_height() const { return height; }

protected:
  static void _bind_methods();

private:
  int width = 256;
  int height = 256;

  std::vector<uint8_t> cells;
  PackedByteArray pixel_buffer;
  Ref<Image> image;
  Ref<ImageTexture> texture;

  void resize_grid();
  void randomize();
  void upload_to_texture();

  inline int idx(int x, int y) const { return y * width + x; }
};

}
