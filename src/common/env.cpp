// nf::SetEnv: an environment variable of this process, which kernel::Relaunch's new process inherits (its own file:
// windows.h's names kept away from the rest)
#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace nf {

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    SetEnvironmentVariableA(name, value);  // the block CreateProcessW hands on
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

}  // namespace nf
