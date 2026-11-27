-- Spinning coin: collected when the player touches it (trigger collider).
local Coin = {}

function Coin:OnStart()
    self.base = self.Entity.Position
    self.phase = self.base.x * 0.7 + self.base.z * 0.3
end

function Coin:OnUpdate(dt: number)
    local t = Time.Now()
    self.Entity.Rotation = Vector3.new(0, t * 120, 0)
    self.Entity.Position = self.base + Vector3.new(0, math.sin(t * 2.5 + self.phase) * 0.15, 0)
end

function Coin:OnCollision(other)
    if other.Name ~= "Player" then return end
    local controller = other.Script
    if controller and controller.Collect then controller:Collect() end
    self.Entity:Destroy()
end

return Coin
