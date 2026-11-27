#include "Lapis/Scripting/Python/PythonScripting.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

#include "Lapis/Assets/AssetManager.h"
#include "Lapis/Core/Log.h"
#include "Lapis/Physics/PhysicsWorld.h"
#include "Lapis/Platform/Input.h"
#include "Lapis/Scene/Entity.h"
#include "Lapis/Scene/Scene.h"
#include "Lapis/Scripting/ScriptEngine.h"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace Lapis::Python {

namespace {

// ---- the parts of the CPython C API we use, declared by hand -------------------
// (All exported by every CPython 3.x shared library; loaded at runtime.)
struct PyObject;
using PyCFunction = PyObject* (*)(PyObject*, PyObject*);
struct PyMethodDef {
    const char* Name;
    PyCFunction Function;
    int Flags;
    const char* Doc;
};
constexpr int METH_VARARGS = 0x0001;
constexpr int Py_file_input = 257;

struct API {
    void (*InitializeEx)(int) = nullptr;
    int (*IsInitialized)() = nullptr;
    const char* (*GetVersion)() = nullptr;
    PyObject* (*RunStringFlags)(const char*, int, PyObject*, PyObject*, void*) = nullptr;
    PyObject* (*DictNew)() = nullptr;
    int (*DictSetItemString)(PyObject*, const char*, PyObject*) = nullptr;
    PyObject* (*DictGetItemString)(PyObject*, const char*) = nullptr;
    PyObject* (*EvalGetBuiltins)() = nullptr;
    PyObject* (*ImportAddModule)(const char*) = nullptr;
    PyObject* (*ModuleGetDict)(PyObject*) = nullptr;
    PyObject* (*CFunctionNewEx)(PyMethodDef*, PyObject*, PyObject*) = nullptr;
    PyObject* (*CallObject)(PyObject*, PyObject*) = nullptr;
    PyObject* (*BuildValue)(const char*, ...) = nullptr;
    int (*ParseTuple)(PyObject*, const char*, ...) = nullptr;
    const char* (*UnicodeAsUTF8)(PyObject*) = nullptr;
    void (*IncRef)(PyObject*) = nullptr;
    void (*DecRef)(PyObject*) = nullptr;
    PyObject* (*ErrOccurred)() = nullptr;
    void (*ErrPrint)() = nullptr;
    void (*ErrClear)() = nullptr;
};

API g_Py;
bool g_Tried = false, g_Available = false;
std::string g_Status = "Python not initialized yet";
PyObject* g_Lapis = nullptr;      // dict of the `lapis` module (borrowed, lives forever)
Scene* g_Scene = nullptr;         // scene whose script is currently running

// ---- library loading -------------------------------------------------------------------
void* OpenLibrary(const std::string& path) {
#if defined(_WIN32)
    return (void*)LoadLibraryW(std::filesystem::path(path).wstring().c_str());
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
#endif
}
void* Symbol(void* lib, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress((HMODULE)lib, name));
#else
    return dlsym(lib, name);
#endif
}

std::vector<std::string> Candidates() {
    std::vector<std::string> out;
    if (const char* env = std::getenv("LAPIS_PYTHON")) out.push_back(env);
#if defined(_WIN32)
    // Engine-local install (future in-editor installer), then standard installs, then PATH.
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(exe).parent_path();
    std::string localApp;
    if (const char* la = std::getenv("LOCALAPPDATA")) localApp = la;
    for (int minor = 14; minor >= 8; --minor) {
        const std::string dll = "python3" + std::to_string(minor) + ".dll";
        const std::string folder = "Python3" + std::to_string(minor);
        out.push_back((exeDir / "Python" / dll).string());
        if (!localApp.empty()) out.push_back(localApp + "\\Programs\\Python\\" + folder + "\\" + dll);
        out.push_back("C:\\Program Files\\" + folder + "\\" + dll);
        out.push_back("C:\\" + folder + "\\" + dll);
        out.push_back(dll);   // PATH / system directories
    }
#elif defined(__APPLE__)
    for (int minor = 14; minor >= 8; --minor) {
        const std::string v = "3." + std::to_string(minor);
        out.push_back("/Library/Frameworks/Python.framework/Versions/" + v + "/Python");
        out.push_back("/opt/homebrew/lib/libpython" + v + ".dylib");
        out.push_back("/usr/local/lib/libpython" + v + ".dylib");
    }
#else
    for (int minor = 14; minor >= 8; --minor) {
        out.push_back("libpython3." + std::to_string(minor) + ".so.1.0");
        out.push_back("libpython3." + std::to_string(minor) + ".so");
    }
#endif
    return out;
}

template <typename F> bool Load(void* lib, F& fn, const char* name) {
    fn = reinterpret_cast<F>(Symbol(lib, name));
    return fn != nullptr;
}

// ---- C functions exposed to Python (as lapis._xxx) --------------------------------------
u64 Pack(EntityID id) { return (u64(id.Generation) << 32) | id.Index; }
EntityID Unpack(unsigned long long v) { EntityID id; id.Index = (u32)(v & 0xFFFFFFFFu); id.Generation = (u32)(v >> 32); return id; }

PyObject* None() { return g_Py.BuildValue(""); }

Entity GetEntity(PyObject* args, int* extraOk = nullptr) {
    LAPIS_UNUSED(extraOk);
    unsigned long long raw = 0;
    if (!g_Scene || !g_Py.ParseTuple(args, "K", &raw)) return {};
    return g_Scene->GetEntity(Unpack(raw));
}

PyObject* C_Find(PyObject*, PyObject* args) {
    const char* name = nullptr;
    if (!g_Py.ParseTuple(args, "s", &name) || !g_Scene) return nullptr;
    Entity e = g_Scene->FindEntityByName(name);
    return e ? g_Py.BuildValue("K", (unsigned long long)Pack(e.GetID())) : None();
}

PyObject* C_Valid(PyObject*, PyObject* args) {
    unsigned long long raw = 0;
    if (!g_Py.ParseTuple(args, "K", &raw)) return nullptr;
    return g_Py.BuildValue("i", g_Scene && g_Scene->GetEntity(Unpack(raw)) ? 1 : 0);
}

PyObject* C_Name(PyObject*, PyObject* args) {
    Entity e = GetEntity(args);
    return g_Py.BuildValue("s", e ? e.GetName().c_str() : "");
}

// Transform / physics vectors: _get(id, what) / _set(id, what, x, y, z)
PyObject* C_Get(PyObject*, PyObject* args) {
    unsigned long long raw = 0;
    const char* what = nullptr;
    if (!g_Scene || !g_Py.ParseTuple(args, "Ks", &raw, &what)) return nullptr;
    Entity e = g_Scene->GetEntity(Unpack(raw));
    if (!e) return None();
    auto& t = e.Transform();
    Vec3 v(0.0f);
    const std::string w = what;
    if (w == "position") v = t.Position;
    else if (w == "rotation") v = t.GetEulerDegrees();
    else if (w == "scale") v = t.Scale;
    else if (w == "world_position") v = t.WorldPosition();
    else if (w == "render_position") v = g_Scene->GetRenderPosition(e.GetID());
    else if (w == "forward") v = t.Forward();
    else if (w == "right") v = t.Right();
    else if (w == "up") v = t.Up();
    else if (w == "velocity") { if (auto* rb = e.TryGet<RigidBodyComponent>()) v = rb->Velocity; }
    return g_Py.BuildValue("(ddd)", (double)v.x, (double)v.y, (double)v.z);
}

PyObject* C_Set(PyObject*, PyObject* args) {
    unsigned long long raw = 0;
    const char* what = nullptr;
    double x = 0, y = 0, z = 0;
    if (!g_Scene || !g_Py.ParseTuple(args, "Ksddd", &raw, &what, &x, &y, &z)) return nullptr;
    Entity e = g_Scene->GetEntity(Unpack(raw));
    if (!e) return None();
    auto& t = e.Transform();
    const Vec3 v((float)x, (float)y, (float)z);
    const std::string w = what;
    auto* rb = e.TryGet<RigidBodyComponent>();
    if (w == "position") { t.Teleport(v); if (rb) rb->Sleeping = false; }
    else if (w == "rotation") t.SetEulerDegrees(v);
    else if (w == "scale") t.Scale = v;
    else if (w == "velocity") { if (rb) { rb->Velocity = v; rb->Sleeping = false; } }
    else if (w == "impulse") { if (rb) rb->AddImpulse(v); }
    else if (w == "force") { if (rb) rb->AddForce(v); }
    else if (w == "color") { if (auto* mr = e.TryGet<MeshRendererComponent>()) mr->Tint = {v.x, v.y, v.z, mr->Tint.w}; }
    return None();
}

PyObject* C_SetVisible(PyObject*, PyObject* args) {
    unsigned long long raw = 0;
    int visible = 1;
    if (!g_Scene || !g_Py.ParseTuple(args, "Ki", &raw, &visible)) return nullptr;
    if (Entity e = g_Scene->GetEntity(Unpack(raw)))
        if (auto* mr = e.TryGet<MeshRendererComponent>()) mr->Visible = visible != 0;
    return None();
}

PyObject* C_Destroy(PyObject*, PyObject* args) {
    Entity e = GetEntity(args);
    if (e) g_Scene->DestroyEntity(e);
    return None();
}

// _spawn(name, mesh, x, y, z, physics, r, g, b, sx, sy, sz)
PyObject* C_Spawn(PyObject*, PyObject* args) {
    const char *name = nullptr, *mesh = nullptr, *physics = nullptr;
    double x, y, z, r, g, b, sx, sy, sz;
    if (!g_Scene || !g_Py.ParseTuple(args, "sssddddddddd", &name, &mesh, &physics, &x, &y, &z, &r, &g, &b, &sx, &sy, &sz)) return nullptr;
    Scene& scene = *g_Scene;
    Entity e = scene.CreateEntity(name);
    e.Transform().Position = {(float)x, (float)y, (float)z};
    e.Transform().PrevPosition = e.Transform().Position;
    e.Transform().Scale = {(float)sx, (float)sy, (float)sz};
    const std::string meshName = mesh;
    if (!meshName.empty()) {
        auto& mr = e.Add<MeshRendererComponent>();
        mr.MeshName = meshName;
        mr.MeshAsset = scene.GetAssets().GetMesh(meshName);
        mr.MaterialAsset = scene.GetAssets().GetSharedMaterial("", 0.5f, 0.0f, Vec3(0.0f), {1, 1}, false, true);
        mr.Tint = {(float)r, (float)g, (float)b, 1.0f};
    }
    const std::string phys = physics;
    if (!phys.empty()) {
        auto& c = e.Add<ColliderComponent>();
        c.Shape = meshName == "Sphere" ? ColliderShape::Sphere : ColliderShape::Box;
        auto& rb = e.Add<RigidBodyComponent>();
        rb.Type = phys == "Static" ? BodyType::Static : phys == "Kinematic" ? BodyType::Kinematic : BodyType::Dynamic;
    }
    return g_Py.BuildValue("K", (unsigned long long)Pack(e.GetID()));
}

PyObject* C_KeyDown(PyObject*, PyObject* args) {
    const char* name = nullptr;
    if (!g_Py.ParseTuple(args, "s", &name)) return nullptr;
    const int k = ScriptKeyFromName(name);
    return g_Py.BuildValue("i", k >= 0 && Input::IsKeyDown(static_cast<Key>(k)) ? 1 : 0);
}
PyObject* C_KeyPressed(PyObject*, PyObject* args) {
    const char* name = nullptr;
    if (!g_Py.ParseTuple(args, "s", &name)) return nullptr;
    const int k = ScriptKeyFromName(name);
    return g_Py.BuildValue("i", k >= 0 && Input::WasKeyPressed(static_cast<Key>(k)) ? 1 : 0);
}
PyObject* C_Mouse(PyObject*, PyObject*) {
    const Vec2 d = Input::GetMouseDelta(), p = Input::GetMousePosition();
    return g_Py.BuildValue("(dddd)", (double)d.x, (double)d.y, (double)p.x, (double)p.y);
}
PyObject* C_Time(PyObject*, PyObject*) { return g_Py.BuildValue("d", g_Scene ? (double)g_Scene->Time : 0.0); }

PyObject* C_Log(PyObject*, PyObject* args) {
    const char* text = nullptr;
    int level = 0;
    if (!g_Py.ParseTuple(args, "si", &text, &level)) return nullptr;
    if (level >= 2) LAPIS_ERROR("[Python] {}", text);
    else if (level == 1) LAPIS_WARN("[Python] {}", text);
    else LAPIS_INFO("[Python] {}", text);
    return None();
}

// _hud_text(x, y, text, r, g, b, scale) / _hud_rect(x, y, w, h, r, g, b, a)
PyObject* C_HudText(PyObject*, PyObject* args) {
    double x, y, r, g, b, scale;
    const char* text = nullptr;
    if (!g_Scene || !g_Py.ParseTuple(args, "ddsdddd", &x, &y, &text, &r, &g, &b, &scale)) return nullptr;
    g_Scene->HudText((float)x, (float)y, text, {(float)r, (float)g, (float)b, 1.0f}, (float)scale);
    return None();
}
PyObject* C_HudRect(PyObject*, PyObject* args) {
    double x, y, w, h, r, g, b, a;
    if (!g_Scene || !g_Py.ParseTuple(args, "dddddddd", &x, &y, &w, &h, &r, &g, &b, &a)) return nullptr;
    g_Scene->HudRect((float)x, (float)y, (float)w, (float)h, {(float)r, (float)g, (float)b, (float)a});
    return None();
}
PyObject* C_ScreenSize(PyObject*, PyObject*) {
    const Vec2 s = g_Scene ? g_Scene->ViewportSize : Vec2(0.0f);
    return g_Py.BuildValue("(dd)", (double)s.x, (double)s.y);
}

// _raycast(ox, oy, oz, dx, dy, dz, max_distance, ignore_id) -> (id, x, y, z, nx, ny, nz, distance) or None
PyObject* C_Raycast(PyObject*, PyObject* args) {
    double ox, oy, oz, dx, dy, dz, maxDist;
    unsigned long long ignore = 0;
    if (!g_Scene || !g_Py.ParseTuple(args, "dddddddK", &ox, &oy, &oz, &dx, &dy, &dz, &maxDist, &ignore)) return nullptr;
    const Ray ray{Vec3((float)ox, (float)oy, (float)oz), Normalize(Vec3((float)dx, (float)dy, (float)dz))};
    auto hit = g_Scene->GetPhysics().Raycast(g_Scene->GetRegistry(), ray, (float)maxDist, ignore ? Unpack(ignore) : EntityID{});
    if (!hit) return None();
    return g_Py.BuildValue("(Kddddddd)", (unsigned long long)Pack(hit->Entity), (double)hit->Point.x, (double)hit->Point.y,
                           (double)hit->Point.z, (double)hit->Normal.x, (double)hit->Normal.y, (double)hit->Normal.z, (double)hit->Distance);
}

PyMethodDef g_Methods[] = {
    {"_find", C_Find, METH_VARARGS, nullptr},       {"_valid", C_Valid, METH_VARARGS, nullptr},
    {"_name", C_Name, METH_VARARGS, nullptr},       {"_get", C_Get, METH_VARARGS, nullptr},
    {"_set", C_Set, METH_VARARGS, nullptr},         {"_set_visible", C_SetVisible, METH_VARARGS, nullptr},
    {"_destroy", C_Destroy, METH_VARARGS, nullptr}, {"_spawn", C_Spawn, METH_VARARGS, nullptr},
    {"_key_down", C_KeyDown, METH_VARARGS, nullptr}, {"_key_pressed", C_KeyPressed, METH_VARARGS, nullptr},
    {"_mouse", C_Mouse, METH_VARARGS, nullptr},     {"_time", C_Time, METH_VARARGS, nullptr},
    {"_log", C_Log, METH_VARARGS, nullptr},         {"_hud_text", C_HudText, METH_VARARGS, nullptr},
    {"_hud_rect", C_HudRect, METH_VARARGS, nullptr}, {"_screen_size", C_ScreenSize, METH_VARARGS, nullptr},
    {"_raycast", C_Raycast, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// The pure-Python half of the `lapis` module. Every call from C++ into user
// code goes through _load/_call, which catch exceptions and return the
// traceback text, so the C++ side never has to inspect Python exceptions.
const char* Prelude = R"PY(
import sys, math, traceback

class _LogStream:
    def __init__(self, level): self.level = level; self.buf = ""
    def write(self, s):
        self.buf += s
        while "\n" in self.buf:
            line, self.buf = self.buf.split("\n", 1)
            if line.strip(): _log(line, self.level)
    def flush(self): pass
sys.stdout = _LogStream(0)
sys.stderr = _LogStream(2)

class Vector3:
    __slots__ = ("x", "y", "z")
    def __init__(self, x=0.0, y=0.0, z=0.0): self.x = float(x); self.y = float(y); self.z = float(z)
    def __add__(self, o): return Vector3(self.x + o.x, self.y + o.y, self.z + o.z)
    def __sub__(self, o): return Vector3(self.x - o.x, self.y - o.y, self.z - o.z)
    def __mul__(self, s):
        if isinstance(s, Vector3): return Vector3(self.x * s.x, self.y * s.y, self.z * s.z)
        return Vector3(self.x * s, self.y * s, self.z * s)
    __rmul__ = __mul__
    def __truediv__(self, s): return Vector3(self.x / s, self.y / s, self.z / s)
    def __neg__(self): return Vector3(-self.x, -self.y, -self.z)
    def __eq__(self, o): return isinstance(o, Vector3) and self.x == o.x and self.y == o.y and self.z == o.z
    def __iter__(self): yield self.x; yield self.y; yield self.z
    def __getitem__(self, i): return (self.x, self.y, self.z)[i]
    def __len__(self): return 3
    def __repr__(self): return "Vector3(%g, %g, %g)" % (self.x, self.y, self.z)
    def length(self): return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)
    def normalized(self):
        l = self.length()
        return self / l if l > 1e-9 else Vector3()
    def dot(self, o): return self.x * o.x + self.y * o.y + self.z * o.z
    def cross(self, o): return Vector3(self.y * o.z - self.z * o.y, self.z * o.x - self.x * o.z, self.x * o.y - self.y * o.x)
    @staticmethod
    def lerp(a, b, t): return a + (b - a) * t
Vector3.zero = Vector3(); Vector3.one = Vector3(1, 1, 1); Vector3.up = Vector3(0, 1, 0)

class Color:
    white = Vector3(1, 1, 1); black = Vector3(0, 0, 0); red = Vector3(1, 0.2, 0.2)
    green = Vector3(0.2, 1, 0.3); blue = Vector3(0.25, 0.45, 1); yellow = Vector3(1, 0.85, 0.2)

def _vec(t): return Vector3(t[0], t[1], t[2]) if t is not None else Vector3()

class Entity:
    __slots__ = ("_id",)
    def __init__(self, id): self._id = id
    def __eq__(self, o): return isinstance(o, Entity) and o._id == self._id
    def __hash__(self): return hash(self._id)
    def __repr__(self): return "Entity(%r)" % self.name
    @property
    def valid(self): return bool(_valid(self._id))
    @property
    def name(self): return _name(self._id)
    def _vprop(what):
        return property(lambda self: _vec(_get(self._id, what)),
                        lambda self, v: _set(self._id, what, float(v[0]), float(v[1]), float(v[2])))
    position = _vprop("position"); rotation = _vprop("rotation"); scale = _vprop("scale")
    velocity = _vprop("velocity")
    world_position = property(lambda self: _vec(_get(self._id, "world_position")))
    render_position = property(lambda self: _vec(_get(self._id, "render_position")))
    forward = property(lambda self: _vec(_get(self._id, "forward")))
    right = property(lambda self: _vec(_get(self._id, "right")))
    up = property(lambda self: _vec(_get(self._id, "up")))
    def add_impulse(self, v): _set(self._id, "impulse", float(v[0]), float(v[1]), float(v[2]))
    def add_force(self, v): _set(self._id, "force", float(v[0]), float(v[1]), float(v[2]))
    def set_color(self, c): _set(self._id, "color", float(c[0]), float(c[1]), float(c[2]))
    def set_visible(self, visible): _set_visible(self._id, 1 if visible else 0)
    def destroy(self): _destroy(self._id)
    @property
    def script(self): return _instances.get(self._id)

class Script:
    """Base class for entity scripts. Override on_start / on_update(dt) / on_collision(other) / on_destroy."""
    entity = None
    def on_start(self): pass
    def on_update(self, dt): pass
    def on_collision(self, other): pass
    def on_destroy(self): pass

def find(name):
    id = _find(name)
    return Entity(id) if id is not None else None

def spawn(name="Entity", mesh="Cube", position=(0, 0, 0), physics="", color=(0.8, 0.8, 0.8), scale=(1, 1, 1)):
    return Entity(_spawn(name, mesh or "", physics or "", float(position[0]), float(position[1]), float(position[2]),
                         float(color[0]), float(color[1]), float(color[2]), float(scale[0]), float(scale[1]), float(scale[2])))

def key_down(key): return bool(_key_down(key))
def key_pressed(key): return bool(_key_pressed(key))
def mouse_delta():
    m = _mouse(); return (m[0], m[1])
def mouse_position():
    m = _mouse(); return (m[2], m[3])
def time(): return _time()
def hud_text(x, y, text, color=(1, 1, 1), scale=1.0):
    _hud_text(float(x), float(y), str(text), float(color[0]), float(color[1]), float(color[2]), float(scale))
def hud_rect(x, y, w, h, color=(0, 0, 0), alpha=1.0):
    _hud_rect(float(x), float(y), float(w), float(h), float(color[0]), float(color[1]), float(color[2]), float(alpha))
def screen_size(): return _screen_size()

class RaycastHit:
    def __init__(self, t):
        self.entity = Entity(t[0]); self.position = Vector3(t[1], t[2], t[3])
        self.normal = Vector3(t[4], t[5], t[6]); self.distance = t[7]
def raycast(origin, direction, max_distance=1000.0, ignore=None):
    t = _raycast(float(origin[0]), float(origin[1]), float(origin[2]), float(direction[0]), float(direction[1]),
                 float(direction[2]), float(max_distance), ignore._id if ignore is not None else 0)
    return RaycastHit(t) if t is not None else None

__all__ = ["Vector3", "Color", "Entity", "Script", "find", "spawn", "key_down", "key_pressed", "mouse_delta",
           "mouse_position", "time", "hud_text", "hud_rect", "screen_size", "raycast", "RaycastHit", "math"]

_instances = {}

def _load(source, path, entity_id):
    try:
        g = {"__name__": path.replace("/", ".").rsplit(".", 1)[0], "__file__": path}
        exec(compile(source, path, "exec"), g)
        classes = [v for v in g.values() if isinstance(v, type) and issubclass(v, Script) and v is not Script]
        if not classes:
            return (0, path + ": no class deriving from lapis.Script was found")
        stem = path.replace("\\", "/").rsplit("/", 1)[-1].rsplit(".", 1)[0]
        cls = next((c for c in classes if c.__name__ == stem), classes[-1])
        obj = cls()
        obj.entity = Entity(entity_id)
        _instances[entity_id] = obj
        return (1, obj)
    except BaseException:
        return (0, traceback.format_exc())

def _call(obj, method, *args):
    try:
        args = tuple(Entity(a) if isinstance(a, int) and method == "on_collision" else a for a in args)
        getattr(obj, method)(*args)
        return (1, None)
    except BaseException:
        return (0, traceback.format_exc())

def _forget(entity_id): _instances.pop(entity_id, None)

def _run(code):
    try:
        exec(compile(code, "<lapis>", "exec"), globals())
        return (1, None)
    except BaseException:
        return (0, traceback.format_exc())
)PY";

PyObject* LapisFunction(const char* name) { return g_Lapis ? g_Py.DictGetItemString(g_Lapis, name) : nullptr; }

// Calls lapis.<helper>(args...) which returns (ok, value). Steals `args`.
bool CallHelper(const char* helper, PyObject* args, PyObject** value, std::string* error) {
    if (value) *value = nullptr;
    PyObject* fn = LapisFunction(helper);
    if (!fn || !args) { if (error) *error = std::string("internal: lapis.") + helper + " missing"; if (args) g_Py.DecRef(args); return false; }
    PyObject* result = g_Py.CallObject(fn, args);
    g_Py.DecRef(args);
    if (!result) {
        if (g_Py.ErrOccurred()) g_Py.ErrPrint();
        if (error) *error = std::string("internal error calling lapis.") + helper;
        return false;
    }
    int ok = 0;
    PyObject* v = nullptr;
    if (!g_Py.ParseTuple(result, "iO", &ok, &v)) { g_Py.ErrClear(); g_Py.DecRef(result); if (error) *error = "bad helper result"; return false; }
    if (ok) {
        if (value) { g_Py.IncRef(v); *value = v; }
    } else if (error) {
        const char* text = g_Py.UnicodeAsUTF8(v);
        *error = text ? text : "unknown Python error";
    }
    g_Py.DecRef(result);
    return ok != 0;
}

} // namespace

// =============================================================================
bool IsPythonScript(const std::string& path) {
    return path.size() > 3 && path.compare(path.size() - 3, 3, ".py") == 0;
}

std::string SearchDescription() {
#if defined(_WIN32)
    return "LAPIS_PYTHON, <engine>\\bin\\Python, %LOCALAPPDATA%\\Programs\\Python\\Python3xx, C:\\Program Files\\Python3xx, C:\\Python3xx, PATH";
#else
    return "LAPIS_PYTHON, then libpython3.x from the system library path";
#endif
}

bool Initialize() {
    if (g_Tried) return g_Available;
    g_Tried = true;
    void* lib = nullptr;
    std::string libPath;
    for (const std::string& c : Candidates()) {
        if ((lib = OpenLibrary(c))) { libPath = c; break; }
    }
    if (!lib) {
        g_Status = "Python not found (install 64-bit Python 3.8+ from python.org; searched: " + SearchDescription() + ")";
        LAPIS_WARN("{}", g_Status);
        return false;
    }
    bool ok = Load(lib, g_Py.InitializeEx, "Py_InitializeEx") && Load(lib, g_Py.IsInitialized, "Py_IsInitialized") &&
              Load(lib, g_Py.GetVersion, "Py_GetVersion") && Load(lib, g_Py.RunStringFlags, "PyRun_StringFlags") &&
              Load(lib, g_Py.DictNew, "PyDict_New") && Load(lib, g_Py.DictSetItemString, "PyDict_SetItemString") &&
              Load(lib, g_Py.DictGetItemString, "PyDict_GetItemString") && Load(lib, g_Py.EvalGetBuiltins, "PyEval_GetBuiltins") &&
              Load(lib, g_Py.ImportAddModule, "PyImport_AddModule") && Load(lib, g_Py.ModuleGetDict, "PyModule_GetDict") &&
              Load(lib, g_Py.CFunctionNewEx, "PyCFunction_NewEx") && Load(lib, g_Py.CallObject, "PyObject_CallObject") &&
              Load(lib, g_Py.BuildValue, "Py_BuildValue") && Load(lib, g_Py.ParseTuple, "PyArg_ParseTuple") &&
              Load(lib, g_Py.UnicodeAsUTF8, "PyUnicode_AsUTF8") && Load(lib, g_Py.IncRef, "Py_IncRef") &&
              Load(lib, g_Py.DecRef, "Py_DecRef") && Load(lib, g_Py.ErrOccurred, "PyErr_Occurred") &&
              Load(lib, g_Py.ErrPrint, "PyErr_Print") && Load(lib, g_Py.ErrClear, "PyErr_Clear");
    if (!ok) {
        g_Status = "Python library " + libPath + " is missing required functions (needs CPython 3.8+)";
        LAPIS_WARN("{}", g_Status);
        return false;
    }
    if (!g_Py.IsInitialized()) g_Py.InitializeEx(0);   // 0: leave signal handling to the engine

    // Build the `lapis` module: C functions first, then the Python prelude.
    PyObject* module = g_Py.ImportAddModule("lapis");
    g_Lapis = module ? g_Py.ModuleGetDict(module) : nullptr;
    if (!g_Lapis) { g_Status = "Python started but the lapis module could not be created"; return false; }
    g_Py.DictSetItemString(g_Lapis, "__builtins__", g_Py.EvalGetBuiltins());
    for (PyMethodDef* m = g_Methods; m->Name; ++m) {
        PyObject* fn = g_Py.CFunctionNewEx(m, nullptr, nullptr);
        if (fn) { g_Py.DictSetItemString(g_Lapis, m->Name, fn); g_Py.DecRef(fn); }
    }
    PyObject* r = g_Py.RunStringFlags(Prelude, Py_file_input, g_Lapis, g_Lapis, nullptr);
    if (!r) {
        g_Py.ErrPrint();
        g_Status = "Python started but the lapis prelude failed (see log)";
        LAPIS_ERROR("{}", g_Status);
        return false;
    }
    g_Py.DecRef(r);

    std::string version = g_Py.GetVersion();
    version = version.substr(0, version.find(' '));
    g_Status = "CPython " + version + " (" + libPath + ")";
    LAPIS_INFO("Python: {}", g_Status);
    g_Available = true;
    return true;
}

bool IsAvailable() { return g_Available; }
const std::string& Status() { return g_Status; }

bool RunString(const std::string& code, std::string* error) {
    if (!Initialize()) { if (error) *error = g_Status; return false; }
    return CallHelper("_run", g_Py.BuildValue("(s)", code.c_str()), nullptr, error);
}

// =============================================================================
// Host
// =============================================================================
Host::~Host() { DestroyAll(); }

void Host::Report(const std::string& message) {
    // Python errors are reported through the scene's script error list too,
    // so the editor, player (--fail-on-script-error) and diagnostics see them.
    m_Scene.GetScripts().ReportError("[Python] " + message);
}

bool Host::Call(void* object, const char* method, EntityID arg, bool withArg, float dt) {
    Scene* previous = g_Scene;
    g_Scene = &m_Scene;
    PyObject* args = nullptr;
    if (dt >= 0.0f) args = g_Py.BuildValue("(Osd)", (PyObject*)object, method, (double)dt);
    else if (withArg) args = g_Py.BuildValue("(OsK)", (PyObject*)object, method, (unsigned long long)Pack(arg));
    else args = g_Py.BuildValue("(Os)", (PyObject*)object, method);
    std::string error;
    const bool ok = CallHelper("_call", args, nullptr, &error);
    g_Scene = previous;
    if (!ok) Report(error);
    return ok;
}

void Host::StartEntity(EntityID id) {
    auto* sc = m_Scene.GetRegistry().TryGet<LuauScriptComponent>(id);
    if (!sc || !IsPythonScript(sc->ScriptPath) || m_Instances.count(Pack(id))) return;
    if (!Initialize()) { Report(sc->ScriptPath + ": " + Status()); sc->Failed = true; return; }
    auto source = m_Scene.GetAssets().ReadText(sc->ScriptPath);
    if (!source) { Report("cannot read script '" + sc->ScriptPath + "'"); sc->Failed = true; return; }

    Scene* previous = g_Scene;
    g_Scene = &m_Scene;
    PyObject* instance = nullptr;
    std::string error;
    const bool ok = CallHelper("_load", g_Py.BuildValue("(ssK)", source->c_str(), sc->ScriptPath.c_str(), (unsigned long long)Pack(id)),
                               &instance, &error);
    g_Scene = previous;
    if (!ok) { Report(error); sc->Failed = true; return; }

    Instance inst;
    inst.Object = instance;
    inst.Path = sc->ScriptPath;
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(m_Scene.GetAssets().Resolve(sc->ScriptPath), ec);
    inst.Stamp = ec ? 0 : (long long)t.time_since_epoch().count();
    m_Instances[Pack(id)] = inst;
    sc->Started = true;
    Call(instance, "on_start");
}

void Host::UpdateEntities(float dt) {
    if (m_Instances.empty()) return;

    // Hot reload: changed .py files are reloaded (new instance, on_start again).
    m_ReloadTimer += dt;
    if (m_ReloadTimer > 0.5f) {
        m_ReloadTimer = 0.0f;
        std::vector<u64> changed;
        for (auto& [key, inst] : m_Instances) {
            std::error_code ec;
            const auto t = std::filesystem::last_write_time(m_Scene.GetAssets().Resolve(inst.Path), ec);
            if (!ec && (long long)t.time_since_epoch().count() != inst.Stamp) changed.push_back(key);
        }
        for (u64 key : changed) {
            const EntityID id = Unpack(key);
            LAPIS_INFO("Python: reloading {}", m_Instances[key].Path);
            DestroyEntity(id);
            if (auto* sc = m_Scene.GetRegistry().TryGet<LuauScriptComponent>(id)) { sc->Started = false; sc->Failed = false; }
            StartEntity(id);
        }
    }

    // Copy: scripts may spawn or destroy entities while we iterate.
    std::vector<std::pair<u64, void*>> live;
    live.reserve(m_Instances.size());
    for (auto& [key, inst] : m_Instances) live.push_back({key, inst.Object});
    for (auto& [key, object] : live)
        if (m_Instances.count(key) && m_Scene.GetEntity(Unpack(key))) Call(object, "on_update", {}, false, dt);
}

void Host::DispatchCollision(EntityID self, EntityID other) {
    auto it = m_Instances.find(Pack(self));
    if (it != m_Instances.end()) Call(it->second.Object, "on_collision", other, true);
}

void Host::DestroyEntity(EntityID id) {
    auto it = m_Instances.find(Pack(id));
    if (it == m_Instances.end()) return;
    void* object = it->second.Object;
    m_Instances.erase(it);
    Call(object, "on_destroy");
    CallHelper("_forget", g_Py.BuildValue("(K)", (unsigned long long)Pack(id)), nullptr, nullptr);
    g_Py.DecRef((PyObject*)object);
}

void Host::DestroyAll() {
    std::vector<u64> keys;
    for (auto& [key, inst] : m_Instances) keys.push_back(key);
    for (u64 key : keys) DestroyEntity(Unpack(key));
}

} // namespace Lapis::Python
