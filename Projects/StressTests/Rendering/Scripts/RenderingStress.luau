-- RENDERING STRESS TEST
-- Fills a growing grid with lit, shadow-casting objects (cubes, spheres and an
-- OBJ model) plus 16 moving point lights, while the camera orbits, until the
-- frame rate stays below MinFPS for 3 seconds or MaxObjects is reached.

-- ---------------------------------------------------------------- logging --
-- CSV + summary written with the sandboxed File library into the game's
-- Saved/ folder (next to the .exe for exported games).
local Log = {}
function Log.Start(test: string)
    Log.file = string.format("%s_%s.csv", test, os.date("%Y%m%d_%H%M%S"))
    Log.test = test
    File.Write(Log.file, "time,load,entities,bodies,fps,frame_ms,physics_ms,script_ms,render_ms,draw_calls,triangles,contacts\n")
    Log.samples = 0
end
function Log.Sample(load: number)
    local p = Perf.Get()
    File.Append(Log.file, string.format("%.2f,%d,%d,%d,%.1f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d\n",
        p.Time, load, p.Entities, p.Bodies, p.FPS, p.FrameMs, p.PhysicsMs, p.ScriptMs, p.RenderMs,
        p.DrawCalls, p.Triangles, p.Contacts))
    Log.samples += 1
    return p
end
function Log.Summary(lines: {string})
    local text = string.format("[%s] %s\n", os.date("%Y-%m-%d %H:%M:%S"), Log.test)
    for _, l in lines do text ..= "  " .. l .. "\n" end
    File.Append("summary.txt", text)
    print(text)
end

local function DrawStats(title: string, loadName: string, load: number, state: string)
    local p = Perf.Get()
    HUD.Rect(12, 12, 360, 178, Color.Black, 0.55)
    HUD.Text(24, 20, title, Color.Yellow, 1.3)
    HUD.Text(24, 50, string.format("%s: %d", loadName, load), Color.White)
    HUD.Text(24, 72, string.format("FPS: %.0f   frame %.1f ms", p.FPS, p.FrameMs), Color.White)
    HUD.Text(24, 94, string.format("physics %.1f ms   scripts %.1f ms   render %.1f ms", p.PhysicsMs, p.ScriptMs, p.RenderMs), Color.White, 0.9)
    HUD.Text(24, 116, string.format("draws %d   tris %d   contacts %d", p.DrawCalls, p.Triangles, p.Contacts), Color.White, 0.9)
    HUD.Text(24, 140, state, Color.Green)
    HUD.Text(24, 162, "Log: Saved/" .. (Log.file or "?"), Color.Gray, 0.85)
end

-- Stop condition shared by all tests: hold below MinFPS for 3 s, or reach the max.
local function UpdateLimit(self, dt: number, maxLoad: number)
    local fps = Perf.FPS()
    if Time.Now() > 3 and fps > 0 and fps < self.MinFPS then self.slowTime += dt else self.slowTime = 0 end
    if not self.limitReached and (self.slowTime > 3 or self.load >= maxLoad) then
        self.limitReached = true
        self.limitAt = Time.Now()
        self.limitLoad = self.load
    end
end

local Test = {}
Test.WaveSize = 400
Test.WaveInterval = 1.0
Test.MaxObjects = 30000
Test.MinFPS = 20
Test.SettleTime = 8
Test.AutoQuit = true

local meshes = { "Cube", "Sphere", "Models/Gem.obj" }

function Test:OnStart()
    Log.Start("stress_rendering")
    self.load = 0; self.timer = 0; self.sampleTimer = 0; self.slowTime = 0
    self.limitReached = false; self.done = false
    self.camera = Scene.GetCamera()
    self.lights = {}
    for i = 1, 16 do
        table.insert(self.lights, Scene.Spawn({ Name = "Light", Light = "Point",
            LightColor = Color.HSV(i / 16, 0.7, 1), Intensity = 30, Range = 14 }))
    end
    math.randomseed(99)
end

function Test:SpawnWave()
    for i = 1, self.WaveSize do
        local n = self.load + i
        -- golden-angle spiral layout so density grows evenly
        local r = math.sqrt(n) * 0.9
        local a = n * 2.39996
        Scene.Spawn({
            Name = "Prop",
            Mesh = meshes[(n % #meshes) + 1],
            Position = Vector3.new(math.cos(a) * r, 0.5 + (n % 7) * 0.35, math.sin(a) * r),
            Rotation = Vector3.new(0, n * 37 % 360, 0),
            Scale = 0.7,
            Color = Color.HSV((n * 0.618) % 1, 0.55, 0.95),
            Roughness = 0.2 + (n % 5) * 0.15,
            Metallic = if n % 4 == 0 then 1 else 0,
        })
    end
    self.load += self.WaveSize
end

function Test:OnUpdate(dt: number)
    local t = Time.Now()
    -- orbiting camera + lights
    local radius = 25 + math.sqrt(self.load) * 0.9
    self.camera.Position = Vector3.new(math.cos(t * 0.15) * radius, radius * 0.6, math.sin(t * 0.15) * radius)
    self.camera:LookAt(Vector3.new(0, 0, 0))
    for i, l in self.lights do
        local a = t * 0.6 + i * (math.pi * 2 / 16)
        local r = 6 + (i % 4) * (radius * 0.15)
        l.Position = Vector3.new(math.cos(a) * r, 2.5, math.sin(a) * r)
    end

    self.timer += dt
    self.sampleTimer += dt
    UpdateLimit(self, dt, self.MaxObjects)
    if not self.limitReached and self.timer >= self.WaveInterval then
        self.timer = 0
        self:SpawnWave()
    end
    if self.sampleTimer >= 0.5 then
        self.sampleTimer = 0
        Log.Sample(self.load)
    end
    if self.limitReached and not self.done and Time.Now() - self.limitAt > self.SettleTime then
        self.done = true
        local p = Perf.Get()
        Log.Summary({
            string.format("objects at limit: %d (FPS below %d for 3 s, or max %d)", self.limitLoad, self.MinFPS, self.MaxObjects),
            string.format("final: FPS %.1f, render %.2f ms, %d draw calls, %d triangles", p.FPS, p.RenderMs, p.DrawCalls, p.Triangles),
            "samples: " .. Log.samples .. " -> Saved/" .. Log.file,
        })
        if self.AutoQuit then App.Quit() end
    end
    local state = if self.done then "Finished - see Saved/summary.txt"
        elseif self.limitReached then string.format("Limit reached at %d objects", self.limitLoad)
        else "Adding objects..."
    DrawStats("Rendering stress test", "Objects", self.load, state)
    if Input.WasKeyPressed("Escape") then App.Quit() end
end

return Test
