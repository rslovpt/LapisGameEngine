# Drops a coloured ball every 0.6 s (up to 40), press B for an extra one,
# R to clear them. Shows a small HUD with the count.
from lapis import *
import random


class BallSpawner(Script):
    interval = 0.6
    limit = 40

    def on_start(self):
        self.timer = 0.0
        self.balls = []
        print("BallSpawner ready - press B to drop a ball, R to clear")

    def drop(self):
        p = self.entity.position
        color = (random.random(), random.random() * 0.6 + 0.4, random.random())
        offset = Vector3(random.uniform(-1.5, 1.5), 0, random.uniform(-1.5, 1.5))
        self.balls.append(spawn("Ball", "Sphere", position=p + offset, physics="Dynamic", color=color, scale=(0.6, 0.6, 0.6)))

    def on_update(self, dt):
        self.timer += dt
        if self.timer >= self.interval and len(self.balls) < self.limit:
            self.timer = 0.0
            self.drop()
        if key_pressed("B"):
            self.drop()
        if key_pressed("R"):
            for b in self.balls:
                if b.valid:
                    b.destroy()
            self.balls = []
        hud_rect(16, 16, 300, 36, Color.black, 0.45)
        hud_text(28, 24, "Python balls: %d   (B drop, R clear)" % len(self.balls), Color.yellow, 1.1)
