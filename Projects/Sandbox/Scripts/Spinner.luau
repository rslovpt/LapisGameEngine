-- Rotates and gently bobs an object.
local Spinner = {}

Spinner.DegreesPerSecond = 45
Spinner.BobHeight = 0.25

function Spinner:OnStart()
    self.base = self.Entity.Position
    self.angle = 0
end

function Spinner:OnUpdate(dt: number)
    self.angle += self.DegreesPerSecond * dt
    self.Entity.Rotation = Vector3.new(0, self.angle, 0)
    self.Entity.Position = self.base + Vector3.new(0, math.sin(Time.Now() * 2) * self.BobHeight, 0)
end

return Spinner
