# Bounces anything that hits it upward and flashes.
from lapis import *


class Bumper(Script):
    def on_start(self):
        self.flash = 0.0

    def on_collision(self, other):
        v = other.velocity
        other.velocity = Vector3(v.x, 9.0, v.z)
        self.flash = 0.3

    def on_update(self, dt):
        self.flash = max(0.0, self.flash - dt)
        k = self.flash / 0.3
        self.entity.set_color((0.3 + 0.7 * k, 0.4 + 0.5 * k, 1.0))
