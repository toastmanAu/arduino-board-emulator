# Submodule patches

Local fixes to vendored third-party code that we cannot easily upstream
(yet) but need for BoardGhost to function correctly. Applied at CMake
configure time by `runtime/CMakeLists.txt`.

## `lovyangfx/0001-skip-lock_t-wait.patch`

Stock `Panel_sdl::lock_t::~lock_t` posts the `_update_in_semaphore` and
then waits up to 1ms on `_update_out_semaphore`. That wait is a
back-pressure signal designed for `Panel_sdl::main()` to drive rendering
in step with sketch writes.

BoardGhost has its own `sim_main()` and doesn't run `Panel_sdl::main()`,
so the OUT semaphore never gets posted. Each lock_t destructor then times
out after 1ms — making per-pixel draws (e.g. `fillScreen` of 320×480,
`drawJpgFile`) serialise to ~1ms each, ≈150s for a full panel clear.

The patch comments out the wait. We accept slightly less smooth
rendering in exchange for sketches actually completing `setup()`.
