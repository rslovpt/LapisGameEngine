-- Platform that moves back and forth. Players standing on it are carried
-- along: the player script reads PlatformVelocity from whatever it stands on.

local Platform = {}

Platform.Offset = Vector3.new(6, 0, 0)   -- travel from the start position
Platform.Period = 4                       -- seconds for a full back-and-forth

function Platform:OnStart()
    -- Entities named "Lift" move up and down instead.
    if self.Entity.Name == "Lift" then
        self.Offset = Vector3.new(0, 4, 0)
        self.Period = 5
    end
    self.home = self.Entity.Position
    self.PlatformVelocity = Vector3.zero
end

function Platform:OnUpdate(dt: number)
    local t = Time.Now() * (2 * math.pi / self.Period)
    local f = (1 - math.cos(t)) * 0.5            -- 0..1, eased at the ends
    local target = self.home + self.Offset * f
    if dt > 0 then self.PlatformVelocity = (target - self.Entity.Position) / dt end
    self.Entity.Position = target
end

return Platform
