-- Patrolling enemy: walks back and forth between two points. Jump on its head
-- to defeat it; touching it from the side hurts the player.
-- The enemy has a trigger collider and no rigid body: it is moved by script.

local Enemy = {}

Enemy.Range = 3          -- walks this far left and right of its start
Enemy.Speed = 2.5

function Enemy:OnStart()
    self.home = self.Entity.Position
    self.dir = 1
    self.alive = true
end

function Enemy:OnUpdate(dt: number)
    if not self.alive then return end
    local p = self.Entity.Position + Vector3.new(self.dir * self.Speed * dt, 0, 0)
    if math.abs(p.x - self.home.x) > self.Range then self.dir = -self.dir end
    self.Entity.Position = p
    -- a little waddle
    self.Entity.Rotation = Vector3.new(0, 0, math.sin(Time.Now() * 10) * 6)
end

function Enemy:OnCollision(other)
    if not self.alive or other.Name ~= "Player" or not other.Script then return end
    local fromAbove = other.Position.y > self.Entity.Position.y + 0.35 and other.Velocity.y <= 0.5
    if fromAbove then
        other.Script:Bounce()
        self:Defeat()
    else
        other.Script:Hurt(false)
    end
end

function Enemy:Defeat()
    self.alive = false
    self.Entity.Visible = false
    self.Entity.Position = Vector3.new(self.home.x, -500, 0)   -- out of the way (Reset brings it back)
end

function Enemy:Reset()
    self.alive = true
    self.dir = 1
    self.Entity.Visible = true
    self.Entity.Position = self.home
end

return Enemy
