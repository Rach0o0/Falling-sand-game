extends SceneTree

# main benchmark sweep
# run all backends windowed with: godot --rendering-driver vulkan --script benchmark.gd

const CPU_SEQUENTIAL := 0
const CPU_PARALLEL := 1
const GPU := 2
const CPU_PARALLEL_COLUMN_BAND:= 3
const GPU_MARGOLUS := 4

func _init() -> void:
	var grid := SandGrid.new()
	var methods := [CPU_SEQUENTIAL, CPU_PARALLEL, CPU_PARALLEL_COLUMN_BAND, GPU, GPU_MARGOLUS]
	var sizes := [128, 256, 512, 1024, 2048]
	var fill := 0.5
	var seed := 42
	var steps := 100

	print("=== Falling sand benchmark youpii ===")
	for m in methods:
		print("")
		for s in sizes:
			grid.run_benchmark(m, s, s, fill, seed, steps)

	grid.free()
	quit()


# TODO: single goutte point to check slope not biased
# TODO: cpu parallel see 1vs2vs3... threads speed
# TODO: gpu see blocksize/layout.. speed