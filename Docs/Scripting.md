# Lapis Luau Scripting API

Gameplay code is written in [Luau](https://luau.org) (Roblox's typed Lua).
Scripts live in a project's `Scripts/` folder and are attached to entities in
a scene (`Script path="Scripts/Thing.luau"`), or run once at startup via
`MainScript=` in `project.lapis`.

## Script structure

```lua
local Door = {}            -- the script table; fields are shared defaults
Door.OpenHeight = 3

function Door:OnStart()    -- once, when play starts (or when spawned)
    self.closed = self.Entity.Position   -- per-entity state lives on `self`
end

function Door:OnUpdate(dt: number)        -- every frame
end

function Door:OnCollision(other)          -- a collision/trigger overlap began
end

function Door:OnDestroy()                 -- entity destroyed or play stopped
end

return Door
```

`self.Entity` is the entity the script is attached to. Another entity's script
instance is reachable through `entity.Script` (e.g. `other.Script:Collect()`).

## Types

* **Vector3** - Luau's native `vector`: `Vector3.new(x, y, z)`, `v.x`, `v + w`,
  `v * 2`. Helpers: `Vector3.zero/one/up/right/forward`, `Length`, `Normalize`,
  `Dot`, `Cross`, `Lerp`, `Distance`.
* **Color** - also a vector (r, g, b in 0..1): `Color.new`, `Color.Red`, ...,
  `Color.HSV(h, s, v)`.

## Entity

| Member | |
|---|---|
| `Name` | read/write string |
| `Position`, `Rotation` (Euler degrees), `Scale` | read/write vectors (local) |
| `WorldPosition`, `Forward`, `Right`, `Up` | read-only vectors |
| `Velocity` | read/write (rigid bodies) |
| `Visible` | read/write bool |
| `Parent`, `Script`, `IsValid`, `Id` | read-only |
| `:Destroy()` `:AddForce(v)` `:AddImpulse(v)` `:SetColor(c)` `:SetEmissive(c)` `:LookAt(p)` `:SetParent(e)` `:GetChildren()` | methods |

## Libraries

* **Scene**: `Find(name)`, `Spawn{...}`, `Destroy(e)`, `GetCamera()`,
  `GetEntities()`, `SetSun(dir, intensity)`, `SetFog(density)`,
  `SetExposure(x)`, `SetSky(zenith, horizon, ground)`, `SetDebugColliders(bool)`.
  `Spawn` accepts: `Name, Mesh ("Cube"/"Sphere"/"Plane"/"Models/x.obj"),
  Position, Rotation, Scale, Color, Roughness, Metallic, Emissive, Texture,
  Tiling, Unlit, CastShadows, Physics ("Dynamic"/"Static"/"Kinematic"),
  Collider ("Box"/"Sphere"), Trigger, Mass, Restitution, Friction, Velocity,
  GravityScale, Light ("Point"/"Directional"), LightColor, Intensity, Range,
  Camera, FieldOfView, Parent, Script`.
* **Input**: `IsKeyDown(k)`, `WasKeyPressed(k)`, `WasKeyReleased(k)`,
  `IsMouseDown(b)`, `WasMousePressed(b)`, `GetMousePosition()`,
  `GetMouseDelta()`, `GetScroll()`. Keys: `"A".."Z"`, `"0".."9"`, `"Space"`,
  `"Enter"`, `"Escape"`, `"Shift"`, `"Control"`, `"Alt"`, `"Tab"`, arrows,
  `"F1".."F12"`. Mouse: `"Left"`, `"Right"`, `"Middle"`.
* **Physics**: `Raycast(origin, dir, maxDist?, ignoreEntity?)` ->
  `{Entity, Position, Normal, Distance}` or nil, `OverlapSphere(center, r)`,
  `SetGravity(v)`, `GetGravity()`.
* **HUD** (2D overlay, call every frame): `Text(x, y, text, color?, scale?, alpha?)`,
  `Rect(x, y, w, h, color?, alpha?)`, `ScreenSize()`.
* **Debug**: `Log(...)` (= `print`), `Warn(...)`, `DrawLine(a, b, color?)`.
* **Time**: `Now()` seconds since play started.
* **App**: `Quit()`, `SetTitle(text)`, `LockCursor(bool)`.

Scripts run sandboxed: each has its own globals, built-in libraries are
read-only, and there is no file or OS access.

## Graphics (in-game settings menus)

Scripts edit a copy of the graphics settings, then apply them. The same
settings exist in Project > Game Graphics Settings (the game's defaults).

| Function | Description |
|---|---|
| `Graphics.Get(key)` | Current value: boolean, number, or the option name (e.g. `"MSAA 4x"`) |
| `Graphics.Set(key, value)` | Change a setting (number, boolean, or option name/index). Returns `true` if valid |
| `Graphics.Apply()` | Apply all changes made with `Set` / `Preset` / `Reset` / `Load` |
| `Graphics.Preset(name)` | `"Low"`, `"Medium"`, `"High"` or `"Ultra"` (quality + effects only) |
| `Graphics.Reset()` | Back to the game's defaults from project.lapis |
| `Graphics.Save()` / `Graphics.Load()` | Player choices in `Saved/graphics.cfg` (loaded automatically at startup) |
| `Graphics.Settings()` | Every setting: `{Key, Label, Category, Type, Help, Value, Min, Max, Step, Options}` |
| `Graphics.Options(key)` | `{ {Name, Available, Reason}, ... }` for this computer (DLSS only listed on NVIDIA RTX) |
| `Graphics.Categories()` | `{"Display", "Quality", "Effects", "Post Processing", "Frame Generation"}` |
| `Graphics.GPU()` | `{Name, Vendor, RTX, API}` |

Unavailable options (e.g. FSR 3 on the OpenGL renderer) can still be chosen;
the engine falls back automatically. A complete menu: `Templates/FirstPerson/Scripts/SettingsMenu.luau`.

## Python (secondary language)

Luau is Lapis' main scripting language. Python scripts are supported too,
using the Python already installed on the computer (64-bit **CPython 3.8+**,
e.g. from python.org). The engine finds it automatically (File > Settings
shows which one); set `LAPIS_PYTHON` to a `python3xx.dll` / `libpython3.x.so`
to choose a specific one. Exported games that use Python need Python on the
player's PC (bundling comes with the in-editor Python setup).

Attach a `.py` file to an entity exactly like a `.luau` script
(File > New Python Script creates one). The file defines a class deriving
from `Script`; if several exist, the one named like the file is used.

```python
from lapis import *

class Spinner(Script):
    speed = 90.0
    def on_start(self):            print("hello from", self.entity.name)
    def on_update(self, dt):       self.entity.rotation = Vector3(0, time() * self.speed, 0)
    def on_collision(self, other): other.add_impulse(Vector3(0, 5, 0))
    def on_destroy(self):          pass
```

| `lapis` | |
|---|---|
| `Vector3(x, y, z)` | `+ - * /`, `length()`, `normalized()`, `dot()`, `cross()`, `Vector3.lerp(a, b, t)`, `Vector3.zero/one/up` |
| `Color.white/black/red/green/blue/yellow` | colors (Vector3) |
| `Entity` | `name`, `valid`, `position`, `rotation` (degrees), `scale`, `velocity`, `world_position`, `render_position`, `forward/right/up`, `add_impulse(v)`, `add_force(v)`, `set_color(c)`, `set_visible(b)`, `destroy()`, `script` |
| `find(name)` | entity or `None` |
| `spawn(name, mesh, position, physics, color, scale)` | `physics`: `""`, `"Dynamic"`, `"Kinematic"`, `"Static"` |
| `key_down(k)` / `key_pressed(k)` | key names as in Luau (`"W"`, `"Space"`, `"F1"`) |
| `mouse_delta()` / `mouse_position()` | tuples |
| `time()` | seconds since the scene started |
| `hud_text(x, y, text, color, scale)` / `hud_rect(x, y, w, h, color, alpha)` / `screen_size()` | 2D overlay |
| `raycast(origin, direction, max_distance, ignore)` | `RaycastHit(entity, position, normal, distance)` or `None` |

`print()` goes to the engine log. Errors show a full Python traceback in the
log / editor and never crash the game. Saving a `.py` file reloads it live.
Example project: `Projects/PythonDemo`.
