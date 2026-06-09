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

## `lovyangfx/0002-require-ctrl-for-rotate.patch`

Stock LGFX maps `R` and `L` (bare keypress, no modifier) to "rotate the
SDL display window 90°". The default shortcut modifier
(`Panel_sdl::_keymod`) is `KMOD_NONE`, which the existing condition
checks with `event.key.keysym.mod == _keymod` — i.e. zero modifiers
pressed. In practice this means any context that bubbles an `R` or `L`
keypress to the SDL window will silently flip the display, including
moments where you think a sketch input field has focus.

This patch changes the rotate-only branch to require Ctrl
(`event.key.keysym.mod & KMOD_CTRL`). The zoom keys (`1`–`6`) keep
their stock `_keymod`-driven behaviour because no analogous footgun
has been reported there. Callers who set a custom modifier via
`Panel_sdl::setShortcutKeymod()` are unaffected for zoom; rotate
unconditionally requires Ctrl now.
