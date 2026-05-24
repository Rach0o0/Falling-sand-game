#include "sand_grid.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <chrono>
#include <iostream>
#include <random>


using namespace godot;

SandGrid::SandGrid() {}

/* ---------------------------------------------------------
  SETTINGS
---------------------------------------------------------- */

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

void SandGrid::randomize() {
  //fill the grid randomly with sand
  std::mt19937 rng(0);
  std::bernoulli_distribution pileouface(0.10);
  for (auto &c : cells)
    c = pileouface(rng) ? SAND : EMPTY;
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

  timing_enabled = true;
  simulation_finished = false;
  simulation_steps = 0;
  simulation_start_time = std::chrono::high_resolution_clock::now();

  //GPU 
  if (gpu_enabled){
    if (!setup_gpu()){
      //gpu doesn't work
      gpu_enabled = false;
      //cpu method
      upload_to_texture();
      return;
    }

    //initial GPU texture 
    update_display_texture();

    return;
  }

  upload_to_texture();

  
}

//each frame
void SandGrid::_process(double delta) {
  //we still don't use delta
  (void)delta;

  if (Engine::get_singleton()->is_editor_hint()) {
    return;
  }

  if (simulation_finished) return;

  //GPU 
  if (gpu_enabled){
    run_gpu_step();
    simulation_steps++;
    return;
  }

  //CPU
  bool moved = step();
  simulation_steps++;

  upload_to_texture();

  if (!moved){
    auto end_time = std::chrono::high_resolution_clock::now();

    double time = std::chrono::duration<double, std::milli>(end_time - simulation_start_time).count();
    UtilityFunctions::print("Simulation ended after ", simulation_steps, " steps in ", time, " ms.");

    simulation_finished = true;
  }
}

/* ---------------------------------------------------------
  SIMULATION RULES
---------------------------------------------------------- */

//rules for sand
bool SandGrid::apply_sand_rules(int x, int y){
  if (cells[idx(x,y)] != SAND){
    return false;
  }

  //try move down
  if (try_move(x,y,x,y+1)){
    return true;
  }

  //see where sand can fall
  bool can_fall_left = in_grid(x-1, y+1) && cells[idx(x-1, y+1)] == EMPTY;
  bool can_fall_right = in_grid(x+1, y+1) && cells[idx(x+1, y+1)] == EMPTY;

  //if both are possible -> random
  if (can_fall_left && can_fall_right) {
    std::mt19937 rng(0);
    std::bernoulli_distribution coin_flip(0.5);
    
    if (coin_flip(rng)){
      return try_move(x, y,x-1, y+1);
    } else {
      return try_move(x, y, x+1 , y+1);
    }
  }

  //left
  if (can_fall_left) {
    return try_move(x, y, x - 1, y + 1);
  }

  //right 
  if (can_fall_right) {
    return try_move(x, y, x + 1, y + 1);
  }

  return false;
}

//try to move sand
bool SandGrid::try_move(int from_x, int from_y, int to_x, int to_y){
  if (!in_grid(to_x, to_y)){
    return false;
  }

  int from = idx(from_x, from_y);
  int to = idx(to_x, to_y);

  if (cells[to] != EMPTY){
    return false;
  }

  cells[from] = EMPTY;
  cells[to] = SAND;

  return true;
}

/* ---------------------------------------------------------
  SIMULATION FUNCTIONS
---------------------------------------------------------- */

//one step of the simulation
bool SandGrid::step() {
  //sand has moved ?
  bool moved = false;

  for (int y = height - 2; y >= 0; --y) {
    for (int x = 0; x < width; ++x) {
      if (apply_sand_rules(x,y)) {
        moved = true;
      }
    }
  }

  return moved;
}

//benchmark
void SandGrid::run_until_stable(){
  using clock = std::chrono::high_resolution_clock;
  auto start = clock::now();

  int steps = 0;

  while (true) {
    bool moved = step();
    steps++;

    if(!moved){
      break;
    }
  }
  auto end = clock::now();

  double time = std::chrono::duration<double, std::milli>(end - start).count();

  UtilityFunctions::print("Simulation ended after ", steps, " steps in ", time, " ms.");
  
  upload_to_texture();
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
  GPU
---------------------------------------------------------- */

bool SandGrid::setup_gpu(){
  /*
  receive GPU, create 2 GPU grids, copy initial sand, load shader, prepare links input/output
  */
  
  rd = RenderingServer::get_singleton()->get_rendering_device();

  if (rd == nullptr){
    return false;
  }

  //create 2 textures 
  gpu_textures[0] = create_gpu_texture();
  gpu_textures[1] = create_gpu_texture();

  if (!gpu_textures[0].is_valid() || !gpu_textures[1].is_valid()){
    return false;
  }

  //initialize textures with actual grid
  initialize_gpu_texture(gpu_textures[0]);
  initialize_gpu_texture(gpu_textures[1]);

  //load and compile shader glsl
  if (!create_compute_pipeline()){
    return false;
  }

  //uniform sets
  uniform_sets[0] = create_uniform_set(gpu_textures[0], gpu_textures[1]);
  uniform_sets[1] = create_uniform_set(gpu_textures[1], gpu_textures[0]);

  if (!uniform_sets[0].is_valid() || !uniform_sets[1].is_valid()) {
    return false;
  }

  return true;
}

RID SandGrid::create_gpu_texture(){
  /*
  create a GPU image of size width x height usable by the shader
  */

  Ref<RDTextureFormat> format;
  format.instantiate();

  format->set_width(width);
  format->set_height(height);

  format->set_format(RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT);

  format->set_usage_bits(
    RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
    RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
    RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
    RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT
  );

  Ref<RDTextureView> view;
  view.instantiate();

  return rd->texture_create(format, view);
}

void SandGrid::initialize_gpu_texture(RID texture_rid){
  PackedFloat32Array data;
  data.resize(width*height*4);

  for (int y = 0; y < height; y++){
    for (int x = 0; x < width; x ++){
      int cell_index = idx(x,y);
      int pixel_index = cell_index * 4;

      uint8_t cell = cells[cell_index];

      if (cell == SAND){
        data[pixel_index + 0] = 1.0f;   // R
        data[pixel_index + 1] = 0.85f;  // G
        data[pixel_index + 2] = 0.15f;  // B
        data[pixel_index + 3] = 1.0f;   // A
      } else {
        data[pixel_index + 0] = 0.0f;   // R
        data[pixel_index + 1] = 0.0f;   // G
        data[pixel_index + 2] = 0.0f;   // B
        data[pixel_index + 3] = 1.0f;   // A
      }
    }
  }

  //CPU -> GPU
  PackedByteArray bytes = data.to_byte_array();
  rd->texture_update(texture_rid, 0, bytes);
}

bool SandGrid::create_compute_pipeline(){
  String shader_path = "res://sand_compute.glsl";

  if (!FileAccess::file_exists(shader_path)){
    return false;
  }

  //open glsl file
  Ref<FileAccess> file = FileAccess::open(shader_path, FileAccess::READ);

  if (file.is_null()){
    return false;
  }

  //reads glsl code 
  String shader_code = file->get_as_text();

  //create Godot shader source
  Ref<RDShaderSource> shader_source;
  shader_source.instantiate();
  
  //set language
  shader_source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
  //compute shader
  shader_source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, shader_code);

  //compile glsl
  Ref<RDShaderSPIRV> shader_spirv = rd->shader_compile_spirv_from_source(shader_source);

  if(shader_spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE) != ""){
    return false;
  }

  //shader GPU
  shader = rd->shader_create_from_spirv(shader_spirv);
  if (!shader.is_valid()){
    return false;
  }

  //pipeline
  pipeline = rd->compute_pipeline_create(shader);
  if (!pipeline.is_valid()){
    return false;
  }

  return true;
}

RID SandGrid::create_uniform_set(RID input_texture, RID output_texture){
  
  //input texture
  Ref<RDUniform> input_uniform;
  input_uniform.instantiate();

  input_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
  input_uniform->set_binding(0);
  input_uniform->add_id(input_texture);

  //output texture
  Ref<RDUniform> output_uniform;
  output_uniform.instantiate();

  output_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
  output_uniform->set_binding(1);
  output_uniform->add_id(output_texture);

  //put uniforms in Godot arrays
  Array uniforms;
  uniforms.append(input_uniform);
  uniforms.append(output_uniform);

  //uniform set
  return rd->uniform_set_create(uniforms, shader, 0);
}

void SandGrid::run_gpu_step(){
  //gpu not ready
  if (rd == nullptr || !pipeline.is_valid()){
    return;
  }

  frame_counter++;

  if (frame_counter < frames_per_update){
    return;
  }

  frame_counter = 0;

  //actual texture
  int input_idx = current_texture_index;
  //next texture
  int output_idx = 1 - current_texture_index; //if input_idx = 1 --> output_id = 0

  //GPU command list 
  int64_t compute_list = rd->compute_list_begin();
  rd->compute_list_bind_compute_pipeline(compute_list, pipeline);
  rd->compute_list_bind_uniform_set(compute_list, uniform_sets[input_idx], 0);

  
  int groups_x = (width + 15) / 16;
  int groups_y = (height + 15) / 16;

  rd->compute_list_dispatch(compute_list, groups_x, groups_y, 1);

  rd->compute_list_end();

  current_texture_index = output_idx;

  update_display_texture();
}

void SandGrid::update_display_texture() {
  //texture gpu -> texture2DRD -> sprite2D
  Ref<Texture2DRD> tex;
  tex.instantiate();

  tex->set_texture_rd_rid(gpu_textures[current_texture_index]);

  set_texture(tex);
}