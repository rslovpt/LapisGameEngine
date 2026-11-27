# Spins and bobs the entity it is attached to.
from lapis import *


class Spinner(Script):
    speed = 90.0      # degrees per second
    bob = 0.25        # metres

    def on_start(self):
        self.base = self.entity.position

    def on_update(self, dt):
        t = time()
        self.entity.rotation = Vector3(0, t * self.speed, 0)
        self.entity.position = self.base + Vector3(0, math.sin(t * 2.0) * self.bob, 0)
