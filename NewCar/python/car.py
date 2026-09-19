"""The whole driving model: keys in, motor duties out.

The ESP32 puts duty on motors and stops when the link goes quiet. Every decision
above that - what a key means, how hard it may accelerate, how tight it may turn,
what turbo does - is here, where changing it costs a rerun instead of a reflash.

Tune the constants and rerun. Nothing needs recompiling and nothing needs flashing.
"""
import math

# ---- chassis -------------------------------------------------------------
TRACK_M = 0.15             # measured: wheel centre to wheel centre
WHEEL_DIAMETER_M = 0.055   # measured
MOTOR_RPM = 500.0          # rated no-load speed, full duty
CRUISE_RPM = 300.0         # what wasd alone asks for; shift lifts it to full

# Top speed is geometry, not a guess: one revolution carries the car pi*d, and the
# motor manages MOTOR_RPM of them a minute - minus whatever the real world takes.
# A loaded motor never reaches its no-load rating and the L298N drops ~2 V getting
# there, so LOAD_FACTOR is the one number here you have to measure: time the car
# over 2 m at full throttle and set it to (measured m/s) / 1.44.
LOAD_FACTOR = 0.75  # CALIBRATE ME
V_MAX = WHEEL_DIAMETER_M * math.pi * MOTOR_RPM / 60.0 * LOAD_FACTOR
CRUISE = CRUISE_RPM / MOTOR_RPM

MOTOR_MIN_DUTY = 0.25       # CALIBRATE: lowest duty that actually turns a wheel
TRIM_LEFT, TRIM_RIGHT = 1.0, 1.0  # CALIBRATE: even out a chassis that veers

# ---- how fast it may gain and lose speed ---------------------------------
ACCEL = 1.20  # ~0.9 s from rest to V_MAX
DECEL = 2.40  # braking may be brisker than launching

# ---- how it is allowed to turn -------------------------------------------
# Two limits shape every corner: below ~0.73 m/s the radius floor binds, so slow
# corners come out tight, and above it lateral grip binds, so fast ones open up.
# Full stick always asks for whichever of the two currently applies, which is what
# makes the steering feel the same at both ends of the speed range.
MIN_TURN_RADIUS = 0.18  # ~1.2x track. Tighter than this scrubs.
MAX_LATERAL = 3.00      # sideways grip before it slides or tips
PIVOT_YAW = 3.00        # 170 deg/s on the spot
PIVOT_FADE = 0.30       # pivot help fades out by this speed

# Yaw is nothing but a wheel-speed difference - w = (vr - vl) / track - so the rate
# a turn can build is the rate the wheels can change speed, 2*a/track. Asking for
# more than this is asking the motors for acceleration they do not have. Unwinding
# is braking one wheel, so a released turn straightens faster than it took to set.
YAW_ACCEL = 2 * ACCEL / TRACK_M   # 16 rad/s2, ~0.19 s to full lock
YAW_RETURN = 2 * DECEL / TRACK_M  # 32 rad/s2, ~0.09 s back to straight

CONTROL_HZ = 100
DT = 1.0 / CONTROL_HZ


def clamp(x, lo, hi):
    return lo if x < lo else hi if x > hi else x


def slew(cur, target, max_delta):
    return cur + clamp(target - cur, -max_delta, max_delta)


def max_yaw(v):
    """How much yaw this speed may have.

    The Ackermann part is w = v / R: a steered axle cannot yaw freely, its rate is
    tied to forward speed by the turn radius, and holding R >= MIN_TURN_RADIUS makes
    a turn an arc instead of a scrub. Grip caps the other end, since lateral
    acceleration is v * w. Standing still there is no arc to follow, so a pivot is
    allowed, faded out by PIVOT_FADE so the allowance does not fall off a cliff as
    the car pulls away.
    """
    speed = abs(v)
    pivot = PIVOT_YAW * clamp(1.0 - speed / PIVOT_FADE, 0.0, 1.0)
    if speed < 1e-3:
        return pivot
    return max(min(speed / MIN_TURN_RADIUS, MAX_LATERAL / speed), pivot)


def deadband(u):
    """Brushed motors do nothing below their stiction duty, which leaves the bottom
    of the range dead. Squash -1..1 into [MOTOR_MIN_DUTY, 1] so small commands move."""
    if abs(u) < 0.01:
        return 0.0
    return math.copysign(MOTOR_MIN_DUTY + (1.0 - MOTOR_MIN_DUTY) * min(abs(u), 1.0), u)


def wheel_speed(duty):
    """Inverse of deadband: duty back to the m/s that wheel was asked for."""
    mag = abs(duty)
    if mag <= MOTOR_MIN_DUTY:
        return 0.0
    return math.copysign((mag - MOTOR_MIN_DUTY) / (1.0 - MOTOR_MIN_DUTY) * V_MAX, duty)


class Car:
    """The controller, plus a plant so the map has something to draw.

    v/w are what the controller intends; x/y/heading are where that would put the
    car. With no encoders the pose is dead reckoning either way, simulated or real.
    """

    def __init__(self):
        self.v = self.w = 0.0          # m/s forward, rad/s yaw (positive = left)
        self.throttle = self.steer = 0.0
        self.turbo = False
        self.duty_left = self.duty_right = 0.0
        self.x = self.y = self.heading = 0.0

    def send(self, keys):
        """The keys currently held, e.g. "wa". Upper case means shift, so "WA" is the
        same turn at full power. "x" or an empty string is neutral."""
        throttle = steer = 0.0
        self.turbo = any(k.isupper() for k in keys)
        for key in keys.lower():
            if key == "w":
                throttle += 1.0
            elif key == "s":
                throttle -= 1.0
            elif key == "a":
                steer -= 1.0
            elif key == "d":
                steer += 1.0
            elif key == "x":
                throttle = steer = 0.0
        self.throttle = clamp(throttle, -1.0, 1.0)
        self.steer = clamp(steer, -1.0, 1.0)

    def tick(self, dt=DT):
        """One control step. Returns the two duties to put on the wire."""
        # The cap rides on throttle alone: steering stays proportional to whatever
        # speed that leaves, so cruise corners exactly as turbo does, only tighter.
        v_target = self.throttle * V_MAX * (1.0 if self.turbo else CRUISE)
        # Steer scales the yaw actually available now, so half stick is half a turn
        # whether crawling or flat out. Right is clockwise, hence negative.
        w_target = -self.steer * max_yaw(self.v)

        # Coming back toward zero, including a commanded reversal, gets the brake
        # rate rather than the accel rate - true of speed and of yaw alike.
        braking = self.v * (v_target - self.v) < 0.0
        unwinding = self.w * (w_target - self.w) < 0.0
        self.v = slew(self.v, v_target, (DECEL if braking else ACCEL) * dt)
        self.w = slew(self.w, w_target, (YAW_RETURN if unwinding else YAW_ACCEL) * dt)

        # Differential kinematics, v +- w*track/2. When a wheel saturates both scale
        # by the SAME factor, which costs speed but preserves the ratio, so the car
        # holds the radius it was asked for instead of straightening under clipping.
        left = self.v - self.w * TRACK_M / 2
        right = self.v + self.w * TRACK_M / 2
        peak = max(abs(left), abs(right))
        if peak > V_MAX:
            left, right = left * V_MAX / peak, right * V_MAX / peak
        self.duty_left = deadband(left / V_MAX) * TRIM_LEFT
        self.duty_right = deadband(right / V_MAX) * TRIM_RIGHT

        self._advance(dt)
        return self.duty_left, self.duty_right

    def packet(self):
        """What goes over BLE: the two duties as whole percentages, "-35 72"."""
        return f"{round(self.duty_left * 100)} {round(self.duty_right * 100)}".encode()

    def _advance(self, dt):
        left, right = wheel_speed(self.duty_left), wheel_speed(self.duty_right)
        v = (left + right) / 2
        w = (right - left) / TRACK_M
        self.x += v * math.cos(self.heading) * dt
        self.y += v * math.sin(self.heading) * dt
        self.heading += w * dt
