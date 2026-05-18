## ANNEXE GODOT

- GDREGISTER_CLASS(MyClass)
class "MyClass" exists and I want to use it in Godot

- GDREGISTER_RUNTIME_CLASS(SandGrid)
class "SandGrid" used in runtime

- initialize_sand_module()
when Godot loads the extension 

- ModuleInitializationLevel
type Godot that indicates at which level Godot is initializating the extension

We use MODULE_INITIALIZATION_LEVEL_SCENE bevause SandGrid heritates from Node2D

- GDExtensionBool
equivalent to boolean

- GDE_EXPORT
function can be visible outside of the library

