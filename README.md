# Falling sand game CSE305 project
by Ayoub Agouzoul & Rachid Tazi.

A falling sand cellular automaton implemented as a Godot 4 GDExtension (C++), with several interchangeable concurrency backends (sequential, parallel CPU, GPU compute) that can be switched at runtime and benchmarked against each other.

The Project report is available [here](report/Concurrent_Falling_Sand_Game_Final_Report_06_06___Rachid_Tazi___Ayoub_Agouzoul.pdf).

## Requirements

- **Godot 4.6+** (the `godot` binary on your `PATH`)
- **SCons** and a **C++17 compiler** (g++ or clang) to build the extension (cf. the official [Godot documentation](https://docs.godotengine.org/en/4.4/contributing/development/compiling/index.html))
- **Vulkan** drivers (needed by the GPU backends)
- For the benchmark plotting script: **Python 3** with `numpy`, `pandas`, `matplotlib`
  (`pip install numpy pandas matplotlib`)

## Build

```bash
git clone --recurse-submodules https://github.com/Rach0o0/Falling-sand-game.git
cd Falling-sand-game
scons platform=linux target=template_debug -j$(nproc) #or platform=macos or platform=windows
```

All commands are run from the project root. If you cloned without `--recurse-submodules`, fetch `godot-cpp` first with `git submodule update --init --recursive`.

## Run the game

```bash
godot . &           # run the simulation
godot --editor . &  # open the project in the Godot editor
```

In-game keyboard controls:

| Key | Action |
|-----|--------|
| `p` | pause / resume |
| `m` | cycle to the next backend (restarts from the same initial grid) |
| `s` | toggle slow motion (sleep between steps) |

To choose the starting backend, select the **SandGrid** node in the editor and set its **`method`** property in the Inspector. Available methods:
`CPU Sequential`, `CPU Parallel`, `GPU`, `CPU Parallel ColumnBand`, `GPU Margolus`, `CPU Margolus`.

## Benchmarks

```bash
godot --rendering-driver vulkan --script benchmark.gd
```

Runs the full sweep (CPU and GPU) and writes one raw row per timed run to `benchmarks/bench_<unixtime>.csv`. The GPU backends need a real device which is why we use the Vulkan driver. The sweep size is controlled by the constants at the top of `benchmark.gd` (sizes, thread counts, workgroups, repeats). They may be edited for a shorter run.

Turn a CSV into the report figures and printed tables:

```bash
python3 analyze_benchmarks.py #picks newest benchmarks/bench_*.csv, or
# python3 analyze_benchmarks.py --csv <file> --outdir <dir>
```

## Project structure

- `src/`: C++ simulation backends and the `SandGrid` node
- `sand_compute.glsl`, `sand_margolus_compute.glsl`: Godot GLSL GPU compute shaders
- `benchmark.gd`: benchmark sweep + `analyze_benchmarks.py`: plotting script
- `project.godot` / `main.tscn`: Godot project and main scene
- `SConstruct`: build script (SCons, like a Makefile)
- `godot-cpp/`: official C++ GDExtension bindings (git submodule)
- `report/`: LaTeX report files