-- First Person Character controller (Lapis template)
--
--   Mouse  - look around (click the window to capture the mouse, Esc to release)
--   WASD   - move          Shift - sprint          Space - jump
--
-- The player is a dynamic rigid body with a tall box collider. Each frame we
-- set its horizontal velocity from the input and keep the vertical velocity
-- from physics (gravity). Ground detection uses a short downward raycast.
-- The camera is a child entity ("PlayerCamera"), pitched up/down here while
-- the body itself only turns left/right (yaw).

local Controller = {}

Controller.WalkSpeed = 5.5
Controller.SprintSpeed = 9
Controller.JumpSpeed = 7.5
Controller.AirControl = 0.25     -- 0 = no steering in the air, 1 = full
Controller.MouseSensitivity = 0.12
Controller.HalfHeight = 0.9      -- must match the Collider size Y

function Controller:OnStart()
    self.camera = Scene.Find("PlayerCamera")
    self.yaw = self.Entity.Rotation.y
    self.pitch = 0
    self.spawn = self.Entity.Position
    self.coins = 0
    self.totalCoins = 0
    for _, e in Scene.GetEntities() do
        if e.Name == "Coin" then self.totalCoins += 1 end
    end
    self.mouseCaptured = true
    App.LockCursor(true)
end

function Controller:IsGrounded(): boolean
    local origin = self.Entity.Position
    local hit = Physics.Raycast(origin, Vector3.new(0, -1, 0), self.HalfHeight + 0.15, self.Entity)
    return hit ~= nil
end

function Controller:OnUpdate(dt: number)
    local body = self.Entity

    -- Mouse capture: Esc releases, clicking captures again.
    if self.mouseCaptured and Input.WasKeyPressed("Escape") then
        self.mouseCaptured = false
        App.LockCursor(false)
    elseif not self.mouseCaptured and Input.WasMousePressed("Left") then
        self.mouseCaptured = true
        App.LockCursor(true)
    end

    -- Look.
    if self.mouseCaptured then
        local d = Input.GetMouseDelta()
        self.yaw -= d.x * self.MouseSensitivity
        self.pitch = math.clamp(self.pitch - d.y * self.MouseSensitivity, -85, 85)
    end
    body.Rotation = Vector3.new(0, self.yaw, 0)
    if self.camera then self.camera.Rotation = Vector3.new(self.pitch, 0, 0) end

    -- Move (relative to where the body faces).
    local forward = body.Forward
    local right = body.Right
    local wish = Vector3.zero
    if Input.IsKeyDown("W") then wish += forward end
    if Input.IsKeyDown("S") then wish -= forward end
    if Input.IsKeyDown("D") then wish += right end
    if Input.IsKeyDown("A") then wish -= right end
    if Vector3.Length(wish) > 0 then wish = Vector3.Normalize(wish) end

    local grounded = self:IsGrounded()
    local speed = if Input.IsKeyDown("Shift") then self.SprintSpeed else self.WalkSpeed
    local v = body.Velocity
    local target = wish * speed
    local control = if grounded then 1 else self.AirControl
    local horizontal = Vector3.Lerp(Vector3.new(v.x, 0, v.z), target, math.min(control * 12 * dt, 1))
    local vy = v.y
    if grounded and Input.WasKeyPressed("Space") then vy = self.JumpSpeed end
    body.Velocity = Vector3.new(horizontal.x, vy, horizontal.z)

    -- Fell off the world: respawn.
    if body.Position.y < -20 then
        body.Position = self.spawn
        body.Velocity = Vector3.zero
    end

    self:DrawHUD(grounded)
end

function Controller:Collect()
    self.coins += 1
    print(string.format("Coin collected (%d/%d)", self.coins, self.totalCoins))
    if self.coins == self.totalCoins then print("All coins collected!") end
end

function Controller:DrawHUD(grounded: boolean)
    local size = HUD.ScreenSize()
    local cx, cy = size.x / 2, size.y / 2
    -- crosshair
    HUD.Rect(cx - 8, cy - 1, 16, 2, Color.White, 0.8)
    HUD.Rect(cx - 1, cy - 8, 2, 16, Color.White, 0.8)
    -- coin counter
    HUD.Rect(16, 16, 190, 34, Color.Black, 0.45)
    HUD.Text(28, 22, string.format("Coins: %d / %d", self.coins, self.totalCoins), Color.Yellow, 1.3)
    if self.coins == self.totalCoins and self.totalCoins > 0 then
        HUD.Text(cx - 170, cy - 80, "All coins collected - nice!", Color.Yellow, 1.8)
    end
    if not self.mouseCaptured then
        HUD.Text(cx - 120, cy + 40, "Click to capture the mouse", Color.White, 1.2)
    end
    HUD.Text(16, size.y - 30, "WASD move   Shift sprint   Space jump   Esc release mouse", Color.White, 1.0, 0.8)
end

return Controller
