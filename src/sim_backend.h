#pragma once

#include <cstdint>
#include <vector>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/texture2d.hpp>

namespace godot {

/* ---------------------------------------------------------
CELL TYPES
---------------------------------------------------------- */

enum Cell : uint8_t {
  EMPTY = 0,
  SAND = 1,
  //wall, water, ...
};


// abstract simulation implementation
// every concurrency method has to implement this interface so the
// SandGrid node and the benchmark code can use them in the exact same way
// hopefully this is not overcomplicated for the goal of the project...
// runs like setup(w,h) -> load(cells) -> step() repeatedly -> read_back(cells)
class SimBackend {
public:
  virtual ~SimBackend() = default;

  // allocate space for a W*H grid in whichever representation the method uses
  virtual bool setup(int width, int height) = 0;

  // copy an initial CPU grid into the method representation
  virtual void load(const std::vector<uint8_t> &cells) = 0;

  // advance the simulation by one step, return true if at least one particle moved
  virtual bool step() = 0;

  // copy the current state back into a CPU grid for rendering
  virtual void read_back(std::vector<uint8_t> &out) = 0;

  // nocopy display texture for GPU backend, inspired from game of life godot implementation
  // given by TA (TODO: what's his name?)
  virtual Ref<Texture2D> get_display_texture() { return Ref<Texture2D>(); }

  virtual const char *name() const = 0;
};

}