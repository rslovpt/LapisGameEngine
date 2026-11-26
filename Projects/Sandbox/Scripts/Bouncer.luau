-- Thrown balls: flash on every hit and clean themselves up after a while.
local Bouncer = {}

function Bouncer:OnStart()
    self.age = 0
    self.hits = 0
end

function Bouncer:OnCollision(other)
    self.hits += 1
    self.Entity:SetEmissive(Vector3.new(0.6, 0.6, 0.6) / self.hits)
end

function Bouncer:OnUpdate(dt: number)
    self.age += dt
    if self.age > 30 or self.Entity.Position.y < -50 then
        self.Entity:Destroy()
    end
end

return Bouncer
