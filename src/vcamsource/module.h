#pragma once

namespace vcam {

// Keeps the DLL loaded while COM objects it created are still alive.
void ModuleAddRef();
void ModuleRelease();
long ModuleObjectCount();

}  // namespace vcam
