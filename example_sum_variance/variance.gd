extends Node

const N : int = 1 << 26
const WORKGROUP_COUNT: int = 64
const WORKGROUP_SIZE: int = 256

var time_start: int
var time_end: int

var time_cpu: int
var time_gpu: int


func _ready() -> void:
	var rd := RenderingServer.create_local_rendering_device()

	# Load GLSL shader.
	var shader_file := load("res://variance.glsl")
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
	var sums_bytes = PackedByteArray()
	sums_bytes.resize(WORKGROUP_COUNT * WORKGROUP_SIZE * 4) # 4 bytes per float
	var sums_buffer := rd.storage_buffer_create(sums_bytes.size(), sums_bytes)
	var squares_buffer := rd.storage_buffer_create(sums_bytes.size(), sums_bytes)

	# Create uniforms to assign the buffer to the rendering device.
	var input_uniform := RDUniform.new()
	input_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	input_uniform.binding = 0 # this needs to match the "binding" in our shader file
	input_uniform.add_id(input_buffer)
	var sums_uniform := RDUniform.new()
	sums_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	sums_uniform.binding = 1 # this needs to match the "binding" in our shader file
	sums_uniform.add_id(sums_buffer)
	var squares_uniform := RDUniform.new()
	squares_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	squares_uniform.binding = 2 # this needs to match the "binding" in our shader file
	squares_uniform.add_id(squares_buffer)

	# Create a uniform set. The last parameter (the 0) needs to match the "set" in our shader file.
	var uniform_set := rd.uniform_set_create([input_uniform, sums_uniform, squares_uniform], shader, 0)

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
	sums_bytes = rd.buffer_get_data(sums_buffer)
	var sums := sums_bytes.to_float32_array()
	var squares_bytes = rd.buffer_get_data(squares_buffer)
	var squares := squares_bytes.to_float32_array()
	#print("Input: ", input)
	#print("Sums: ", sums)
	#print("Squares: ", squares)
	var total_sum: float = 0.0
	var total_squares: float = 0.0
	for i in sums.size():
		total_sum += sums[i]
		total_squares += squares[i]
	time_end = Time.get_ticks_usec()
	time_gpu = time_end - time_start
	print("GPU variance: ", total_squares / N - total_sum * total_sum / (N * N))
	print("GPU time: ", time_gpu)

	time_start = Time.get_ticks_usec()
	var sum: float = 0.0
	var square: float = 0.0
	for i in N:
		sum += input[i]
		square += input[i] * input[i]
	time_end = Time.get_ticks_usec()
	time_cpu = time_end - time_start
	print("GDScript (CPU) variance: ", square / N - sum * sum / (N * N))
	print("GDScript (CPU) time: ", time_cpu)

	print("Speedup: ", float(time_cpu) / time_gpu)

	# Free memory.
	rd.free_rid(pipeline)
	rd.free_rid(uniform_set)
	rd.free_rid(input_buffer)
	rd.free_rid(sums_buffer)
	rd.free_rid(squares_buffer)
	rd.free_rid(shader)
	rd.free()
