-- Moves an entity in a circle around the world origin.
local Orbit = {}

Orbit.Radius = 7
Orbit.Speed = 0.6

function Orbit:OnUpdate(dt: number)
    local t = Time.Now() * self.Speed
    self.Entity.Position = Vector3.new(math.cos(t) * self.Radius, 1.5, math.sin(t) * self.Radius)
end

return Orbit
