"""NewCar - a simulated car you can drive, and a real one when there is one.

    uv run main.py

The window is the simulator and it needs nothing else: no car, no radio, not even
bleak installed. Drive it with wasd the moment it opens.

Meanwhile a background thread tries the real car over BLE and narrates itself on
the terminal you launched from, so the window never blocks on a radio. Connect,
and the same keys stream to the hardware while its telemetry comes back as a
purple ghost on the map. Ghost drifting away from the simulated car means the
calibration in src/config.h is off - that is what the two traces are for.

Everything under the car is src/motor.h, linked through car.py. Nothing about how
this thing drives is written twice.
"""
import asyncio
import math
import threading
import tkinter as tk
from time import perf_counter

from car import CFG, Car, DT, TRACK

try:
    from bleak import BleakClient, BleakScanner
except ImportError:  # the simulator has no use for it
    BleakClient = BleakScanner = None

DEVICE = "NewCar"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # controller -> car
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # car -> controller
SEND_HZ = 20

W, H = 1000, 680
SCALE = 170  # pixels per metre
KEYS = set("wasd")
TRAIL_MAX = 3000
BG, INK, DIM, ACCENT, GHOST = "#11151c", "#e8eef5", "#2a3340", "#4fd1c5", "#c792ea"


class Link:
    """BLE on its own thread, reporting to the terminal. The UI only reads attributes.

    Nothing here can stall the window: the worst case is a scan that finds nothing
    and goes back to idle.
    """

    def __init__(self, auto=True):
        self.status = "idle"
        self.payload = "x"
        self.telemetry = None  # (v, w, duty_left, duty_right) as the car reports it
        self._wanted = auto and BleakScanner is not None
        if BleakScanner is None:
            self.status = "bleak not installed - simulator only"
            print(f"[ble] {self.status}", flush=True)
            return
        threading.Thread(target=lambda: asyncio.run(self._loop()), daemon=True).start()

    def toggle(self):
        if BleakScanner is None:
            return
        self._wanted = not self._wanted
        if not self._wanted:
            self._say("disconnecting")

    @property
    def live(self):
        return self.status == "connected"

    def _say(self, status):
        """One line per change of state; telemetry rewrites its own line underneath."""
        if status != self.status:
            self.status = status
            print(f"\n[ble] {status}", flush=True)

    def _notify(self, _sender, data):
        try:
            v, w, left, right = data.decode().split()
            self.telemetry = (float(v), float(w), int(left) / 100, int(right) / 100)
        except ValueError:
            return  # a truncated notification is not worth dropping the link over
        print(f"\r[car] {self.telemetry[0]:+.2f} m/s  {self.telemetry[1]:+.2f} rad/s"
              f"  L{self.telemetry[2]:+.2f} R{self.telemetry[3]:+.2f}   ", end="", flush=True)

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
                    await client.start_notify(NUS_TX, self._notify)
                    self._say("connected")
                    while self._wanted and client.is_connected:
                        await client.write_gatt_char(NUS_RX, self.payload.encode(), response=False)
                        await asyncio.sleep(1 / SEND_HZ)
                    await client.write_gatt_char(NUS_RX, b"x", response=False)
            except Exception as err:  # a dropped link is ordinary; say so and idle
                self._say(f"lost the link ({type(err).__name__})")
            self._wanted = False
            self.telemetry = None


class Game:
    def __init__(self, root, link=None):
        self.root = root
        self.canvas = tk.Canvas(root, width=W, height=H, bg=BG, highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        self.held = set()
        self.link = link if link is not None else Link()
        self.reset()

        root.bind("<KeyPress>", self.press)
        root.bind("<KeyRelease>", self.release)
        self.last = perf_counter()
        self.backlog = 0.0
        self.frame()

    def reset(self):
        self.car = Car()
        self.trail = []
        self.ghost = None  # (x, y, heading), dead-reckoned from what the car reports
        self.ghost_trail = []
        self.ghost_clock = None

    # --- input -------------------------------------------------------------
    def press(self, event):
        key = event.keysym.lower()
        if key == "escape":
            self.root.destroy()
        elif key == "r":
            self.reset()
        elif key == "c":
            self.link.toggle()
        elif key in KEYS:
            self.held.add(key)

    def release(self, event):
        self.held.discard(event.keysym.lower())

    # --- loop --------------------------------------------------------------
    def frame(self):
        now = perf_counter()
        self.backlog = min(self.backlog + now - self.last, 0.25)  # a slow frame must not fast-forward
        self.last = now

        payload = "".join(sorted(self.held)) or "x"
        self.link.payload = payload  # the BLE thread sends this at its own 20 Hz
        self.car.send(payload)
        while self.backlog >= DT:
            self.car.tick()
            self.backlog -= DT
            if not self.trail or math.dist(self.trail[-1], (self.car.x, self.car.y)) > 0.01:
                self.trail.append((self.car.x, self.car.y))
        del self.trail[:-TRAIL_MAX]

        self.follow_ghost(now)
        self.draw()
        self.root.after(16, self.frame)

    def follow_ghost(self, now):
        """Dead-reckon the real car from its 5 Hz telemetry. Coarse, but enough to
        show it drifting away from the simulated one."""
        if not self.link.live or self.link.telemetry is None:
            self.ghost_clock = None
            return
        if self.ghost is None:
            self.ghost = (self.car.x, self.car.y, self.car.heading)
        elapsed = 0.0 if self.ghost_clock is None else now - self.ghost_clock
        self.ghost_clock = now
        v, w = self.link.telemetry[0], self.link.telemetry[1]
        x, y, heading = self.ghost
        self.ghost = (x + v * math.cos(heading) * elapsed,
                      y + v * math.sin(heading) * elapsed,
                      heading + w * elapsed)
        if not self.ghost_trail or math.dist(self.ghost_trail[-1], self.ghost[:2]) > 0.01:
            self.ghost_trail.append(self.ghost[:2])
        del self.ghost_trail[:-TRAIL_MAX]

    # --- drawing -----------------------------------------------------------
    def to_screen(self, x, y):
        return W / 2 + (x - self.car.x) * SCALE, H / 2 - (y - self.car.y) * SCALE

    def draw(self):
        self.canvas.delete("all")
        self.grid()
        self.turn_circle()
        self.path(self.trail, "#31506b")
        self.path(self.ghost_trail, "#5b4a73")
        if self.ghost:
            self.body(*self.ghost, outline=GHOST, fill="", duties=None)
        self.body(self.car.x, self.car.y, self.car.heading, outline=ACCENT, fill="#1d2b3a",
                  duties=(self.car.duty_left, self.car.duty_right))
        self.hud()

    def path(self, points, colour):
        if len(points) > 1:
            self.canvas.create_line(*[v for p in points for v in self.to_screen(*p)],
                                    fill=colour, width=2)

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
        car = self.car
        if abs(car.w) < 1e-3 or abs(car.v) < 1e-3:
            return
        radius = car.v / car.w
        r = abs(radius) * SCALE
        if r > 4 * SCALE:
            return
        sx, sy = self.to_screen(car.x - radius * math.sin(car.heading),
                                car.y + radius * math.cos(car.heading))
        tight = abs(radius) <= CFG["MIN_TURN_RADIUS_M"] + 0.01
        self.canvas.create_oval(sx - r, sy - r, sx + r, sy + r,
                                outline="#e07b53" if tight else "#3d6b8f", dash=(4, 4))

    def body(self, x, y, heading, outline, fill, duties):
        cos, sin = math.cos(heading), math.sin(heading)

        def place(fwd, left):
            return self.to_screen(x + fwd * cos - left * sin, y + fwd * sin + left * cos)

        hull = [place(0.15, 0), place(0.06, 0.085), place(-0.10, 0.085),
                place(-0.10, -0.085), place(0.06, -0.085)]
        self.canvas.create_polygon([v for p in hull for v in p], fill=fill, outline=outline,
                                   width=2, dash=() if duties else (5, 3))
        if duties is None:
            return
        for side, duty in ((1, duties[0]), (-1, duties[1])):
            corners = [place(f, side * TRACK / 2 + s * 0.018)
                       for f, s in ((0.05, 1), (0.05, -1), (-0.05, -1), (-0.05, 1))]
            colour = "#4ade80" if duty > 0 else ("#f87171" if duty < 0 else "#64748b")
            self.canvas.create_polygon([v for p in corners for v in p], fill=colour)

    def hud(self):
        car = self.car
        turning = abs(car.w) > 1e-3 and abs(car.v) > 1e-3
        radius = abs(car.v / car.w) if turning else float("inf")
        rows = [
            f"speed      {car.v:+.2f} m/s   of {CFG['V_MAX_MPS']:.2f} max",
            f"yaw        {car.w:+.2f} rad/s",
            f"radius     {radius:6.2f} m   floor {CFG['MIN_TURN_RADIUS_M']:.2f}",
            f"duty       L {car.duty_left:+.2f}   R {car.duty_right:+.2f}",
        ]
        for i, row in enumerate(rows):
            self.canvas.create_text(18, 22 + i * 20, anchor="w", fill=INK,
                                    font=("Consolas", 11), text=row)

        link = self.link
        self.canvas.create_text(W - 18, 22, anchor="e", fill=GHOST if link.live else DIM,
                                font=("Consolas", 11), text=f"BLE  {link.status}")
        if link.telemetry:
            v, w, left, right = link.telemetry
            self.canvas.create_text(
                W - 18, 42, anchor="e", fill=GHOST, font=("Consolas", 11),
                text=f"car  {v:+.2f} m/s  {w:+.2f} rad/s   drift {v - car.v:+.2f}")

        self.canvas.create_text(18, H - 42, anchor="w", fill=ACCENT, font=("Consolas", 11),
                                text="w a s d  drive     c  connect     r  reset     esc  quit")
        self.canvas.create_text(18, H - 20, anchor="w", fill=INK, font=("Consolas", 11),
                                text="held: " + ("".join(sorted(self.held)) or "-"))


if __name__ == "__main__":
    print("[sim] window is live - wasd to drive, no hardware required", flush=True)
    root = tk.Tk()
    root.title("NewCar")
    Game(root)
    root.mainloop()
