--!strict
-- MainScript: runs once when the project starts (after the scene loads).
-- Builds the procedural parts of the level.

print("Welcome to the Lapis Sandbox!")
print("Controls: hold Right Mouse = look, WASD/QE = move, Shift = fast,")
print("          Left Mouse / Space = throw ball, B = drop crate, R = reset, F1 = colliders, Esc = quit")

-- A pyramid of crates the player can knock over.
local function BuildTower(origin: vector)
    local rows = 5
    for row = 0, rows - 1 do
        local count = rows - row
        for i = 0, count - 1 do
            local x = (i - (count - 1) / 2) * 1.02
            Scene.Spawn({
                Name = "Crate",
                Mesh = "Cube",
                Texture = "Textures/Crate.tga",
                Position = origin + Vector3.new(x, 0.5 + row * 1.0, 0),
                Physics = "Dynamic",
                Mass = 1,
                Friction = 0.7,
                Restitution = 0.05,
                Roughness = 0.8,
            })
        end
    end
end

BuildTower(Vector3.new(5, 0, -3))

-- A row of colored pillars.
for i = 0, 6 do
    Scene.Spawn({
        Name = "Pillar",
        Mesh = "Cube",
        Position = Vector3.new(-10 + i * 3.3, 2, -11),
        Scale = Vector3.new(0.8, 4, 0.8),
        Color = Color.HSV(i / 7, 0.55, 0.95),
        Roughness = 0.35,
        Physics = "Static",
    })
end
