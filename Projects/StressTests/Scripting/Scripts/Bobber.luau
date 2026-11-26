-- Tiny per-entity script: bob up and down and spin (runs every frame on
-- thousands of entities).
local Bobber = {}

function Bobber:OnStart()
    self.base = self.Entity.Position
    self.phase = self.base.x * 0.3 + self.base.z * 0.2
end

function Bobber:OnUpdate(dt: number)
    local t = Time.Now() + self.phase
    self.Entity.Position = self.base + Vector3.new(0, math.sin(t * 2) * 0.5, 0)
    self.Entity.Rotation = Vector3.new(0, t * 90, 0)
end

return Bobber
