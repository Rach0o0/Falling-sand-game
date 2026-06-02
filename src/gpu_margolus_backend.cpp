#include "gpu_margolus_backend.h"

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

/* ---------------------------------------------------------
  GPU
---------------------------------------------------------- */

GpuMargolusBackend::~GpuMargolusBackend() {
  // destructor needs to free only when script owns a local device (headless benchmark)
  // have to do it in that case
  // otherwise (global RenderingDevice) leave it to godot to manage memory
  // https://www.reddit.com/r/godot/comments/1pjt82v/psa_renderingserver_free_rid_and_renderingdevice/
  if (owns_rd && rd != nullptr) {
    if (uniform_sets[0].is_valid()) rd->free_rid(uniform_sets[0]);
    if (uniform_sets[1].is_valid()) rd->free_rid(uniform_sets[1]);
    if (pipeline.is_valid()) rd->free_rid(pipeline);
    if (shader.is_valid()) rd->free_rid(shader);
    if (gpu_textures[0].is_valid()) rd->free_rid(gpu_textures[0]);
    if (gpu_textures[1].is_valid()) rd->free_rid(gpu_textures[1]);
    memdelete(rd);
    rd = nullptr;
  }
}

bool GpuMargolusBackend::setup(int p_width, int p_height) {
  // receive GPU, create 2 GPU grids, copy initial sand, load shader, prepare links input/output

  width = p_width;
  height = p_height;

  current_texture_index = 0;
  phase = 0;
  step_count = 0;

  // https://forum.godotengine.org/t/choosing-global-vs-local-rendering-device/113880/2
  RenderingServer *rs = RenderingServer::get_singleton();
  if (force_local) {
    // benchmark: private device so steps can be submitted+synced for timing
    rd = rs->create_local_rendering_device();
    owns_rd = (rd != nullptr);
  } else {
    // interactive: main device (lets us display via Texture2DRD)
    rd = rs->get_rendering_device();
    owns_rd = false;
    if (rd == nullptr) {
      rd = rs->create_local_rendering_device();
      owns_rd = (rd != nullptr);
    }
  }
  if (rd == nullptr) {
    UtilityFunctions::print("[gpu margolus] no RenderingDevice (headless needs a real driver; run windowed)");
    return false;
  }

  //create 2 textures
  gpu_textures[0] = create_gpu_texture();
  gpu_textures[1] = create_gpu_texture();
  if (!gpu_textures[0].is_valid() || !gpu_textures[1].is_valid()) {
    UtilityFunctions::print("[gpu_margolus] texture_create failed");
    return false;
  }

  //load and compile shader glsl
  if (!create_compute_pipeline()) {
    UtilityFunctions::print("[gpu_margolus] create_compute_pipeline failed");
    return false;
  }

  //uniform sets
  uniform_sets[0] = create_uniform_set(gpu_textures[0], gpu_textures[1]);
  uniform_sets[1] = create_uniform_set(gpu_textures[1], gpu_textures[0]);
  if (!uniform_sets[0].is_valid() || !uniform_sets[1].is_valid()) {
    UtilityFunctions::print("[gpu_margolus] uniform_set_create failed");
    return false;
  }

  return true;
}

RID GpuMargolusBackend::create_gpu_texture() {
  // create a GPU image of size width x height usable by the shader

  Ref<RDTextureFormat> format;
  format.instantiate();
  format->set_width(width);
  format->set_height(height);
  format->set_format(RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT);
  format->set_usage_bits(
      RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
      RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
      RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
      RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);

  Ref<RDTextureView> view;
  view.instantiate();
  return rd->texture_create(format, view);
}

void GpuMargolusBackend::initialize_gpu_texture(RID texture_rid, const std::vector<uint8_t> &cells) {
  PackedFloat32Array data;
  data.resize(static_cast<int64_t>(width) * height * 4);

  for (int i = 0; i < width * height; ++i) {
    int p = i * 4;
    if (cells[i] == SAND) {
      data[p + 0] = 1.0f;   // R
      data[p + 1] = 0.85f;  // G
      data[p + 2] = 0.15f;  // B
      data[p + 3] = 1.0f;   // A
    } else if (cells[i] == WOOD) {
      data[p + 0] = 0.45f;
      data[p + 1] = 0.25f;
      data[p + 2] = 0.10f;
      data[p + 3] = 1.0f;
    }else {
      data[p + 0] = 0.0f;   // R
      data[p + 1] = 0.0f;   // G
      data[p + 2] = 0.0f;   // B
      data[p + 3] = 1.0f;   // A
    }
  }

  //CPU -> GPU
  PackedByteArray bytes = data.to_byte_array();
  rd->texture_update(texture_rid, 0, bytes);
}

void GpuMargolusBackend::load(const std::vector<uint8_t> &cells) {
  //initialize textures with actual grid
  initialize_gpu_texture(gpu_textures[0], cells);
  initialize_gpu_texture(gpu_textures[1], cells);
  current_texture_index = 0;
  phase = 0;
  step_count = 0;
}

bool GpuMargolusBackend::create_compute_pipeline() {
  String shader_path = "res://sand_margolus_compute.glsl";
  if (!FileAccess::file_exists(shader_path)) {
    return false;
  }

  //open glsl file
  Ref<FileAccess> file = FileAccess::open(shader_path, FileAccess::READ);
  if (file.is_null()) {
    return false;
  }
  //reads glsl code
  String shader_code = file->get_as_text();

  // #[compute] line causes errors glsl errors (sometimes???) (at least on Ayoub's laptop)
  // this stripping makes the "Failed parse:" errors disappear
  {
    PackedStringArray lines = shader_code.split("\n");
    String cleaned;
    for (int i = 0; i < lines.size(); ++i) {
      if (lines[i].strip_edges().begins_with("#[")) {
        continue;
      }
      cleaned += lines[i];
      cleaned += "\n";
    }
    shader_code = cleaned;
  }

  //create Godot shader source
  Ref<RDShaderSource> shader_source;
  shader_source.instantiate();
  //set language
  shader_source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
  //compute shader
  shader_source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, shader_code);

  //compile glsl
  Ref<RDShaderSPIRV> shader_spirv = rd->shader_compile_spirv_from_source(shader_source);
  String compile_err = shader_spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
  if (compile_err != "") {
    UtilityFunctions::print("[gpu_margolus] shader compile error: ", compile_err);
    return false;
  }

  //shader GPU
  shader = rd->shader_create_from_spirv(shader_spirv);
  if (!shader.is_valid()) {
    return false;
  }

  //pipeline
  pipeline = rd->compute_pipeline_create(shader);
  return pipeline.is_valid();
}

RID GpuMargolusBackend::create_uniform_set(RID input_texture, RID output_texture) {
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

bool GpuMargolusBackend::step() {
  //gpu not ready
  if (rd == nullptr || !pipeline.is_valid()) {
    return false;
  }

  //actual texture
  int input_idx = current_texture_index;
  //next texture
  int output_idx = 1 - current_texture_index; //if input_idx = 1 --> output_id = 0

  // push constant = grid dimensions sent straight to the glsl
  // size must be 16xN bytes so pad the 2 ints with 2 dummies
  //     https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html
  //     https://docs.godotengine.org/en/stable/tutorials/shaders/compute_shaders.html
  PackedByteArray push;
  push.resize(16);
  push.encode_s32(0, width);
  push.encode_s32(4, height);
  push.encode_s32(8, phase);
  push.encode_s32(12, step_count);

  //Margolus
  //number of blocks to be treated
  int blocks_x;
  int blocks_y;

  if (phase == 0){
    blocks_x = (width + 1)/ 2;
    blocks_y = (height + 1)/2;
  } else {
    blocks_x = (width + 2)/2;
    blocks_y = (height+2)/2;
  }
  int groups_x = (blocks_x + 15) /16;
  int groups_y = (blocks_y + 15)/16;


  //GPU command list, like a recipe
  int64_t compute_list = rd->compute_list_begin();
  rd->compute_list_bind_compute_pipeline(compute_list, pipeline);
  rd->compute_list_bind_uniform_set(compute_list, uniform_sets[input_idx], 0);
  rd->compute_list_set_push_constant(compute_list, push, push.size());


  rd->compute_list_dispatch(compute_list, groups_x, groups_y, 1);
  rd->compute_list_end();

  // this is what makes the GPU headless benchmark reflect real GPU time
  // submit+sync makes the work actually executes:
  // https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html
  // https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html#class-renderingdevice-method-submit
  // https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html#class-renderingdevice-method-sync
  if (owns_rd) {
    rd->submit();
    rd->sync();
  }

  current_texture_index = output_idx;

  //alternate phase
  phase = 1 - phase;
  step_count++;

  return true;
}

void GpuMargolusBackend::read_back(std::vector<uint8_t> &out) {
  if (rd == nullptr) {
    return;
  }
  out.assign(static_cast<size_t>(width) * height, EMPTY);
  PackedByteArray bytes = rd->texture_get_data(gpu_textures[current_texture_index], 0);
  PackedFloat32Array floats = bytes.to_float32_array();
  for (int i = 0; i < width * height; ++i) {
    float r = floats[i * 4 + 0];
    float g = floats[i * 4 + 1];

    if (r > 0.8f && g > 0.7f) {
      out[i] = SAND;
    } else if (r > 0.3f) {
      out[i] = WOOD;
    } else {
      out[i] = EMPTY;
    }
  }
}

Ref<Texture2D> GpuMargolusBackend::get_display_texture() {
  // avoid RID leak warning for Texture2DRD
  if (owns_rd) {
    return Ref<Texture2D>();
  }
  //texture gpu -> texture2DRD -> sprite2D
  Ref<Texture2DRD> tex;
  tex.instantiate();
  tex->set_texture_rd_rid(gpu_textures[current_texture_index]);
  return tex;
}

}
