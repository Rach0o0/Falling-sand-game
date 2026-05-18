#pragma once

#include <godot_cpp/core/class_db.hpp>
/*
contains functions such that GDREGISTER_CLASS() or ModuleInitializationLevel
*/

using namespace godot;

void initialize_sand_module(ModuleInitializationLevel p_level);
void uninitialize_sand_module(ModuleInitializationLevel p_level);
