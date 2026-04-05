# Plan: Refactor openxr-simple-playground for Hackability

## TL;DR

Restructure the ~4400-line `main.cpp` into well-separated logical sections with a declarative extension registration system, a decomposed `main()`, and a clean renderer interface — all in a single file. The goal is to make adding a new OpenXR extension a localized, copy-paste-friendly operation, and to make the renderer and main loop easy to hack on.

---

## Current Problems

1. **main() is 2700 lines** — init, event handling, input polling, plane detection state machine, rendering, frame submission, and cleanup all in one function
2. **Adding a new extension touches 5+ places** — define struct, write init func, write fp-load func, add to `ext_init_funcs[]` array, cast with `get_ext()` at every use site, add extension-specific logic scattered across main loop
3. **Raw malloc/free everywhere** — inconsistent null checks, easy to leak (plane polygons, VBOs), no RAII despite being C++20
4. **Platform #ifdefs scattered** — Linux/Windows code interleaved throughout main() and init code
5. **Magic numbers** — 0.75 grab threshold, 0.005 velocity scale, 0.33 cube size, hardcoded colors as float arrays
6. **Rendering tightly coupled** — `render_frame()` directly queries extension state, hand tracking joint data, xdev lists
7. **Dead/disabled code** — `#if 0` blocks, plane detection hardcoded disabled, `render_quad()` copies same texture every frame
8. **Inconsistent memory management** — some pointers init to nullptr, some to NULL; mix of C and C++ allocation
9. **Math code is duplicated** — `XrMatrix4x4f_*` functions duplicated from OpenXR SDK, plus HandmadeMath included at line 4320 for a few helpers
10. **Bugs** — memory leak in plane polygon handling, potential infinite loop in `try_move()`, VBO never deleted

---

## Design Principles

- **Single file stays** — organized into clearly marked sections with `// ========` banners
- **One-block extension registration** — adding an extension = adding one contiguous block of code
- **Simple class inheritance for extensions** — a base `Extension` class with virtual methods is OK and natural here; but no deep hierarchies, no abstract factories, no CRTP
- **Modern C++ where intuitive** — `std::vector` for dynamic arrays, simple virtual dispatch, `override`. Avoid anything that requires a C++ expert to parse (no SFINAE, fold expressions, etc.). A C programmer reading the code should be able to follow the logic.
- **Replace C macros with typesafe C++ where practical** — C macros aren't easy to read either, so this is a judgement call per macro. Simple templates are fine and often clearer (e.g., a `load_fn<PFN_type>()` instead of `LOAD_OR_RETURN` macro). Complex template metaprogramming is not. The bar: "could a C programmer read this and go 'oh, that makes sense'?" Keep macros only where they genuinely can't be replaced simply (e.g., X-macros for enum→string reflection).
- **Keep the existing flavor** — the current code reads like C-with-classes, uses `printf`, `struct` keyword, procedural flow. Improve it but don't turn it into a different language. Match the "taste" of the existing section banners, naming, and commenting style.
- **Code that belongs together stays together** — prefer one function with local variables over a web of tiny helpers when a programmer will want to see/modify earlier state. Don't break the renderer into a generic mess of sub-functions — it's simple and should read as one flow.
- **Utility functions sparingly** — only extract when it genuinely reduces duplication or improves clarity, not for the sake of "clean architecture"
- **Copy-paste friendly** — existing extension blocks serve as templates for new ones

---

## Progress

| Commit | Phase | Description |
|--------|-------|-------------|
| `0432883` | pre | Port to C++20: fix compiler errors and idiom conversions |
| `7ef71bf` | 5 (partial) | Fix two linked list traversal bugs in try_move() |
| `39c5e39` | 4 (partial), 6 (partial) | Add CHECK_XR macros and named constants for magic numbers |
| `a2ae438` | 4 (partial) | Remove unused math_3d external dependency |
| `ec36b36` | 1 ✅ | Add section banners and annotate #if 0 option blocks |
| `ab61555` | 3 ✅ | Decompose main() into focused helper functions |
| `5b6eae7` | 2 ✅ | Replace base_extension_t with polymorphic Extension class |

**Done:** Phase 1 (section layout), Phase 2 (extension registry), Phase 3 (decompose main()),
plus partial cross-phase work: named constants, CHECK_XR macro, remove math_3d, fix linked list bugs.

**Remaining:**
- Phase 4: define `RenderContext` struct; evaluate HandmadeMath usage
- Phase 5: replace raw malloc/free with `std::vector`; fix plane polygon leak; fix VBO leak; enable plane detection
- Phase 6: `--help` output; `--list-extensions` flag; data-driven controller profiles

---

## Steps

### Phase 1: Section Reorganization (file layout) ✅

Reorder the file into clearly bannered sections. No logic changes — just move code blocks:

1. **Move code into this section order**, separated by `// ======== SECTION NAME ========` banners:
   - **(A) Platform & Includes** — all `#include`, platform `#ifdef` blocks, `gettimeofday` shim, GL function loading macros. Consolidate the scattered platform ifdefs into one block.
   - **(B) Constants & Types** — `ARRAY_SIZE`, `identity_pose`, `degrees_to_radians`, all enums, all struct definitions
   - **(C) Math** — all `XrMatrix4x4f_*` functions, vector helpers. Remove HandmadeMath dependency if its usage can be inlined (it's used for ~3 helpers: `HMM_V3`, `HMM_NormV3`, `HMM_LenV3` — replace with inline equivalents already partly in the file as `vec3_mag`/`vec3_norm`)
   - **(D) OpenXR Utilities** — `xr_check()`, enum-to-string macros, `print_system_properties()`, `print_supported_view_configs()`, etc.
   - **(E) Extension Registry** — the new extension system (see Phase 2)
   - **(F) Swapchain** — `_create_swapchain()`, `create_one_swapchain()`, `create_swapchain_from_views()`, `acquire_swapchain()`, `destroy_swapchain()`
   - **(G) Actions & Input** — `create_action()`, `suggest_actions()`, `create_action_space()`, `get_action_data()`, action profile definitions
   - **(H) Renderer** — all OpenGL code: shaders, `init_gl()`, `render_block/cube/frame()`, quad layer rendering
   - **(I) Application Lifecycle** — decomposed main (see Phase 3)
   - **(J) Entry Point** — `main()` becomes a thin dispatcher

### Phase 2: Declarative Extension System ✅

Replace the current scattered extension pattern with a simple class hierarchy.

2. **Define a base `Extension` class** with virtual methods, replacing `base_extension_t` and the function pointer tables. Keep it dead simple — a C programmer should read it and immediately understand:
   ```
   struct Extension {
       const char* name;
       bool supported;
       uint32_t version;
       
       virtual XrResult init_fp(XrInstance) { return XR_SUCCESS; }
       virtual void on_session_create(XrSession) {}
       virtual void on_frame(AppState&, XrTime) {}
       virtual void render(AppState&, RenderContext&) {}
       virtual void on_event(XrEventDataBuffer*) {}
       virtual void cleanup(XrInstance) {}
       virtual ~Extension() = default;
   };
   ```
   No `std::function`, no lambdas for callbacks — just plain virtual methods with sensible defaults.

3. **Each extension is a subclass** defined in one contiguous block containing: struct definition, all its data (function pointers, handles, state), and all its method implementations. The block is self-contained — you can read it top to bottom without jumping around the file.

4. **Create a static `Extension*` array** at file scope, populated by `new ExtHandTracking()`, etc. Adding an extension = write one class block + add one line to the array.

5. **Migrate each existing extension** to the new pattern:
   - `XR_KHR_opengl_enable` — `init_fp` loads `xrGetOpenGLGraphicsRequirementsKHR`; the requirements check stays in session setup (it's not per-frame)
   - `XR_MNDX_egl_enable` — marker only, no overrides needed
   - `XR_EXT_hand_tracking` — `init_fp` loads function pointers; `on_session_create` creates trackers; `on_frame` queries joint locations; `render` draws joints + velocities
   - `XR_KHR_composition_layer_depth` — `on_session_create` allocates depth info structs
   - `XR_FB_display_refresh_rate` — `on_session_create` queries/sets refresh rate
   - `XR_EXT_plane_detection` — `on_session_create` creates detector; `on_frame` runs the state machine; `render` draws detected planes
   - `XR_EXT_hand_interaction` — marker extension, only affects action profile suggestions
   - `XR_EXT_user_presence` — `on_event` handles presence changes
   - `XR_MNDX_xdev_space` — `on_frame` updates device list; `render` draws xdev cubes

6. **Document the recipe** as a comment block at the top of section (E) — a minimal skeleton showing the pattern. Something like "copy this block, change the name, add your data and override the methods you need."

### Phase 3: Decompose main() — conservatively ✅

The goal is NOT to shatter main() into dozens of tiny functions. It's to group the 2700-line monolith into a handful of coherent phases, where each phase keeps its local variables together so you can see and modify the flow.

7. **Extract these phase functions from main()**:
   - `init_openxr(ApplicationState&)` — everything from extension discovery through system/hardware query, view config, blend mode, SDL window, graphics binding, session creation. This is one logical "get OpenXR running" block. (~lines 2314-2544)
   - `setup_session(ApplicationState&)` — reference spaces, swapchain creation, depth setup, refresh rate, action set + actions + profiles + attach, hand tracker/plane detector creation via extension callbacks, GL init. Another logical block: "now that we have a session, set it up." (~lines 2545-2931)
   - `handle_events(ApplicationState&)` → returns `{quit, skip_render}` — SDL + OpenXR event polling. This is naturally self-contained. (~lines 2935-3016)
   - `run_frame(ApplicationState&)` — wait frame, locate views, sync actions, poll input, extension `on_frame` calls, acquire/render/release/submit. Keep the per-frame flow as **one function** — a programmer hacking on the frame loop wants everything visible. (~lines 3026-3357)
   - `cleanup(ApplicationState&)` — resource destruction (~lines 3358-3382)

8. **main() becomes ~30 lines**: parse args → `init_openxr` → `setup_session` → loop { `handle_events`, `run_frame` } → `cleanup`

Note: `run_frame` stays as one longer function rather than splitting into update/render/submit. The frame loop is where people hack, and they want to see the whole flow.

### Phase 4: Renderer Cleanup (partial — named constants and math_3d removal done)

9. **Define a `RenderContext` struct** passed to extension `render()` methods, containing: projection matrix, view matrix, play space, predicted display time, view index, uniform locations. This gives extensions what they need to draw without coupling them to `ApplicationState`.

10. **Replace magic numbers with named constants** at the top of the renderer section:
   - `GRAB_HAPTIC_THRESHOLD = 0.75f`
   - `VELOCITY_SCALE = 0.005f`
   - `CUBE_SIZE = 0.33f`
   - `FLOOR_SIZE = 20.0f`
   - Color constants: `COLOR_LEFT_HAND`, `COLOR_RIGHT_HAND`, `COLOR_CONTROLLER`, `COLOR_AIM`, `COLOR_XDEV`, `COLOR_FLOOR`

11. **HandmadeMath.h** — keep it unless the refactor truly eliminates all uses. It's a good, hackable math library. Don't remove it just to remove a dependency — only if no code needs it anymore. Do remove `math_3d.h` (it can't be included in C++ and is unused).

12. **Keep `render_frame()` as one function** — it's currently ~170 lines and reads top to bottom. Don't split it into `render_scene_cubes()`, `render_controllers()`, etc. — that makes it harder to hack on, not easier. The only change: extension-specific rendering (hand joints, xdevs, planes) moves into each extension's `render()` override, which `render_frame()` calls via a loop over the extension array. The core scene (cubes, controllers, floor) stays inline in `render_frame()`.

13. **Refactor dead rendering code** — the `#if 0` blocks are not useless; they represent alternative visualization modes. Convert them into runtime options (controlled by CLI flags or `args` fields) that are disabled by default. A programmer can flip them on without uncommenting code.

### Phase 5: Modernize Memory & Fix Bugs (partial — linked list bugs fixed)

14. **Replace raw malloc/free with `std::vector`** for dynamically-sized arrays where it makes the code cleaner:
   - `viewconfig_views`, `views`, `projection_views` → `std::vector`
   - `swapchain_lengths`, `images`, `swapchains` → `std::vector` members of `swapchain_t`
   - `acquired_color`, `acquired_depth` → `std::vector<uint32_t>`
   - Plane polygon vertices → `std::vector`
   - xdev space linked list → `std::vector<xdev_space_element>` (eliminates the linked list + `try_move` complexity entirely)
   
   Don't blindly convert everything — keep raw allocations where they're fine (e.g., one-shot temporary arrays freed in the same scope).

15. **Fix known bugs**:
   - Plane detection polygon memory leak
   - `try_move()` linked list traversal issues (moot after converting to vector)
   - VBO resource leak: add `glDeleteBuffers` in cleanup

16. **Enable plane detection when runtime supports it** — remove hardcoded disable. Add `--no-plane-detection` CLI flag if someone needs to suppress it.

### Phase 6: Quality of Life Improvements (partial — CHECK_XR macro done)

15. **Add `--help` output** listing all CLI flags with descriptions (currently `parse_opts` has no help message)

16. **Unify error handling** — `xr_check()` is good; ensure all OpenXR calls use it consistently. Add a `CHECK_XR(expr, msg)` macro that calls `xr_check` and returns false/error on failure, reducing the 3-line check pattern to 1 line.

17. **Add a `--list-extensions` flag** that prints supported extensions and exits — useful for quick runtime capability checks.

18. **Consolidate controller profile definitions** — the 5 interaction profile `suggest_actions()` calls in main use repetitive `Binding` arrays. Use a data-driven table: `{profile_path, {action, left_path, right_path}[]}` and loop.

---

## Relevant Files

- [openxr-simple-playground/main.cpp](openxr-simple-playground/main.cpp) — the only file to modify (single-file paradigm)
- [openxr-simple-playground/external/HandmadeMath.h](openxr-simple-playground/external/HandmadeMath.h) — keep unless truly unused after refactor
- [openxr-simple-playground/CMakeLists.txt](openxr-simple-playground/CMakeLists.txt) — remove math_3d include path
- [openxr-simple-playground/external/math_3d/math_3d.h](openxr-simple-playground/external/math_3d/math_3d.h) — remove (can't be included in C++, unused)

Key current patterns to reference:
- Extension init: `init_hand_tracking_t()` at line 1327, `init_opengl_t()` at line 1288
- Extension fp loading: `init_hand_tracking_fp()` at line 1307
- Extension usage: `get_ext()` calls + casts scattered in main()
- Rendering: `render_frame()` at line 4667
- Event handling: the `while (poll_result == XR_SUCCESS)` block starting ~line 2870

---

## Verification

1. **Build succeeds**: `cmake -B openxr-simple-playground/build openxr-simple-playground && cmake --build openxr-simple-playground/build` — must compile clean with `-Wall -Wextra`
2. **Runtime test**: Start Monado with remote driver, launch the app, send head/controller poses via `monado-remote-client`, confirm rendering works (cubes visible, controllers tracked, hand joints displayed if supported)
3. **Extension test**: Verify each extension still activates when the runtime supports it — check stdout for "Loading function pointers for extension X" messages
4. **Regression**: Compare stdout/stderr output before and after refactor with same runtime — should be functionally identical
5. **New extension smoke test**: After refactor, add a stub extension block following the documented recipe and verify it compiles and is discovered without touching any other code
6. **Memory**: Run under `valgrind --leak-check=full` — should show no leaks (fix all the known leak bugs)
7. **Style**: Run `git clang-format` and verify no unexpected changes outside project-owned code

---

## Decisions

- **Single file kept** — sections delineated by banner comments
- **Simple class inheritance for extensions** — a base `Extension` class with virtual methods. No deep hierarchies, no templates, no CRTP. A C programmer should be able to read it cold.
- **Modern C++ only where intuitive** — `std::vector`, `override`, virtual methods. No `std::function`, no `std::optional`, no structured bindings, no fold expressions. If a C programmer would squint at it, don't use it.
- **Keep the existing flavor** — the code reads like C-with-classes today. Keep `printf`, `struct` keyword habit, procedural flow. Improve structure without changing the language feel.
- **Don't over-decompose** — `render_frame()` stays as one readable function. `run_frame()` keeps the full per-frame flow together. Extract functions only for the big init/setup/cleanup phases.
- **Utility functions sparingly** — only where genuine duplication exists (e.g., the `render_block`/`render_cube` helpers are fine; don't invent more).
- **No dynamic extension registration** — the extension array is static/compile-time. Simpler and sufficient.
- **HandmadeMath.h** — keep unless the refactor eliminates all uses. Remove `math_3d.h` (unusable in C++).
- **`#if 0` blocks** — convert to runtime-toggled options (CLI flags / args fields), not deleted.
- **C macros → typesafe C++** — replace macros with simple templates or inline functions where it improves readability. Keep X-macros for enum reflection (no better alternative). Judgement call per macro.
- **Plane detection enabled by default** — add CLI flag to disable
- **xdev linked list → `std::vector`** — simpler, eliminates `try_move` bugs
- **Controller profiles as data tables** — reduces repetitive binding code

---

## Further Considerations

1. **Event handler dispatch via extensions** — Let extensions handle their own events via `on_event()` override instead of a monolithic switch. The core switch handles session state changes; extensions handle the rest. *Recommended.*

2. **Action system extensibility** — Add an `on_setup_actions()` virtual method so future extensions can register their own actions. *Recommended — but keep it simple: just let the extension add bindings to the existing action set.*

3. **Shader hot-reload** — `--watch-shaders` for rapid iteration. *Defer — feature addition beyond refactoring scope.*
