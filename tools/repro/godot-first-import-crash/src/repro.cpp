// Minimal GDExtension: one empty RefCounted class, nothing else.
#include <gdextension_interface.h>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

class Hello : public godot::RefCounted {
    GDCLASS(Hello, godot::RefCounted)
protected:
    static void _bind_methods() {}
};

static void init(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(Hello);
    }
}
static void deinit(godot::ModuleInitializationLevel) {}

extern "C" GDExtensionBool GDE_EXPORT repro_init(GDExtensionInterfaceGetProcAddress p, GDExtensionClassLibraryPtr l,
                                                 GDExtensionInitialization* i) {
    godot::GDExtensionBinding::InitObject o(p, l, i);
    o.register_initializer(init);
    o.register_terminator(deinit);
    o.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return o.init();
}
