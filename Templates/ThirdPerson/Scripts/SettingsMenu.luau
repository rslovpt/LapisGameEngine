-- In-game graphics settings menu (Lapis template)
--
--   Tab          open / close
--   Up / Down    choose a setting      Left / Right  change it
--   Q / E        previous / next tab   1-4           Low / Medium / High / Ultra preset
--   Enter        apply + save          R             back to the game's defaults
--
-- Everything is generated from Graphics.Settings(), so new engine settings
-- show up here automatically. The player's choices are saved with
-- Graphics.Save() (Saved/graphics.cfg) and loaded again on the next start.
-- Other scripts can check `Scene.Find("SettingsMenu").Script.Open`.

local Menu = {}

Menu.Open = false
Menu.WasOpen = false   -- open last frame (lets the controller re-capture the mouse)

function Menu:OnStart()
    self.tab = 1
    self.row = 1
    self.dirty = false
    self.message = ""
    self.messageTime = 0
end

-- Settings of the current tab, freshly read (values change as we edit).
function Menu:Rows()
    local categories = Graphics.Categories()
    self.tab = math.clamp(self.tab, 1, #categories)
    local rows = {}
    for _, s in Graphics.Settings() do
        if s.Category == categories[self.tab] then table.insert(rows, s) end
    end
    return rows, categories
end

local function OptionIndex(setting): number
    for i, o in setting.Options do
        if o.Name == setting.Value then return i end
    end
    return 1
end

function Menu:Change(setting, direction: number)
    if setting.Type == "bool" then
        Graphics.Set(setting.Key, not setting.Value)
    elseif setting.Type == "option" then
        local i = OptionIndex(setting) + direction
        if i < 1 then i = #setting.Options elseif i > #setting.Options then i = 1 end
        Graphics.Set(setting.Key, setting.Options[i].Name)
    else
        local step = if setting.Step > 0 then setting.Step else (setting.Max - setting.Min) / 20
        Graphics.Set(setting.Key, math.clamp(setting.Value + step * direction, setting.Min, setting.Max))
    end
    self.dirty = true
end

function Menu:Notify(text: string)
    self.message = text
    self.messageTime = Time.Now()
end

function Menu:OnUpdate(dt: number)
    self.WasOpen = self.Open
    if Input.WasKeyPressed("Tab") then
        self.Open = not self.Open
        App.LockCursor(not self.Open)
    end
    if not self.Open then return end

    local rows, categories = self:Rows()
    if Input.WasKeyPressed("Q") then self.tab = if self.tab > 1 then self.tab - 1 else #categories; self.row = 1 end
    if Input.WasKeyPressed("E") then self.tab = if self.tab < #categories then self.tab + 1 else 1; self.row = 1 end
    rows, categories = self:Rows()
    if Input.WasKeyPressed("Up") then self.row = if self.row > 1 then self.row - 1 else #rows end
    if Input.WasKeyPressed("Down") then self.row = if self.row < #rows then self.row + 1 else 1 end
    self.row = math.clamp(self.row, 1, math.max(#rows, 1))
    local current = rows[self.row]
    if current then
        if Input.WasKeyPressed("Left") then self:Change(current, -1) end
        if Input.WasKeyPressed("Right") then self:Change(current, 1) end
    end
    local presets = {"Low", "Medium", "High", "Ultra"}
    for i, name in presets do
        if Input.WasKeyPressed(tostring(i)) then Graphics.Preset(name); self.dirty = true; self:Notify(name .. " preset (Enter to apply)") end
    end
    if Input.WasKeyPressed("R") then Graphics.Reset(); self.dirty = true; self:Notify("Defaults restored (Enter to apply)") end
    if Input.WasKeyPressed("Enter") then
        Graphics.Apply()
        self.dirty = false
        self:Notify(if Graphics.Save() then "Applied and saved" else "Applied (could not save)")
    end

    self:Draw(self:Rows())
end

local function ValueText(s): string
    if s.Type == "bool" then return if s.Value then "On" else "Off" end
    if s.Type == "option" then return s.Value end
    if s.Max <= 2 then return string.format("%d%%", math.floor(s.Value * 100 + 0.5)) end
    return tostring(math.floor(s.Value + 0.5))
end

function Menu:Draw(rows, categories)
    local size = HUD.ScreenSize()
    local w, h = 640, 120 + #rows * 30
    local x, y = (size.x - w) / 2, math.max(40, (size.y - h) / 2)
    HUD.Rect(0, 0, size.x, size.y, Color.Black, 0.35)
    HUD.Rect(x, y, w, h, Vector3.new(0.08, 0.09, 0.11), 0.94)
    HUD.Text(x + 20, y + 14, "GRAPHICS SETTINGS", Color.White, 1.4)

    -- tabs
    local tx = x + 20
    for i, name in categories do
        local active = i == self.tab
        HUD.Text(tx, y + 52, name, if active then Color.Yellow else Vector3.new(0.6, 0.62, 0.66), 1.0)
        tx += #name * 9 + 24
    end

    -- rows
    local ry = y + 86
    for i, s in rows do
        local selected = i == self.row
        if selected then HUD.Rect(x + 12, ry - 4, w - 24, 28, Vector3.new(0.2, 0.35, 0.65), 0.9) end
        HUD.Text(x + 24, ry, s.Label, Color.White, 1.0)
        local value = ValueText(s)
        if s.Type == "option" then
            for _, o in s.Options do
                if o.Name == s.Value and not o.Available then value ..= "  (fallback)" end
            end
        end
        HUD.Text(x + w * 0.55, ry, (if selected then "<  " else "   ") .. value .. (if selected then "  >" else ""), Color.White, 1.0)
        ry += 30
    end

    local gpu = Graphics.GPU()
    local footer = "Tab close   Q/E tabs   Arrows change   1-4 presets   R defaults   Enter apply+save"
    HUD.Text(x + 20, y + h - 26, footer, Vector3.new(0.6, 0.62, 0.66), 0.85)
    HUD.Text(x + 20, y + h + 8, "GPU: " .. gpu.Name .. "  (" .. gpu.API .. ")", Vector3.new(0.6, 0.62, 0.66), 0.85)
    if self.dirty then HUD.Text(x + w - 170, y + 16, "unapplied changes", Color.Yellow, 0.9) end
    if self.message ~= "" and Time.Now() - self.messageTime < 2.5 then
        HUD.Text(x + w - 260, y + 52, self.message, Color.Yellow, 0.9)
    end
end

return Menu
