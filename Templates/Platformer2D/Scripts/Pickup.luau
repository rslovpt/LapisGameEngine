-- Coins, checkpoints, spikes and the goal flag share this script; the
-- entity name decides what it does:
--   "Coin"       - collected on touch (spins, hides itself)
--   "Checkpoint" - sets the respawn point
--   "Spikes"     - hurts the player
--   "Goal"       - finishes the level

local Pickup = {}

function Pickup:OnStart()
    self.home = self.Entity.Position
    self.kind = self.Entity.Name
    self.IsPickup = self.kind == "Coin" or self.kind == "Checkpoint" or self.kind == "Goal"
    self.done = false
end

function Pickup:OnUpdate(dt: number)
    if self.kind == "Coin" and not self.done then
        local t = Time.Now()
        self.Entity.Rotation = Vector3.new(0, t * 180, 0)
        self.Entity.Position = self.home + Vector3.new(0, math.sin(t * 3 + self.home.x) * 0.12, 0)
    elseif self.kind == "Goal" then
        self.Entity.Rotation = Vector3.new(0, 0, math.sin(Time.Now() * 2) * 4)
    end
end

function Pickup:OnCollision(other)
    if other.Name ~= "Player" or not other.Script then return end
    local player = other.Script
    if self.kind == "Coin" and not self.done then
        self.done = true
        player:Collect()
        self.Entity.Visible = false
        self.Entity.Position = Vector3.new(self.home.x, -500, 0)
    elseif self.kind == "Checkpoint" and not self.done then
        self.done = true
        player:SetCheckpoint(self.home + Vector3.new(0, 1, 0))
        self.Entity.Rotation = Vector3.new(0, 0, 0)
        print("Checkpoint!")
    elseif self.kind == "Spikes" then
        player:Hurt(false)
    elseif self.kind == "Goal" then
        player:Win()
    end
end

function Pickup:Reset()
    self.done = false
    self.Entity.Visible = true
    self.Entity.Position = self.home
end

return Pickup
