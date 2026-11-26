-- Free-fly camera: hold right mouse to look around, WASD to move,
-- Q/E down/up, Shift to go fast, scroll wheel changes speed.
local FlyCamera = {}

FlyCamera.Speed = 8
FlyCamera.Sensitivity = 0.12

function FlyCamera:OnStart()
    local r = self.Entity.Rotation
    self.pitch = r.x
    self.yaw = r.y
    self.looking = false
end

function FlyCamera:OnUpdate(dt: number)
    local e = self.Entity

    local wantLook = Input.IsMouseDown("Right")
    if wantLook ~= self.looking then
        self.looking = wantLook
        App.LockCursor(wantLook)
    end
    if self.looking then
        local d = Input.GetMouseDelta()
        self.yaw -= d.x * self.Sensitivity
        self.pitch = math.clamp(self.pitch - d.y * self.Sensitivity, -89, 89)
        e.Rotation = Vector3.new(self.pitch, self.yaw, 0)
    end

    local scroll = Input.GetScroll()
    if scroll ~= 0 then
        self.Speed = math.clamp(self.Speed * (1 + scroll * 0.15), 1, 100)
    end

    local move = Vector3.zero
    if Input.IsKeyDown("W") then move += e.Forward end
    if Input.IsKeyDown("S") then move -= e.Forward end
    if Input.IsKeyDown("D") then move += e.Right end
    if Input.IsKeyDown("A") then move -= e.Right end
    if Input.IsKeyDown("E") then move += Vector3.up end
    if Input.IsKeyDown("Q") then move -= Vector3.up end

    if Vector3.Length(move) > 0 then
        local speed = self.Speed * (Input.IsKeyDown("Shift") and 3 or 1)
        e.Position += Vector3.Normalize(move) * speed * dt
    end
end

return FlyCamera
