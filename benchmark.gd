extends SceneTree

# main benchmark sweep, results in CSV
# run all backends windowed with: godot --rendering-driver vulkan --script benchmark.gd

# method ids from SandGrid::Method
const CPU_SEQUENTIAL := 0
const CPU_PARALLEL := 1
const GPU := 2
const CPU_PARALLEL_COLUMN_BAND := 3
const GPU_MARGOLUS := 4
const CPU_MARGOLUS := 5

# scenario ids from SandGrid::Scenario
const SCN_RANDOM_DENSE := 0
const SCN_CHECKERBOARD := 1
const SCN_FULL_UPPER_HALF := 2
const SCN_HOURGLASS := 3
const SCN_SINGLE_SOURCE := 4

# the four scenarios averaged (afterwards)
const SCENARIOS_SUITE := [SCN_RANDOM_DENSE, SCN_CHECKERBOARD, SCN_FULL_UPPER_HALF, SCN_HOURGLASS]

# benchmark hyperparams
const SEED := 42
const STEPS := 100
const REPEATS := 3

const ALL_METHODS := [CPU_SEQUENTIAL, CPU_PARALLEL, CPU_PARALLEL_COLUMN_BAND, CPU_MARGOLUS, GPU, GPU_MARGOLUS]
const PARALLEL_CPU_METHODS := [CPU_PARALLEL, CPU_PARALLEL_COLUMN_BAND, CPU_MARGOLUS]
const GPU_METHODS := [GPU, GPU_MARGOLUS]

const COMPARISON_SIZES := [128, 256, 512, 1024, 2048]
const CPU_SCALING_SIZES := [512, 1024, 2048]
const GPU_SIZES := [256, 512, 1024, 2048]
const WORKGROUPS := [[8, 8], [16, 16], [32, 32], [8, 32], [32, 8]]

const FAIRNESS_SIZE := 256
const FAIRNESS_STEPS := 700
const FAIRNESS_REPEATS := 1 # no need to repeat cuz its deterministic

var _file: FileAccess
var _csv_path: String


func _init() -> void:
	var grid := SandGrid.new()

	_open_csv()
	print("=== Falling sand benchmark sweep ===")
	print("cores reported by OS: ", OS.get_processor_count())
	print("writing raw rows to ", _csv_path)

	_sweep_comparison(grid)
	_sweep_cpu_scaling(grid)
	_sweep_gpu_workgroup(grid)
	_sweep_fairness(grid)

	_file.close()
	print("=== done -> ", _csv_path, " ===")

	grid.free()
	quit()


func _thread_counts() -> Array:
	var n := OS.get_processor_count()
	var base := [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64]
	var out := []
	for t in base:
		if t <= n and not out.has(t):
			out.append(t)
	if not out.has(n):
		out.append(n)
	return out


# full cores for the parallel CPU backends
func _default_threads(m: int) -> int:
	return OS.get_processor_count() if m in PARALLEL_CPU_METHODS else 0

# https://docs.godotengine.org/en/stable/tutorials/scripting/resources.html
func _open_csv() -> void:
	DirAccess.make_dir_recursive_absolute("res://benchmarks")
	_csv_path = "res://benchmarks/bench_%d.csv" % int(Time.get_unix_time_from_system())
	_file = FileAccess.open(_csv_path, FileAccess.WRITE)
	_file.store_line("sweep,method_id,method_name,scenario_id,scenario_name,width,height," +
		"threads,wg_x,wg_y,steps,repeat_idx,run_ms,ms_per_frame,mcells_per_s," +
		"particles_init,particles_final,conserved,fair_L,fair_R,fair_bias")


# one CSV row per repet
func _write_rows(sweep: String, r: Dictionary) -> void:
	if not r.get("ok", false):
		return
	var steps: int = r["steps"]
	var w: int = r["width"]
	var h: int = r["height"]
	var run_ms: Array = r["run_ms"]
	for i in run_ms.size():
		var ms: float = run_ms[i]
		var mspf := (ms / steps) if steps > 0 else 0.0
		var mcells := (float(w) * h * steps / (ms / 1000.0) / 1e6) if ms > 0.0 else 0.0
		var row := "%s,%d,%s,%d,%s,%d,%d,%d,%d,%d,%d,%d,%f,%f,%f,%d,%d,%d,%d,%d,%f" % [
			sweep, r["method_id"], r["method_name"], r["scenario_id"], r["scenario_name"],
			w, h, r["threads"], r["wg_x"], r["wg_y"], steps, i, ms, mspf, mcells,
			r["particles_init"], r["particles_final"], (1 if r["conserved"] else 0),
			r["fair_L"], r["fair_R"], r["fair_bias"]]
		_file.store_line(row)
	_file.flush()


#every method x size x the 4 scenarios
func _sweep_comparison(grid: SandGrid) -> void:
	print("\n-- comparison sweep --")
	for m in ALL_METHODS:
		for s in COMPARISON_SIZES:
			for scn in SCENARIOS_SUITE:
				var r := grid.run_benchmark_ex(m, s, s, scn, SEED, STEPS, _default_threads(m), 16, 16, REPEATS)
				_write_rows("comparison", r)


#parallel methods x size x thread count x the 4 scenario
#+ 1thread sequential reference
func _sweep_cpu_scaling(grid: SandGrid) -> void:
	print("\n-- cpu thread scaling sweep --")
	for s in CPU_SCALING_SIZES:
		for scn in SCENARIOS_SUITE:
			var rseq := grid.run_benchmark_ex(CPU_SEQUENTIAL, s, s, scn, SEED, STEPS, 1, 16, 16, REPEATS)
			_write_rows("cpu_scaling", rseq)
		for m in PARALLEL_CPU_METHODS:
			for t in _thread_counts():
				for scn in SCENARIOS_SUITE:
					var r := grid.run_benchmark_ex(m, s, s, scn, SEED, STEPS, t, 16, 16, REPEATS)
					_write_rows("cpu_scaling", r)


# GPU grid size x workgroup layout x the 4 scenarios
func _sweep_gpu_workgroup(grid: SandGrid) -> void:
	print("\n-- gpu workgroup sweep --")
	for m in GPU_METHODS:
		for s in GPU_SIZES:
			for wg in WORKGROUPS:
				for scn in SCENARIOS_SUITE:
					var r := grid.run_benchmark_ex(m, s, s, scn, SEED, STEPS, 0, wg[0], wg[1], REPEATS)
					_write_rows("gpu_workgroup", r)


# fairness test single source dropped and left to stabilize
func _sweep_fairness(grid: SandGrid) -> void:
	print("\n-- fairness sweep --")
	for m in ALL_METHODS:
		var r := grid.run_benchmark_ex(m, FAIRNESS_SIZE, FAIRNESS_SIZE, SCN_SINGLE_SOURCE,
			SEED, FAIRNESS_STEPS, _default_threads(m), 16, 16, FAIRNESS_REPEATS)
		_write_rows("fairness", r)