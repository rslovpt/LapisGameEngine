-- Smooth side-scrolling camera with look-ahead, clamped to the level, plus
-- parallax for background layers (Lapis 2D template).
--
-- Attach to the camera entity. Background entities whose names start with
-- "Far" or "Mid" scroll slower than the level, which makes them look distant.

local Camera = {}

Camera.TargetName = "Player"
Camera.Smoothing = 6         -- higher = follows more tightly
Camera.LookAhead = 2.5       -- how far ahead of the player (in the facing direction) to look
Camera.VerticalOffset = 1.5
Camera.MinX = 2
Camera.MaxX = 118
Camera.MinY = 3
Camera.FarParallax = 0.85    -- 1 = moves with the camera (infinitely far), 0 = part of the level
Camera.MidParallax = 0.55

function Camera:OnStart()
    self.target = Scene.Find(self.TargetName)
    self.lookAhead = 0
    local start = if self.target then self.target.Position else self.Entity.Position
    self.Entity.Position = Vector3.new(start.x, start.y + self.VerticalOffset, self.Entity.Position.z)
    -- Remember where each background layer starts.
    self.layers = {}
    for _, e in Scene.GetEntities() do
        local n = e.Name
        if string.sub(n, 1, 3) == "Far" then table.insert(self.layers, {e = e, base = e.Position, factor = self.FarParallax})
        elseif string.sub(n, 1, 3) == "Mid" then table.insert(self.layers, {e = e, base = e.Position, factor = self.MidParallax}) end
    end
    self.startX = self.Entity.Position.x
end

function Camera:OnUpdate(dt: number)
    if not self.target or not self.target.IsValid then return end
    local p = self.target.RenderPosition          -- smooth, interpolated position
    local v = self.target.Velocity
    local wantAhead = if math.abs(v.x) > 0.5 then math.sign(v.x) * self.LookAhead else self.lookAhead
    self.lookAhead += (wantAhead - self.lookAhead) * math.min(2 * dt, 1)

    local cam = self.Entity.Position
    local tx = math.clamp(p.x + self.lookAhead, self.MinX, self.MaxX)
    local ty = math.max(p.y + self.VerticalOffset, self.MinY)
    local k = math.min(self.Smoothing * dt, 1)
    local x = cam.x + (tx - cam.x) * k
    local y = cam.y + (ty - cam.y) * k
    self.Entity.Position = Vector3.new(x, y, cam.z)

    -- Parallax: layers follow the camera by their factor.
    local dx = x - self.startX
    for _, layer in self.layers do
        if layer.e.IsValid then
            layer.e.Position = Vector3.new(layer.base.x + dx * layer.factor, layer.base.y + (y - self.MinY) * layer.factor * 0.5, layer.base.z)
        end
    end
end

return Camera
