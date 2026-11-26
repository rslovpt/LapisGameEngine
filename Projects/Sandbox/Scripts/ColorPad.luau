-- A trigger pad: anything that touches it gets recolored.
local ColorPad = {}

function ColorPad:OnCollision(other)
    other:SetColor(Color.HSV(math.random(), 0.8, 1))
    print(other.Name .. " touched the color pad")
end

return ColorPad
