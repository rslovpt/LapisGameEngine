-- PHYSICS STRESS TEST
-- Drops waves of rigid bodies (boxes + spheres) into a walled arena until the
-- frame rate stays below MinFPS for 3 seconds or MaxBodies is reached, then
-- keeps simulating the pile for a while and writes a summary.

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
Test.WaveSize = 100
Test.WaveInterval = 1.5
Test.MaxBodies = 4000
Test.MinFPS = 20
Test.SettleTime = 10       -- seconds to keep running after the limit
Test.AutoQuit = true       -- quit when finished (exported game)

function Test:OnStart()
    Log.Start("stress_physics")
    self.load = 0; self.timer = 0; self.sampleTimer = 0; self.slowTime = 0
    self.limitReached = false; self.done = false
    math.randomseed(1234)
end

function Test:SpawnWave()
    for i = 1, self.WaveSize do
        local sphere = math.random() < 0.4
        local s = 0.6 + math.random() * 0.6
        Scene.Spawn({
            Name = "Body",
            Mesh = if sphere then "Sphere" else "Cube",
            Position = Vector3.new((math.random() - 0.5) * 26, 8 + math.random() * 10, (math.random() - 0.5) * 26),
            Scale = s,
            Color = Color.HSV(math.random(), 0.6, 0.95),
            Roughness = 0.5,
            Physics = "Dynamic",
            Restitution = 0.15,
            Friction = 0.6,
        })
    end
    self.load += self.WaveSize
end

function Test:OnUpdate(dt: number)
    self.timer += dt
    self.sampleTimer += dt
    UpdateLimit(self, dt, self.MaxBodies)
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
            string.format("bodies at limit: %d (limit = FPS below %d for 3 s, or max %d)", self.limitLoad, self.MinFPS, self.MaxBodies),
            string.format("after settling: FPS %.1f, physics %.2f ms, contacts %d", p.FPS, p.PhysicsMs, p.Contacts),
            "samples: " .. Log.samples .. " -> Saved/" .. Log.file,
        })
        if self.AutoQuit then App.Quit() end
    end
    local state = if self.done then "Finished - see Saved/summary.txt"
        elseif self.limitReached then string.format("Limit reached at %d bodies - settling...", self.limitLoad)
        else "Adding bodies..."
    DrawStats("Physics stress test", "Bodies", self.load, state)
    if Input.WasKeyPressed("Escape") then App.Quit() end
end

return Test
