# Temple Run OpenGL Project: Complete Code and Pipeline Walkthrough

This document explains the current implementation in this repository. It is
written against the source tree as it exists now, especially:

- [`src/main.cpp`](src/main.cpp)
- [`src/common.h`](src/common.h)
- [`src/gfx.h`](src/gfx.h)
- [`src/shaders.h`](src/shaders.h)
- [`src/models.h`](src/models.h)
- [`src/world.h`](src/world.h)
- [`src/scene.h`](src/scene.h)
- [`src/characters.h`](src/characters.h)
- [`src/hud.h`](src/hud.h)

The project is a procedural, real-time OpenGL 3.3 Temple Run-style game. It
does not load model files or image assets for the core scene. Geometry,
materials, textures, lighting, terrain, obstacles, characters, and the sky are
created by C++ code at startup and drawn every frame.

## 1. What happens from source code to a running frame

The complete pipeline is:

```text
C++ source
  |
  | g++ compiles main.cpp and glad.c
  | main.cpp includes the project .h files
  v
Windows executable: src/output/main.exe
  |
  | glfwInit / window creation
  | GLAD loads OpenGL function pointers
  | buildMeshes creates VAOs/VBOs
  | initTextures creates procedural OpenGL textures
  | buildProgram compiles GLSL strings from shaders.h
  v
Per-frame loop
  |
  +-- read keyboard/mouse/window events
  +-- advance animation time
  +-- if not paused: simulate runner, monkey, collisions, turns, power-ups
  +-- choose camera and view/projection matrices
  +-- draw procedural sky
  +-- upload light/material/camera uniforms
  +-- draw visible world segments
  +-- draw runner and monkey
  +-- draw HUD
  +-- swap front/back buffers
```

There are two clocks:

- `gAnim`: real elapsed time. Cosmetic animation can continue while the game is
  paused.
- `gGame`: gameplay time. It stops while the game is paused, so obstacles,
  power-ups, the runner, and the monkey do not advance during inspection mode.

## 2. Build dependencies and responsibilities

### Required external/runtime pieces

| Dependency | Location | Responsibility |
|---|---|---|
| GLFW | `include/GLFW`, `lib/libglfw3dll.a`, `src/output/glfw3.dll` | Window, OpenGL context, keyboard, mouse, timing, buffers |
| GLAD | `include/glad/glad.h`, `src/glad.c` | Loads OpenGL 3.3 function pointers |
| GLM | `include/glm` | Vector, matrix, transform, and geometric math |
| OpenGL | Windows `opengl32` library | GPU rendering API |
| GDI | Windows `gdi32` library | Required by the GLFW Windows link setup |
| stb_easy_font | `src/stb_easy_font.h` | Small immediate-mode bitmap-like HUD text helper |

The project uses a MinGW-style compiler. The VS Code task in
[`.vscode/tasks.json`](.vscode/tasks.json) runs approximately:

```powershell
g++ -g -O2 -std=c++17 `
  src\main.cpp src\glad.c `
  -Iinclude -Llib `
  -lglfw3dll -lopengl32 -lgdi32 `
  -o src\output\main.exe
```

Only `main.cpp` and `glad.c` are compilation units. The project headers are
included into `main.cpp`; they are not separately compiled or run.

## 3. Include and initialization order

`main.cpp` includes:

1. `characters.h`
2. `hud.h`
3. `<cstring>`

Those headers include the lower-level project headers they need. In practical
terms the dependency direction is:

```text
main.cpp
  -> characters.h
  -> hud.h
      -> shaders.h
  -> models.h
      -> gfx.h
          -> common.h
          -> OpenGL / GLFW / GLM
  -> scene.h
  -> world.h
  -> stb_easy_font.h
```

The headers use `inline` functions and global inline variables because they are
intended to be included as part of one executable translation unit. This is
convenient for a course project, but a larger application would usually split
definitions into `.cpp` files and expose declarations through normal headers.

## 4. `src/common.h`: shared math and utility layer

See [`common.h`](src/common.h).

### Lines 1-30: includes and shared aliases

These lines include GLFW, GLAD, and GLM types, then import common GLM names
such as `vec2`, `vec3`, `mat4`, and `GLuint`. This keeps later rendering code
readable while still using strongly typed math objects.

### Lines 32-34: constants

`PI` and `TAU` are shared angle constants. They prevent repeated magic numbers
when generating circles, rotating models, and animating periodic effects.

### Lines 37-49: random helpers

- `frand()` produces a floating-point value in `[0, 1)`.
- `frange(a, b)` maps that value to an interval.
- `irange(a, b)` produces an inclusive integer.
- `chance(p)` returns true with probability `p`.

World generation uses these functions to vary scenery, obstacle placement,
coin placement, colors, and procedural texture noise.

### Lines 51-69: interpolation and angle helpers

- `lerpf` performs linear interpolation.
- `smoothf` performs a smooth interpolation with eased endpoints.
- `angleDiff` computes the shortest signed angular difference.
- `approach` moves a value toward a target without overshooting.

These are used for smooth lane movement, camera movement, character facing,
turn transitions, and animation blending.

### Lines 74-83: direction and color helpers

`dirFromYaw` and `rightFromYaw` convert a heading into forward/right vectors.
They are used by the free camera and by path-relative placement. Color helpers
convert between color representations used by materials and procedural texture
generation.

## 5. `src/gfx.h`: GPU resources, meshes, textures, and draw primitives

See [`gfx.h`](src/gfx.h).

### Lines 1-13: graphics-layer includes and texture identifiers

This section imports the shared math layer and declares texture slots. The
texture enum gives stable names such as ground, stone, wood, foliage, metal,
gold, and emissive/fire materials.

### Lines 15-51: `Program`

`Program` wraps an OpenGL shader-program handle. Its methods:

- Store the program ID.
- Look up uniform locations.
- Upload matrices, vectors, floats, integers, and arrays.

The wrapper centralizes `glUseProgram`, `glGetUniformLocation`, and the
`glUniform*` calls. Rendering code therefore expresses intent (`p.m4("uVP",
matrix)`) instead of repeating raw OpenGL boilerplate.

### Lines 53-78: `buildProgram`

`buildProgram`:

1. Creates a vertex shader.
2. Loads its source string.
3. Compiles it.
4. Prints the compiler log when compilation fails.
5. Repeats the process for the fragment shader.
6. Attaches both shaders to a program.
7. Links the program.
8. Prints a link log when linking fails.
9. Deletes intermediate shader objects.

This is where the Intel sky-shader error is reported. A shader source string
from `shaders.h` is compiled by the driver at runtime; it is not a separate
`shaders.exe` program.

### Lines 80-119: vertex format and mesh upload

`Vtx` contains:

- position
- normal
- UV coordinate
- vertex color

`MeshData` is a CPU-side vector of vertices. `uploadMesh` creates a VAO and VBO,
uploads the vertex array with `glBufferData`, and configures vertex attributes.
The attribute layout must match the `layout(location = ...)` declarations in
`SCENE_VS`.

### Lines 121-265: procedural mesh builders

The `addBox`, `addCyl`, `addSphere`, `addCone`, and related helpers append
triangles to `MeshData`. They do not draw immediately. They only construct
CPU-side geometry. The final upload happens once in `buildMeshes`.

Transforms passed to these functions place and scale primitives into complete
objects. For example, a cylinder can become a tree trunk, column, leg, torch,
or pillar depending on its transform and material.

### Lines 266-330: grid and procedural texture noise

`addGrid` creates a tessellated plane used by ground or water-like surfaces.
`hash21` and `fbm` create deterministic value noise. The noise is used to
produce textures without external image files.

The important distinction is:

- C++ `fbm` generates texture pixel data on the CPU.
- GLSL `valueNoise`/`fbm2` in the sky shader generates sky variation on the GPU.

They are unrelated functions and should not share ambiguous names with GLSL
built-ins.

### Lines 332-end: `initTextures`

`initTextures` allocates OpenGL 2D textures, fills them with generated pixel
data, sets filtering/wrapping parameters, and makes them available through the
texture enum. This gives the scene material variation while keeping the
repository self-contained.

## 6. `src/shaders.h`: all GLSL stages

See [`shaders.h`](src/shaders.h).

The file stores GLSL source as C++ raw string literals. The GPU only sees those
strings after `buildProgram` compiles them.

### Lines 17-94: shared lighting function

`GLSL_LIGHTING` defines uniforms and `computeLighting`.

The lighting model contains:

- hemisphere ambient light (`uAmbSky`, `uAmbGround`)
- directional sun/moon light (`uSunDir`, `uSunCol`)
- up to eight point lights
- up to two spot lights
- distance attenuation
- diffuse Lambert lighting
- specular highlights
- optional sun-shadow contribution

`computeLighting` writes separate diffuse, sun, specular, and sun-specular
terms so the fragment shader can apply shadows selectively.

### Lines 96-166: scene vertex shader

`SCENE_VS` transforms each vertex:

```text
object position
  -> model space
  -> world space using uModel
  -> camera space using uView
  -> clip space using uProj
```

It also transforms normals using the inverse-transpose normal matrix, passes UVs
and colors forward, and optionally computes lighting per vertex for Gouraud
shading.

### Lines 168-214: scene fragment shader support

`shadowRay` performs the lightweight ray-style shadow test used for the sun.
It samples the occupancy representation prepared by the C++ scene code. This
is not a full path tracer; it is a bounded visibility test suitable for a
real-time course project.

The final fragment shader combines material color, texture color, vertex color,
lighting, fog, and emission.

### Lines 217-227: sky vertex shader

The sky is a full-screen triangle. `gl_VertexID` generates its three vertices,
so no sky VBO is needed. The shader writes normalized device coordinates and
places the triangle at the far depth.

### Lines 228-303: sky fragment shader

The sky fragment shader:

1. Reconstructs a world-space direction using `uInvViewProj`.
2. Blends horizon and zenith colors.
3. Draws a sun disc and glow.
4. Draws a moon glow when the sun is below the horizon.
5. Adds procedural stars at night.
6. Adds subtle cloud/noise variation.

The helper is named `valueNoise`; it must remain distinct from any GLSL
built-in overload. The old invalid `shaders.exe` file was not this shader: it
was a GCC precompiled header accidentally given an `.exe` extension.

### Lines 304-327: HUD shaders

The HUD vertex shader maps 2D pixel coordinates to normalized device
coordinates. The HUD fragment shader outputs the color supplied by the CPU
font and bar drawing code.

### Lines 329-end: source composition

`sceneFragmentSource(bool gouraud)` prepends a `#define GOURAUD` when requested
and combines the shared lighting code with the scene shader body. This is how
the P/G shading switch changes the active pipeline without duplicating all
lighting code manually.

## 7. `src/models.h`: materials, culling, and object models

See [`models.h`](src/models.h).

### Lines 1-39: scene program and material state

`SceneProg` groups the Phong and Gouraud OpenGL programs. `gCur` points to the
program selected for the current frame.

`gDrawCalls` counts submitted object draws, which is useful for classroom
profiling and debugging.

`Mat` describes a material:

- base color
- texture slot
- UV scale
- specular strength
- shininess
- emission

### Lines 40-68: `mat` and `drawMesh`

`mat` is a compact material constructor. `drawMesh`:

1. Rejects invalid/empty meshes.
2. Performs visibility checks.
3. Uploads the model matrix.
4. Uploads material uniforms.
5. Binds textures.
6. Binds the VAO.
7. Calls `glDrawArrays` or the configured draw function.

This is the central C++ to GPU handoff for solid scene geometry.

### Lines 70-90: culling

`CullInfo` stores the current camera position and view limits. `inView` uses a
cheap distance/radius test to avoid issuing draw calls for objects too far from
the camera.

This is intentionally conservative: visual correctness is preferred over a
complex spatial acceleration structure.

### Lines 92-end: object constructors and mesh library

`Meshes G` stores reusable geometry. `buildMeshes` creates the primitive and
composite meshes used by the scene:

- ground strips
- temple pieces
- trees and bushes
- rocks, pillars, gates, statues
- coins and power-ups
- runner and monkey parts

Transform helpers `T`, `S`, and `RY` make object assembly readable.

## 8. `src/world.h`: procedural level and game objects

See [`world.h`](src/world.h).

### Lines 1-23: segment coordinate system

The world is an endless graph of `Segment` objects. Each segment has a length,
yaw, parent/child relationships, and a local coordinate system.

The runner does not move through one giant static world coordinate. Instead:

- `pSeg` identifies the current segment.
- `pS` is distance along that segment.
- `pLat` is lateral offset across the three lanes.
- `segPos` converts segment-local coordinates into world coordinates.

This design makes turns and branch paths possible without rewriting every
object's world coordinate when the road turns.

### Lines 24-70: lane, fork, prop, and difficulty definitions

These constants and enums define:

- three lane centers
- turn timing window
- obstacle kinds
- power-up kinds
- scenery kinds
- difficulty scaling

The `Segment` children represent left/right continuation choices. A missing
child is a dead end.

### Lines 71-175: collision geometry

`HitBox` describes an obstacle collision volume in segment-local coordinates.
`obstacleBoxes` produces one or more boxes for dynamic obstacles such as
spikes, gates, logs, blades, and fire.

Using boxes instead of testing a single center point lets the gameplay match the
visual shape more closely.

### Lines 176-301: scenery and segment content generation

`generateScenery` fills a segment with trees, rocks, pillars, temples, torches,
coins, and power-ups. Placement is seeded and constrained so the road remains
playable.

Obstacle patterns are selected according to difficulty. The generator is the
reason the runner sees different arrangements after restarting.

### Lines 302-end: child segments and path graph

`ensureChildren` creates the next left/right segments when the player gets near
the end of the current segment. It may create:

- only a left child
- only a right child
- both children
- a missing child representing a dead end

The graph is lazy: only nearby segments are created, which keeps memory and
draw work bounded while allowing the run to continue indefinitely.

## 9. `src/scene.h`: converting world data into draw calls

See [`scene.h`](src/scene.h).

### Lines 1-10: draw timing

`DrawTimes` separates animation time from gameplay time. This means decorative
animation can keep running in pause/free-camera mode while gameplay state stays
frozen.

### Lines 12-24: road strips and gap sorting

`strip` draws a rectangular section of a segment. `sortedGaps` determines which
parts of a road are missing, such as pits or broken bridge sections.

### Lines 25-77: segment ground

`drawSegmentGround` draws the road, lane markings, sides, and any branch
geometry for a segment. Segment-local transforms are converted into world
transforms by the segment's yaw and origin.

### Lines 78-125: pits and gaps

`drawSegmentPits` renders dangerous missing-road areas and their visual
boundaries. The collision/update code in `main.cpp` uses the same segment and
gap data to decide whether the player falls.

### Lines 126-170: static props

`drawProps` loops through generated scenery and draws only visible objects.
Different `PropKind` values select different reusable meshes/materials.

### Lines 171-274: obstacles and collectibles

`drawObstacle` chooses the visual model for each obstacle kind and applies its
animation state. `drawCollectibles` renders coins and power-ups, including
rotation, bobbing, glow, and pickup-specific colors.

The scene layer does not decide whether an object is collected. It only draws
the current state. Game rules remain in `main.cpp`.

## 10. `src/characters.h`: runner and monkey

See [`characters.h`](src/characters.h).

### Lines 1-22: pose data

`Pose` stores animation values such as leg swing, arm swing, airborne weight,
slide weight, stumble weight, and death/lie state.

The simulation computes these values; the drawing functions consume them.

### Lines 23-30: joint helpers

`jointRoot` creates a transform around a body joint. `limbDown` places and
rotates a cylinder-like limb below that joint. These helpers keep the player
and monkey models anatomically consistent.

### Lines 31-88: `drawPlayer`

The runner is assembled from reusable mesh parts:

- torso
- head
- arms
- legs
- optional slide/jump/death poses

The base transform comes from the current segment position and the player's
heading. It is not hard-coded to world origin.

### Lines 89-end: `drawMonkey`

The monkey uses the same pose and limb system with different proportions,
colors, and facial details. Its base position comes from `monkeyWorld`, which
places it behind the runner along the path graph even after turns.

## 11. `src/hud.h`: screen-space user interface

See [`hud.h`](src/hud.h).

### Lines 1-11: HUD state

`Hud` owns the screen-space VAO/VBO and the text buffer used by
`stb_easy_font`. It also stores framebuffer dimensions so the HUD can remain
correct after window resizing.

### Lines 12-end: overlay drawing

The HUD draws:

- score and distance
- coin count
- current speed
- power-up timers
- stumble warning
- pause/help overlay
- game-over reason
- performance counters

The HUD uses an orthographic pixel coordinate system, separate from the 3D
camera. This prevents UI elements from moving when the camera moves through
the world.

## 12. `src/main.cpp`: application state and orchestration

See [`main.cpp`](src/main.cpp).

### Lines 1-20: includes and file map

The comments describe the intended module split. Includes pull in character
drawing, HUD code, and string utilities. Through those headers, the main file
gets world, scene, model, graphics, shader, and math functionality.

### Lines 23-30: tuning constants

These are gameplay constants:

- base/max speed and speed ramp
- monkey gap and caught distance
- stumble timeout
- boost, magnet, shield durations
- gravity and jump velocity
- day/night duration

Keeping tuning values together makes balancing possible without searching
through simulation code.

### Lines 34-86: global runtime state

This block owns the application-wide state:

- GLFW window and framebuffer dimensions
- HUD and sky shader objects
- animation/game clocks
- pause/help/shading/light toggles
- game state (`GS_PLAY`, `GS_DYING`, `GS_OVER`)
- runner position and animation
- monkey position and chase gap
- power-up timers
- camera state

The code uses globals because this is a single-window course project. In a
larger design, these would be grouped into `Game`, `Renderer`, `World`, and
`Camera` objects.

### Lines 91-104: coordinate helpers

`inTurnWindow` answers whether a turn key can currently affect the runner.
`playerWorld` converts the runner's current segment-local position to world
space. `liveSegments` collects the current segment, previous segment, and
nearby children for drawing and collision visibility.

### Lines 106-122: monkey path placement

`monkeyWorld` follows the runner's segment graph. If the monkey is still on the
current segment it uses `pSeg`; otherwise it uses `pPrev`. This avoids the
common bug where the monkey jumps to a straight-line world position after a
turn.

### Lines 126-142: reset and death

`resetGame` creates a fresh starting segment graph and resets every gameplay
timer/state. `die` changes the state to `GS_DYING`, stores the cause, starts the
death animation, and updates the best score.

Death is centralized so obstacle collisions, falls, and missed turns all
produce consistent end-of-run behavior.

### Lines 144-176: stumble and power-up rules

`onHit` implements the two-stage stumble rule:

1. First hit starts `gStumbleTimer`, slows the runner, and moves the monkey
   closer.
2. A second hit before the timer expires calls `die(C_CAUGHT)`.
3. A shield consumes itself and prevents the stumble.
4. Temporary invulnerability prevents multiple hits in adjacent frames.

`pickPowerUp` starts the selected timer. A speed boost clears the stumble timer,
which returns the monkey to its normal gap.

### Lines 178-190: turning

`doTurn` selects the left or right child based on `pTurnPending`. If the child
does not exist, the runner has reached a dead end and dies with `C_WALL`.
Otherwise, the current segment becomes `pPrev`, the child becomes `pSeg`, and
the runner starts at the beginning of the new segment.

### Lines 192-277: collisions and game simulation

`collideObstacles`:

- Builds the player's vertical collision range.
- Iterates obstacles near the player.
- Builds obstacle hitboxes.
- Tests forward, lateral, and vertical overlap.
- Calls `onHit` for a collision.

The remaining simulation functions update:

- forward distance along the segment
- acceleration and speed boosts
- lane interpolation
- jumping and gravity
- sliding
- stumble timers
- magnet attraction
- coin collection
- falling into gaps
- turn windows
- monkey pursuit

Each update uses `dt`, so behavior is frame-rate independent.

### Lines 278-409: death animation and camera movement

`updateDying` animates the final stumble/catch sequence and eventually changes
the state to `GS_OVER`.

Camera functions select between:

- normal third-person chase view
- pause/free-camera inspection view
- smooth return from inspection mode
- orbit view during a catch/death sequence

The camera is calculated from the runner/path graph, not from a fixed global
`z` axis, so it can follow curved/turned segments.

### Lines 410-463: input actions

`actionSide` handles left/right lane changes or queues a path turn when the
runner is inside the turn window. This separation is important:

- before a fork: the same key changes lane
- near a fork: the key can choose a child segment

Other input handlers manage jump, slide, pause, free camera, shading mode,
lighting toggles, restart, and help.

### Lines 464-487: GLFW callbacks and camera input

These callbacks respond to:

- framebuffer resize
- key press/release
- mouse movement
- mouse button actions

The resize callback updates `gFbW/gFbH`, which affects both the 3D projection
and the HUD's pixel coordinate system.

### Lines 489-610: sky, scene, and frame rendering

`drawSky` binds the sky shader, uploads inverse view-projection and sky
parameters, disables depth writes as appropriate, and draws the full-screen
triangle.

`renderScene`:

1. Computes day/night sky state.
2. Selects Phong or Gouraud scene program.
3. Uploads camera, fog, ambient, sun, point, and spot light uniforms.
4. Uploads shadow/culling data.
5. Draws live segments.
6. Draws player and monkey.
7. Restores relevant OpenGL state.

### Lines 611-783: frame timing and initialization helpers

These lines contain utility code for:

- measuring frame times
- formatting HUD values
- setting up the sky VAO
- creating the GLFW window
- loading GLAD
- building meshes/textures/shaders
- printing driver and control information

### Lines 784-end: `main`

`main` is the executable entry point:

1. Initializes GLFW.
2. Requests an OpenGL 3.3 core profile.
3. Creates the window.
4. Registers callbacks.
5. Loads GLAD.
6. Enables depth testing, blending, face culling, and sRGB-related state
   where supported.
7. Builds meshes and procedural textures.
8. Builds Phong, Gouraud, sky, and HUD programs.
9. Resets the game.
10. Enters the loop.
11. Polls events.
12. Calculates `dt`.
13. Updates animation and gameplay.
14. Renders the frame.
15. Swaps buffers.
16. Cleans up GLFW on exit.

## 13. Exact per-frame interaction order

The order matters:

```text
GLFW event polling
  -> key/mouse callbacks update intent
  -> animation clock advances
  -> gameplay clock advances only when not paused
  -> runner simulation
  -> obstacle/pit/coin/power collision tests
  -> segment turn/dead-end decision
  -> monkey follows updated runner path
  -> camera follows updated path
  -> sky is rendered
  -> world geometry is rendered
  -> characters are rendered
  -> HUD is rendered last
  -> glfwSwapBuffers
```

If the world were rendered before simulation, the visible player and collision
state could be one frame out of sync. If the HUD were rendered before the 3D
scene, depth testing could hide it.

## 14. Phong versus Gouraud shading

The project supports both modes using the same lighting equations:

- **Phong:** normals and world positions reach the fragment shader; lighting is
  evaluated once per pixel.
- **Gouraud:** lighting is evaluated in the vertex shader; the resulting color
  is interpolated across the triangle.

Phong usually produces better specular highlights. Gouraud is useful for
demonstrating the visual difference and can be cheaper for simple geometry.

The switch changes the scene fragment source assembled by
`sceneFragmentSource`, then selects the corresponding `SceneProg`.

## 15. Lighting pipeline

For every visible object:

1. C++ computes world-space light positions and colors.
2. C++ uploads them as uniforms.
3. The vertex shader transforms position and normal.
4. `computeLighting` calculates ambient, directional, point, spot, and
   specular terms.
5. The shader optionally evaluates a bounded sun-shadow ray.
6. Material and texture color modulate the lighting result.
7. Fog blends distant geometry toward the sky fog color.
8. Emission adds self-lit color for fire, torches, and power-ups.

The shader does not know what an obstacle or tree means. It only sees geometry,
material uniforms, textures, lights, and camera data. Game semantics stay in
the C++ layer.

## 16. Why the project uses procedural assets

Procedural assets make the project:

- self-contained
- reproducible
- easy to submit
- free from missing texture-path errors
- suitable for a graphics course demonstration

External textures or models can be added later, but they would require:

1. image/model loading code
2. asset path handling
3. texture coordinate validation
4. packaging rules
5. filtering and color-space decisions

The current implementation deliberately avoids those extra failure points.

## 17. Running and troubleshooting

### Build

In VS Code:

1. Open the project root.
2. Press `Ctrl+Shift+B`.
3. Select **Build Temple Run (g++)**.

Or run:

```powershell
g++ -g -O2 -std=c++17 `
  .\src\main.cpp .\src\glad.c `
  -I.\include -L.\lib `
  -lglfw3dll -lopengl32 -lgdi32 `
  -o .\src\output\main.exe
```

### Run

```powershell
.\src\output\main.exe
```

`glfw3.dll` must be beside `main.exe`, which is why the repository keeps a
copy in `src/output`.

### Shader errors

Shader source is compiled at application startup. The file
`src/output/shaders.exe` is not part of the pipeline and must not be run. If a
file with that name appears and begins with `gpch`, it is a GCC precompiled
header incorrectly named as an executable and should be deleted.

### Header diagnostics

Opening a header directly in a C++ language service can report a warning such
as "`#pragma once` in main file". That does not mean the application build is
broken. The header is meant to be included by `main.cpp`, not compiled as a
standalone translation unit.

## 18. Design limitations and sensible next improvements

Current intentional limitations:

- one executable and one primary translation unit
- global state instead of subsystem classes
- procedural low-poly models
- bounded real-time shadow test rather than full ray tracing
- no asset loading pipeline
- simplified collision volumes

Good next improvements, in order:

1. Add a small automated shader-source validation step.
2. Separate global state into `GameState`, `Renderer`, and `Camera`.
3. Add tests for segment generation and turn availability.
4. Add a real text renderer if presentation quality requires it.
5. Add optional texture/model loading behind a clear asset directory.
6. Profile draw calls and segment culling before adding more geometry.

The most important invariant to preserve is that world generation, collision
logic, rendering, and camera placement all use the same segment-local
coordinate system. If one subsystem switches back to a global straight-road
assumption, turns and dead ends will become visually or mechanically
inconsistent.
