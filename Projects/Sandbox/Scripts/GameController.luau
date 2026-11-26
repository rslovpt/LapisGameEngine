-- Gameplay input for the sandbox: throwing balls, dropping crates, reset.
local Game = {}

function Game:OnStart()
    self.spawned = {}
    self.thrown = 0
end

local function Track(self, e)
    table.insert(self.spawned, e)
end

function Game:OnUpdate(dt: number)
    local cam = Scene.GetCamera()
    if cam == nil then return end

    if Input.WasMousePressed("Left") or Input.WasKeyPressed("Space") then
        self.thrown += 1
        local dir = cam.Forward
        Track(self, Scene.Spawn({
            Name = "Ball",
            Mesh = "Sphere",
            Position = cam.Position + dir * 1.5,
            Scale = 0.6,
            Color = Color.HSV(math.random(), 0.75, 1),
            Roughness = 0.3,
            Physics = "Dynamic",
            Mass = 2,
            Restitution = 0.55,
            Velocity = dir * 22,
            Script = "Scripts/Bouncer.luau",
        }))
        print("Threw ball #" .. self.thrown)
    end

    if Input.WasKeyPressed("B") then
        local hit = Physics.Raycast(cam.Position, cam.Forward, 200)
        local at = if hit then hit.Position + Vector3.new(0, 6, 0) else cam.Position + cam.Forward * 8
        Track(self, Scene.Spawn({
            Name = "DroppedCrate",
            Mesh = "Cube",
            Texture = "Textures/Crate.tga",
            Position = at,
            Physics = "Dynamic",
            Restitution = 0.1,
        }))
        print("Dropped a crate")
    end

    if Input.WasKeyPressed("R") then
        for _, e in self.spawned do
            if e.IsValid then e:Destroy() end
        end
        self.spawned = {}
        print("Reset: removed everything you spawned")
    end

    if Input.WasKeyPressed("Escape") then
        print("Escape pressed - quitting")
        App.Quit()
    end
end

return Game
