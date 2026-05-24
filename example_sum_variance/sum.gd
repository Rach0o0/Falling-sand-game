extends Node

const N : int = 1 << 23
const WORKGROUP_COUNT: int = 128
const WORKGROUP_SIZE: int = 1024

var time_start: int
var time_end: int

var time_cpu: int
var time_gpu: int


func _ready() -> void:
	var rd := RenderingServer.create_local_rendering_device()

	# Load GLSL shader.
	var shader_file := load("res://sum.glsl")
	var shader_spirv: RDShaderSPIRV = shader_file.get_spirv()
	var shader := rd.shader_create_from_spirv(shader_spirv)

	# Prepare our input data.
	var arr: Array[float]
	arr.resize(N)
	for i in N:
		arr[i] = randf()
	var input := PackedFloat32Array(arr)

	# Create storage buffers that can hold our float values.
	var input_bytes := input.to_byte_array()
	var input_buffer := rd.storage_buffer_create(input_bytes.size(), input_bytes)
	var output_bytes = PackedByteArray()
	output_bytes.resize(WORKGROUP_COUNT * WORKGROUP_SIZE * 4) # 4 bytes per float
	var output_buffer := rd.storage_buffer_create(output_bytes.size(), output_bytes)

	# Create uniforms to assign the buffer to the rendering device.
	var input_uniform := RDUniform.new()
	input_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	input_uniform.binding = 0 # this needs to match the "binding" in our shader file
	input_uniform.add_id(input_buffer)
	var output_uniform := RDUniform.new()
	output_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	output_uniform.binding = 1 # this needs to match the "binding" in our shader file
	output_uniform.add_id(output_buffer)

	# Create a uniform set. The last parameter (the 0) needs to match the "set" in our shader file.
	var uniform_set := rd.uniform_set_create([input_uniform, output_uniform], shader, 0)

	# Create a compute pipeline.
	var pipeline := rd.compute_pipeline_create(shader)
	var compute_list := rd.compute_list_begin()
	rd.compute_list_bind_compute_pipeline(compute_list, pipeline)
	rd.compute_list_bind_uniform_set(compute_list, uniform_set, 0)
	rd.compute_list_dispatch(compute_list, WORKGROUP_COUNT, 1, 1)
	rd.compute_list_end()

	# Submit to GPU and wait for sync
	time_start = Time.get_ticks_usec()
	rd.submit()
	rd.sync()

	# Read back the data from the buffer
	var out_bytes := rd.buffer_get_data(output_buffer)
	var outputs := out_bytes.to_float32_array()
	#print("Input: ", input)
	#print("Outputs: ", outputs)
	var output: float = 0.0
	for i in outputs.size():
		output += outputs[i]
	time_end = Time.get_ticks_usec()
	time_gpu = time_end - time_start
	print("GPU sum: ", output)
	print("GPU time: ", time_gpu)

	time_start = Time.get_ticks_usec()
	var sum: float = 0.0
	for i in N:
		sum += input[i]
	time_end = Time.get_ticks_usec()
	time_cpu = time_end - time_start
	print("GDScript (CPU) sum: ", sum)
	print("GDScript (CPU) time: ", time_cpu)

	print("Speedup: ", float(time_cpu) / time_gpu)

	# Free memory.
	rd.free_rid(pipeline)
	rd.free_rid(uniform_set)
	rd.free_rid(input_buffer)
	rd.free_rid(output_buffer)
	rd.free_rid(shader)
	rd.free()
