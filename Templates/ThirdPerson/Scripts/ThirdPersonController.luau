-- Third Person Character controller + orbit camera (Lapis template)
--
--   Mouse   - orbit the camera (click to capture the mouse, Esc to release)
--   Wheel   - zoom in / out
--   WASD    - move relative to the camera     Shift - sprint
--   Space   - jump (press again in the air for a double jump)
--   Tab     - graphics settings menu
--
-- How it works
--   * "Player" is a dynamic rigid body with a box collider. Its rotation is
--     never changed (so the collider never turns); instead the visible child
--     "Model" turns smoothly to face the direction of movement.
--   * Movement is relative to the camera's yaw, flattened onto the ground.
--   * "PlayerCamera" is NOT a child of the player. Every frame it is placed
--     behind the player's RenderPosition (the smoothly interpolated position
--     the player is drawn at), so it never jitters at any frame rate.
--   * A raycast from the player's head to the camera pulls the camera in
--     front of walls so it never clips through them.

local Controller = {}

-- Movement
Controller.WalkSpeed = 6
Controller.SprintSpeed = 10
Controller.JumpSpeed = 8
Controller.DoubleJump = true
Controller.AirControl = 0.35
Controller.TurnSpeed = 12          -- how fast the model turns to face movement
Controller.HalfHeight = 0.9        -- must match the Collider size Y

-- Camera
Controller.MouseSensitivity = 0.15
Controller.Distance = 6            -- default zoom
Controller.MinDistance = 2
Controller.MaxDistance = 12
Controller.PivotHeight = 1.4       -- look-at point above the player's center
Controller.ShoulderOffset = 0.6    -- over-the-shoulder offset to the right
Controller.CameraRadius = 0.25     -- keep this far from walls
Controller.MinPitch = -70
Controller.MaxPitch = 40

local function lerpAngle(a: number, b: number, t: number): number
    local d = (b - a + 180) % 360 - 180
    return a + d * t
end

function Controller:OnStart()
    self.camera = Scene.Find("PlayerCamera")
    self.model = Scene.Find("Model")
    self.yaw = 0
    self.pitch = -15
    self.zoom = self.Distance
    self.currentDistance = self.Distance
    self.modelYaw = 0
    self.spawn = self.Entity.Position
    self.canDoubleJump = false
    self.coins = 0
    self.totalCoins = 0
    for _, e in Scene.GetEntities() do
        if e.Name == "Coin" then self.totalCoins += 1 end
    end
    self.mouseCaptured = true
    App.LockCursor(true)
end

function Controller:IsGrounded(): boolean
    -- Three short rays (center + two corners) so edges count as ground.
    local p = self.Entity.Position
    local down = Vector3.new(0, -1, 0)
    local reach = self.HalfHeight + 0.15
    for _, o in {Vector3.zero, Vector3.new(0.25, 0, 0.25), Vector3.new(-0.25, 0, -0.25)} do
        if Physics.Raycast(p + o, down, reach, self.Entity) then return true end
    end
    return false
end

function Controller:OnUpdate(dt: number)
    local body = self.Entity

    -- Settings menu open: stand still and let the menu have the mouse.
    local menu = Scene.Find("SettingsMenu")
    local menuOpen = menu and menu.Script and menu.Script.Open
    if menuOpen then
        local v = body.Velocity
        body.Velocity = Vector3.new(0, v.y, 0)
        self.mouseCaptured = false
        self:UpdateCamera(dt)
        return
    end
    if not self.mouseCaptured and menu and menu.Script and menu.Script.WasOpen then self.mouseCaptured = true end

    -- Mouse capture: Esc releases, clicking captures again.
    if self.mouseCaptured and Input.WasKeyPressed("Escape") then
        self.mouseCaptured = false
        App.LockCursor(false)
    elseif not self.mouseCaptured and Input.WasMousePressed("Left") then
        self.mouseCaptured = true
        App.LockCursor(true)
    end

    -- Orbit + zoom.
    if self.mouseCaptured then
        local d = Input.GetMouseDelta()
        self.yaw -= d.x * self.MouseSensitivity
        self.pitch = math.clamp(self.pitch - d.y * self.MouseSensitivity, self.MinPitch, self.MaxPitch)
    end
    self.zoom = math.clamp(self.zoom - Input.GetScroll() * 0.8, self.MinDistance, self.MaxDistance)

    -- Movement relative to the camera's yaw.
    local yawRad = math.rad(self.yaw)
    local forward = Vector3.new(-math.sin(yawRad), 0, -math.cos(yawRad))
    local right = Vector3.new(math.cos(yawRad), 0, -math.sin(yawRad))
    local wish = Vector3.zero
    if Input.IsKeyDown("W") then wish += forward end
    if Input.IsKeyDown("S") then wish -= forward end
    if Input.IsKeyDown("D") then wish += right end
    if Input.IsKeyDown("A") then wish -= right end
    local moving = Vector3.Length(wish) > 0
    if moving then wish = Vector3.Normalize(wish) end

    local grounded = self:IsGrounded()
    if grounded then self.canDoubleJump = self.DoubleJump end
    local speed = if Input.IsKeyDown("Shift") then self.SprintSpeed else self.WalkSpeed
    local v = body.Velocity
    local control = if grounded then 1 else self.AirControl
    local horizontal = Vector3.Lerp(Vector3.new(v.x, 0, v.z), wish * speed, math.min(control * 12 * dt, 1))
    local vy = v.y
    if Input.WasKeyPressed("Space") then
        if grounded then
            vy = self.JumpSpeed
        elseif self.canDoubleJump then
            vy = self.JumpSpeed * 0.9
            self.canDoubleJump = false
        end
    end
    body.Velocity = Vector3.new(horizontal.x, vy, horizontal.z)

    -- Turn the visible model toward the movement direction.
    if moving and self.model then
        local targetYaw = math.deg(math.atan2(-wish.x, -wish.z))
        self.modelYaw = lerpAngle(self.modelYaw, targetYaw, math.min(self.TurnSpeed * dt, 1))
        self.model.Rotation = Vector3.new(0, self.modelYaw, 0)
    end
    -- A little squash & stretch while in the air.
    if self.model then
        local s = if grounded then 1 else math.clamp(1 + v.y * 0.02, 0.9, 1.15)
        self.model.Scale = Vector3.new(1 / math.sqrt(s), s, 1 / math.sqrt(s))
    end

    -- Fell off the world: respawn.
    if body.Position.y < -20 then
        body.Position = self.spawn
        body.Velocity = Vector3.zero
    end

    self:UpdateCamera(dt)
    self:DrawHUD()
end

function Controller:UpdateCamera(dt: number)
    if not self.camera then return end
    self.camera.Rotation = Vector3.new(self.pitch, self.yaw, 0)
    local camForward = self.camera.Forward
    local camRight = self.camera.Right

    -- Follow the interpolated (drawn) position, not the raw physics one.
    local pivot = self.Entity.RenderPosition + Vector3.new(0, self.PivotHeight - self.HalfHeight, 0) + camRight * self.ShoulderOffset

    -- Pull the camera in front of anything between it and the player.
    local wanted = self.zoom
    local hit = Physics.Raycast(pivot, -camForward, wanted + self.CameraRadius, self.Entity)
    if hit and not hit.Entity.Script then  -- ignore coins (they have scripts)
        wanted = math.max(0.5, hit.Distance - self.CameraRadius)
    end
    -- Zoom in instantly (no clipping), zoom back out smoothly.
    if wanted < self.currentDistance then
        self.currentDistance = wanted
    else
        self.currentDistance += (wanted - self.currentDistance) * math.min(6 * dt, 1)
    end
    self.camera.Position = pivot - camForward * self.currentDistance
end

function Controller:Collect()
    self.coins += 1
    print(string.format("Coin collected (%d/%d)", self.coins, self.totalCoins))
end

function Controller:DrawHUD()
    local size = HUD.ScreenSize()
    HUD.Rect(16, 16, 190, 34, Color.Black, 0.45)
    HUD.Text(28, 22, string.format("Coins: %d / %d", self.coins, self.totalCoins), Color.Yellow, 1.3)
    if self.coins == self.totalCoins and self.totalCoins > 0 then
        HUD.Text(size.x / 2 - 170, size.y / 2 - 120, "All coins collected - nice!", Color.Yellow, 1.8)
    end
    if not self.mouseCaptured then
        HUD.Text(size.x / 2 - 120, size.y / 2 + 60, "Click to capture the mouse", Color.White, 1.2)
    end
    HUD.Text(16, size.y - 30, "WASD move   Mouse orbit   Wheel zoom   Shift sprint   Space jump x2   Tab settings", Color.White, 1.0, 0.8)
end

return Controller
