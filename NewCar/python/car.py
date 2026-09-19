"""Loads the firmware's own motion code.

The game and the tests both go through here, so what they exercise is the
C++ in src/motor.h rather than a Python re-implementation that would quietly
drift from the car. The shared library is rebuilt whenever the sources change.
"""
import ctypes
import math
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE.parent / "src"
LIB = HERE / ("motor.dll" if sys.platform == "win32" else "libmotor.so")
SOURCES = [HERE / "shim.cpp", SRC / "motor.h", SRC / "config.h"]


def _build():
    newest = max(p.stat().st_mtime for p in SOURCES)
    if LIB.exists() and LIB.stat().st_mtime >= newest:
        return
    cmd = ["g++", "-O2", "-std=c++17", "-shared", "-I", str(SRC),
           str(HERE / "shim.cpp"), "-o", str(LIB)]
    try:
        subprocess.run(cmd, check=True)
    except FileNotFoundError:
        sys.exit("g++ not found - install one (scoop install mingw) and retry")


_build()
_lib = ctypes.CDLL(str(LIB))
F, PF = ctypes.c_float, ctypes.POINTER(ctypes.c_float)
_lib.motor_from_keys.argtypes = [ctypes.c_char_p, ctypes.c_int, PF, PF]
_lib.motor_tick.argtypes = [PF, PF, F, F, F, PF, PF]
_lib.motor_max_yaw.argtypes = [F]
_lib.motor_max_yaw.restype = F
_lib.motor_deadband.argtypes = [F]
_lib.motor_deadband.restype = F
_lib.motor_config_name.argtypes = [ctypes.c_int]
_lib.motor_config_name.restype = ctypes.c_char_p
_lib.motor_config_value.argtypes = [ctypes.c_int]
_lib.motor_config_value.restype = F

# Straight out of config.h as the compiler saw it, derived values and all.
CFG = {_lib.motor_config_name(i).decode(): _lib.motor_config_value(i)
       for i in range(_lib.motor_config_count())}
TRACK = CFG["TRACK_M"]
V_MAX = CFG["V_MAX_MPS"]
MIN_DUTY = CFG["MOTOR_MIN_DUTY"]
DT = 1.0 / CFG["CONTROL_HZ"]

max_yaw = _lib.motor_max_yaw
deadband = _lib.motor_deadband


class Car:
    """The firmware's controller state, plus a plant to push it around on.

    Wheel speed is recovered from duty by inverting motor::deadband, which is what
    a correctly calibrated MOTOR_MIN_DUTY means: set it wrong in config.h and the
    simulated car mistracks in the same direction the real one will.
    """

    def __init__(self):
        self.v = self.w = 0.0
        self.throttle = self.steer = 0.0
        self.duty_left = self.duty_right = 0.0
        self.x = self.y = self.heading = 0.0

    def send(self, payload: str):
        """Feed the car a packet, byte for byte what goes over BLE."""
        th, st = F(), F()
        raw = payload.encode()
        _lib.motor_from_keys(raw, len(raw), th, st)
        self.throttle, self.steer = th.value, st.value

    def tick(self, dt=DT):
        v, w, left, right = F(self.v), F(self.w), F(), F()
        _lib.motor_tick(v, w, self.throttle, self.steer, dt, left, right)
        self.v, self.w = v.value, w.value
        self.duty_left, self.duty_right = left.value, right.value
        self._advance(dt)
        return self.duty_left, self.duty_right

    def _advance(self, dt):
        vl, vr = _wheel_speed(self.duty_left), _wheel_speed(self.duty_right)
        v = (vl + vr) / 2.0
        w = (vr - vl) / TRACK
        self.x += v * math.cos(self.heading) * dt
        self.y += v * math.sin(self.heading) * dt
        self.heading += w * dt
        return v, w


def _wheel_speed(duty):
    """Inverse of motor::deadband: duty back to the m/s the wheel was asked for."""
    mag = abs(duty)
    if mag <= MIN_DUTY:
        return 0.0
    speed = (mag - MIN_DUTY) / (1.0 - MIN_DUTY) * V_MAX
    return speed if duty > 0 else -speed
