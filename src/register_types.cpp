/*
C++ classes that we want to use in Godot
*/

#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "sand_grid.h"

using namespace godot;

void initialize_sand_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    //we initialize the class only if we are in the scene level
    return;
  }
  GDREGISTER_CLASS(SandGrid);
  //GDREGISTER_RUNTIME_CLASS(SandGrid);
}

void uninitialize_sand_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }
}

extern "C" {
GDExtensionBool GDE_EXPORT
sand_library_init( // equivalent of "main" function, i think?
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization) {
  //creates init_obj that prepares connexion btwn Godot and extension    
  godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library,
                                                 r_initialization);
  
                                                 
  init_obj.register_initializer(initialize_sand_module);
  init_obj.register_terminator(uninitialize_sand_module);
  init_obj.set_minimum_library_initialization_level(
      MODULE_INITIALIZATION_LEVEL_SCENE);

  return init_obj.init();
}
}
