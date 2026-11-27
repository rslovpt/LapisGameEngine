-- 2D platformer player (Lapis template)
--
--   A / D or Left / Right  - run          Shift - run faster
--   Space / W / Up         - jump (hold for a higher jump)
--   R                      - restart      Tab   - graphics settings
--
-- The game is 3D under the hood: everything lives on the X-Y plane, the
-- camera is orthographic, and the player's rigid body has `lock=z` so physics
-- can never push it out of the plane.
--
-- Game-feel tricks used here (all tunable below):
--   * coyote time  - you can still jump a moment after running off a ledge
--   * jump buffer  - pressing jump just before landing still jumps
--   * variable jump height - releasing jump early cuts the jump short
--   * faster falling than rising, capped fall speed
--   * acceleration / deceleration instead of instant velocity

local Player = {}

Player.RunSpeed = 7
Player.SprintSpeed = 10
Player.Acceleration = 70
Player.Deceleration = 60
Player.AirAcceleration = 40
Player.JumpSpeed = 13
Player.JumpCutMultiplier = 0.45   -- vertical speed kept when jump is released early
Player.FallGravityMultiplier = 1.6
Player.MaxFallSpeed = 22
Player.CoyoteTime = 0.1
Player.JumpBuffer = 0.12
Player.HalfSize = 0.45            -- matches Collider size
Player.Lives = 3
Player.InvulnerableTime = 1.2

function Player:OnStart()
    self.spawn = self.Entity.Position
    self.checkpoint = self.spawn
    self.coins = 0
    self.totalCoins = 0
    for _, e in Scene.GetEntities() do
        if e.Name == "Coin" then self.totalCoins += 1 end
    end
    self.lives = self.Lives
    self.coyote = 0
    self.buffer = 0
    self.facing = 1
    self.hurtTimer = 0
    self.time = 0
    self.won = false
    self.gameOver = false
    self.eyes = {Scene.Find("EyeL"), Scene.Find("EyeR")}
    self.pupils = {Scene.Find("PupilL"), Scene.Find("PupilR")}
    App.LockCursor(false)
end

-- Ground check: two rays at the feet corners. Returns the entity below (or nil).
function Player:Ground()
    local p = self.Entity.Position
    local down = Vector3.new(0, -1, 0)
    for _, dx in {-self.HalfSize + 0.05, self.HalfSize - 0.05} do
        local hit = Physics.Raycast(p + Vector3.new(dx, 0, 0), down, self.HalfSize + 0.08, self.Entity)
        if hit and not (hit.Entity.Script and hit.Entity.Script.IsPickup) then return hit.Entity end
    end
    return nil
end

local function approach(value: number, target: number, amount: number): number
    if value < target then return math.min(value + amount, target) end
    return math.max(value - amount, target)
end

function Player:OnUpdate(dt: number)
    local menu = Scene.Find("SettingsMenu")
    if menu and menu.Script and menu.Script.Open then return end

    if Input.WasKeyPressed("R") then self:Restart() end
    if self.won or self.gameOver then self:DrawHUD(); return end
    self.time += dt

    local body = self.Entity
    local v = body.Velocity
    local ground = self:Ground()
    local grounded = ground ~= nil and v.y <= 0.1

    -- Input
    local dir = 0
    if Input.IsKeyDown("A") or Input.IsKeyDown("Left") then dir -= 1 end
    if Input.IsKeyDown("D") or Input.IsKeyDown("Right") then dir += 1 end
    local jumpPressed = Input.WasKeyPressed("Space") or Input.WasKeyPressed("W") or Input.WasKeyPressed("Up")
    local jumpHeld = Input.IsKeyDown("Space") or Input.IsKeyDown("W") or Input.IsKeyDown("Up")

    -- Horizontal: accelerate toward the target speed. Standing on a moving
    -- platform adds the platform's own velocity.
    local carry = 0
    if grounded and ground.Script and ground.Script.PlatformVelocity then carry = ground.Script.PlatformVelocity.x end
    local top = if Input.IsKeyDown("Shift") then self.SprintSpeed else self.RunSpeed
    local accel = if dir ~= 0 then (if grounded then self.Acceleration else self.AirAcceleration) else self.Deceleration
    local vx = approach(v.x - (self.lastCarry or 0), dir * top, accel * dt) + carry
    self.lastCarry = carry
    if dir ~= 0 then self.facing = dir end

    -- Jumping with coyote time and buffering.
    self.coyote = if grounded then self.CoyoteTime else self.coyote - dt
    self.buffer = if jumpPressed then self.JumpBuffer else self.buffer - dt
    local vy = v.y
    if self.buffer > 0 and self.coyote > 0 then
        vy = self.JumpSpeed
        self.buffer = 0
        self.coyote = 0
    end
    if not jumpHeld and vy > 0 then vy *= self.JumpCutMultiplier ^ (dt * 20) end
    -- Heavier falling (on top of normal gravity), capped.
    if vy < 0 then vy -= 20 * (self.FallGravityMultiplier - 1) * dt end
    vy = math.max(vy, -self.MaxFallSpeed)
    body.Velocity = Vector3.new(vx, vy, 0)

    -- Eyes look where we're going.
    for i, eye in self.eyes do
        if eye then
            local x = (if i == 1 then -0.15 else 0.15) + self.facing * 0.1
            eye.Position = Vector3.new(x, 0.15, 0.46)
            local pupil = self.pupils[i]
            if pupil then pupil.Position = Vector3.new(x + self.facing * 0.05, 0.13, 0.5) end
        end
    end

    -- Squash & stretch on the visible model.
    local model = Scene.Find("PlayerModel")
    if model then
        local s = if grounded then 1 else math.clamp(1 + vy * 0.015, 0.85, 1.2)
        model.Scale = Vector3.new(0.9 / s, 0.9 * s, 0.9)
    end

    -- Blink while invulnerable.
    if self.hurtTimer > 0 then
        self.hurtTimer -= dt
        if model then model.Visible = math.floor(self.hurtTimer * 12) % 2 == 0 or self.hurtTimer <= 0 end
    end

    if body.Position.y < -12 then self:Hurt(true) end
    self:DrawHUD()
end

-- Called by enemies / spikes. `fell` = fell out of the world (always hurts).
function Player:Hurt(fell: boolean?)
    if self.won then return end
    if self.hurtTimer > 0 and not fell then return end
    self.lives -= 1
    if self.lives <= 0 then
        self.gameOver = true
        self.Entity.Velocity = Vector3.zero
        return
    end
    self.hurtTimer = self.InvulnerableTime
    if fell then
        self.Entity.Position = self.checkpoint
        self.Entity.Velocity = Vector3.zero
    else
        self.Entity.Velocity = Vector3.new(-self.facing * 8, 9, 0)   -- knock-back
    end
end

function Player:Bounce()
    local v = self.Entity.Velocity
    self.Entity.Velocity = Vector3.new(v.x, self.JumpSpeed * 0.8, 0)
end

function Player:Collect()
    self.coins += 1
end

function Player:SetCheckpoint(position)
    self.checkpoint = position
end

function Player:Win()
    if self.won then return end
    self.won = true
    self.Entity.Velocity = Vector3.zero
    print(string.format("Level complete in %.1f s with %d/%d coins", self.time, self.coins, self.totalCoins))
end

function Player:Restart()
    self.Entity.Position = self.spawn
    self.Entity.Velocity = Vector3.zero
    self.checkpoint = self.spawn
    self.coins, self.lives, self.time = 0, self.Lives, 0
    self.won, self.gameOver, self.hurtTimer = false, false, 0
    for _, e in Scene.GetEntities() do
        if e.Script and e.Script.Reset then e.Script:Reset() end
    end
    local model = Scene.Find("PlayerModel")
    if model then model.Visible = true end
end

function Player:DrawHUD()
    local size = HUD.ScreenSize()
    HUD.Rect(16, 16, 330, 40, Color.Black, 0.45)
    local hearts = string.rep("<3 ", self.lives)
    HUD.Text(28, 24, string.format("Coins %d/%d    Lives %s", self.coins, self.totalCoins, hearts), Color.Yellow, 1.2)
    HUD.Text(size.x - 150, 24, string.format("%6.1f s", self.time), Color.White, 1.3)
    if self.won then
        HUD.Rect(size.x / 2 - 260, size.y / 2 - 70, 520, 130, Color.Black, 0.6)
        HUD.Text(size.x / 2 - 170, size.y / 2 - 50, "LEVEL COMPLETE!", Color.Yellow, 2.2)
        HUD.Text(size.x / 2 - 200, size.y / 2 + 10, string.format("%.1f seconds  -  %d / %d coins   (R to play again)", self.time, self.coins, self.totalCoins), Color.White, 1.0)
    elseif self.gameOver then
        HUD.Rect(size.x / 2 - 200, size.y / 2 - 60, 400, 110, Color.Black, 0.6)
        HUD.Text(size.x / 2 - 110, size.y / 2 - 40, "GAME OVER", Color.Red, 2.2)
        HUD.Text(size.x / 2 - 80, size.y / 2 + 15, "Press R to retry", Color.White, 1.1)
    end
    HUD.Text(16, size.y - 30, "A/D run   Space jump (hold = higher)   Shift faster   R restart   Tab settings", Color.White, 1.0, 0.8)
end

return Player
