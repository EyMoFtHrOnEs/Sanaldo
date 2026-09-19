"""NewCar - a simulated car you can drive, and a real one when there is one.

    uv run main.py

The window is the simulator and it needs nothing else: no car, no radio, not even
bleak installed. Drive it with wasd the moment it opens; hold shift for turbo.

The driving model in car.py runs here either way. Connect with `c` and the duties
it produces go out to the ESP32 at 20 Hz, which puts them straight on the motors -
so what the window shows is what the hardware is doing, not a separate guess at it.
BLE lives on a background thread and narrates itself on the terminal you launched
from, so the window never blocks on a scan.
"""
import asyncio
import threading
import tkinter as tk
from math import cos, dist, degrees, hypot, inf, sin
from time import perf_counter

import car

try:
    from bleak import BleakClient, BleakScanner
except ImportError:  # the simulator has no use for it
    BleakClient = BleakScanner = None

DEVICE = "NewCar"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
SEND_HZ = 20
STOP = b"0 0"

W, H = 1000, 660
SCALE = 170  # pixels per metre
KEYS = set("wasd")
SHIFT = {"shift_l", "shift_r"}
TRAIL_MAX = 3000
BG, INK, DIM, ACCENT = "#11151c", "#e8eef5", "#2a3340", "#4fd1c5"


class Link:
    """BLE on its own thread, reporting to the terminal. The UI only reads attributes.

    Nothing here can stall the window: the worst case is a scan that finds nothing
    and goes back to idle.
    """

    def __init__(self):
        self.status = "idle"
        self.packet = STOP
        self._wanted = BleakScanner is not None
        if BleakScanner is None:
            self.status = "bleak not installed - simulator only"
            print(f"[ble] {self.status}", flush=True)
            return
        threading.Thread(target=lambda: asyncio.run(self._loop()), daemon=True).start()

    def toggle(self):
        if BleakScanner is not None:
            self._wanted = not self._wanted

    def _say(self, status):
        if status != self.status:
            self.status = status
            print(f"[ble] {status}", flush=True)

    async def _loop(self):
        while True:
            if not self._wanted:
                self._say("idle - press c in the window to connect")
                await asyncio.sleep(0.1)
                continue
            self._say(f"scanning for {DEVICE}")
            device = await BleakScanner.find_device_by_name(DEVICE, timeout=8.0)
            if device is None:
                self._say("not found")
                self._wanted = False
                continue
            try:
                async with BleakClient(device) as client:
                    self._say("connected")
                    while self._wanted and client.is_connected:
                        await client.write_gatt_char(NUS_RX, self.packet, response=False)
                        await asyncio.sleep(1 / SEND_HZ)
                    await client.write_gatt_char(NUS_RX, STOP, response=False)
            except Exception as err:  # a dropped link is ordinary; say so and idle
                self._say(f"lost the link ({type(err).__name__})")
            self._wanted = False


class Game:
    def __init__(self, root):
        self.root = root
        self.canvas = tk.Canvas(root, width=W, height=H, bg=BG, highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        self.held = set()
        self.turbo = False
        self.link = Link()
        self.reset()

        root.bind("<KeyPress>", self.press)
        root.bind("<KeyRelease>", self.release)
        self.last = perf_counter()
        self.backlog = 0.0
        self.frame()

    def reset(self):
        self.car = car.Car()
        self.trail = []

    # --- input -------------------------------------------------------------
    # Tk reports shift+w as keysym "W", so lower-casing keeps `held` consistent no
    # matter when shift went down or came up relative to the letter.
    def press(self, event):
        key = event.keysym.lower()
        if key == "escape":
            self.root.destroy()
        elif key == "r":
            self.reset()
        elif key == "c":
            self.link.toggle()
        elif key in SHIFT:
            self.turbo = True
        elif key in KEYS:
            self.held.add(key)

    def release(self, event):
        key = event.keysym.lower()
        if key in SHIFT:
            self.turbo = False
        self.held.discard(key)

    def keys(self):
        held = "".join(sorted(self.held)) or "x"
        return held.upper() if self.turbo else held

    # --- loop --------------------------------------------------------------
    def frame(self):
        now = perf_counter()
        self.backlog = min(self.backlog + now - self.last, 0.25)  # a slow frame must not fast-forward
        self.last = now

        self.car.send(self.keys())
        while self.backlog >= car.DT:
            self.car.tick()
            self.backlog -= car.DT
            if not self.trail or dist(self.trail[-1], (self.car.x, self.car.y)) > 0.01:
                self.trail.append((self.car.x, self.car.y))
        del self.trail[:-TRAIL_MAX]

        # The model runs at 100 Hz and the radio samples it at 20; the duties are
        # rate-limited, so what the car misses between packets is under 5%.
        self.link.packet = self.car.packet()

        self.draw()
        self.root.after(16, self.frame)

    # --- drawing -----------------------------------------------------------
    def to_screen(self, x, y):
        return W / 2 + (x - self.car.x) * SCALE, H / 2 - (y - self.car.y) * SCALE

    def draw(self):
        self.canvas.delete("all")
        self.grid()
        self.turn_circle()
        if len(self.trail) > 1:
            self.canvas.create_line(*[v for p in self.trail for v in self.to_screen(*p)],
                                    fill="#31506b", width=2)
        self.body()
        self.hud()

    def grid(self):
        """Half-metre squares, so a turn radius can be read straight off the screen."""
        step = 0.5
        for i in range(-int(W / SCALE / step) - 1, int(W / SCALE / step) + 2):
            x, _ = self.to_screen((self.car.x // step + i) * step, 0)
            self.canvas.create_line(x, 0, x, H, fill=DIM)
        for i in range(-int(H / SCALE / step) - 1, int(H / SCALE / step) + 2):
            _, y = self.to_screen(0, (self.car.y // step + i) * step)
            self.canvas.create_line(0, y, W, y, fill=DIM)

    def turn_circle(self):
        """The arc the car is committed to, drawn about its centre of rotation."""
        c = self.car
        if abs(c.w) < 1e-3 or abs(c.v) < 1e-3:
            return
        radius = c.v / c.w
        r = abs(radius) * SCALE
        if r > 4 * SCALE:
            return
        sx, sy = self.to_screen(c.x - radius * sin(c.heading), c.y + radius * cos(c.heading))
        tight = abs(radius) <= car.MIN_TURN_RADIUS + 0.01
        self.canvas.create_oval(sx - r, sy - r, sx + r, sy + r,
                                outline="#e07b53" if tight else "#3d6b8f", dash=(4, 4))

    def body(self):
        c = self.car
        ahead, left = cos(c.heading), sin(c.heading)

        def place(fwd, side):
            return self.to_screen(c.x + fwd * ahead - side * left, c.y + fwd * left + side * ahead)

        hull = [place(0.15, 0), place(0.06, 0.085), place(-0.10, 0.085),
                place(-0.10, -0.085), place(0.06, -0.085)]
        self.canvas.create_polygon([v for p in hull for v in p], fill="#1d2b3a",
                                   outline=ACCENT, width=2)
        for side, duty in ((1, c.duty_left), (-1, c.duty_right)):
            corners = [place(f, side * car.TRACK_M / 2 + s * 0.018)
                       for f, s in ((0.05, 1), (0.05, -1), (-0.05, -1), (-0.05, 1))]
            colour = "#4ade80" if duty > 0 else ("#f87171" if duty < 0 else "#64748b")
            self.canvas.create_polygon([v for p in corners for v in p], fill=colour)

    def hud(self):
        c = self.car
        turning = abs(c.w) > 1e-3 and abs(c.v) > 1e-3
        rpm = car.MOTOR_RPM if self.turbo else car.CRUISE_RPM
        rows = [
            f"speed      {c.v:+.2f} m/s   of {car.V_MAX:.2f} max",
            f"yaw        {c.w:+.2f} rad/s",
            f"radius     {abs(c.v / c.w) if turning else inf:6.2f} m   floor {car.MIN_TURN_RADIUS:.2f}",
            f"duty       L {c.duty_left:+.2f}   R {c.duty_right:+.2f}",
            f"mode       {'TURBO' if self.turbo else 'cruise'}  {rpm:.0f} rpm",
            f"travelled  {hypot(c.x, c.y):.2f} m from start",
        ]
        for i, row in enumerate(rows):
            self.canvas.create_text(18, 22 + i * 20, anchor="w", fill=INK,
                                    font=("Consolas", 11), text=row)
        self.canvas.create_text(W - 18, 22, anchor="e", fill=INK, font=("Consolas", 11),
                                text=f"BLE  {self.link.status}")
        self.canvas.create_text(18, H - 42, anchor="w", fill=ACCENT, font=("Consolas", 11),
                                text="w a s d  drive    shift  turbo    c  connect    r  reset    esc  quit")
        self.canvas.create_text(18, H - 20, anchor="w", fill=INK, font=("Consolas", 11),
                                text="sending: " + self.link.packet.decode())


if __name__ == "__main__":
    print("[sim] window is live - wasd to drive, no hardware required", flush=True)
    root = tk.Tk()
    root.title("NewCar")
    Game(root)
    root.mainloop()
