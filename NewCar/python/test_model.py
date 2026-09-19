"""Asserts on the driving model in car.py.

    uv run test_model.py
"""
import car
from car import CRUISE_RPM, DT, MOTOR_RPM, TRACK_M, V_MAX, Car, deadband, max_yaw, wheel_speed


def near(a, b, eps=1e-3):
    return abs(a - b) < eps


def drive(vehicle, keys, seconds):
    vehicle.send(keys)
    for _ in range(int(seconds / DT)):
        vehicle.tick()
    return vehicle


def test_keys_mean_what_they_say():
    c = Car()
    c.send("w")
    assert (c.throttle, c.steer) == (1.0, 0.0)
    c.send("s")
    assert c.throttle == -1.0
    c.send("ad")  # both turn keys cancel rather than fighting
    assert c.steer == 0.0
    c.send("wd")
    assert (c.throttle, c.steer) == (1.0, 1.0)
    c.send("x")
    assert (c.throttle, c.steer) == (0.0, 0.0)
    c.send("")  # an empty packet is neutral too
    assert (c.throttle, c.steer) == (0.0, 0.0)


def test_shift_is_turbo():
    cruise = drive(Car(), "w", 5)
    turbo = drive(Car(), "W", 5)
    assert near(turbo.v, V_MAX, 1e-2), "shift gives the full 500 rpm"
    assert near(cruise.v, V_MAX * CRUISE_RPM / MOTOR_RPM, 1e-2), "wasd alone is 300"

    c = Car()
    c.send("D")  # any upper-case letter is shift, steering included
    assert c.turbo and c.steer == 1.0
    c.send("wd")
    assert not c.turbo

    # The cap is on throttle, not on the turn: cruise still corners to the floor.
    cornering = drive(Car(), "wd", 6)
    assert near(abs(cornering.v / cornering.w), car.MIN_TURN_RADIUS, 1e-2)


def test_acceleration_is_gradual():
    c = Car()
    c.send("W")
    c.tick()
    assert 0.0 < c.v <= car.ACCEL * DT + 1e-6, "no jack-rabbit start"

    drive(c, "W", 5)
    assert near(c.v, V_MAX, 1e-2)
    assert near(c.duty_left, c.duty_right), "straight line, matched wheels"

    half = drive(Car(), "W", V_MAX / car.ACCEL / 2)
    assert half.v < V_MAX * 0.75, "the ramp is a real limit, not a formality"


def test_turns_are_not_sudden():
    c = drive(Car(), "w", 5)
    before = c.w
    c.send("wd")
    c.tick()
    assert abs(c.w - before) <= car.YAW_ACCEL * DT + 1e-6

    # Wheels can only change speed so fast, and yaw is their difference: asking for
    # more yaw acceleration than 2*ACCEL/TRACK would be asking for torque that is
    # not there.
    assert near(car.YAW_ACCEL, 2 * car.ACCEL / TRACK_M, 1e-2)


def test_letting_go_straightens_faster_than_turning_in():
    c = drive(Car(), "wd", 3)
    locked = c.w
    c.send("w")
    c.tick()
    assert abs(c.w) < abs(locked), "must start unwinding at once"
    assert abs(c.w - locked) > car.YAW_ACCEL * DT, "and quicker than it wound up"


def test_ackermann_radius_is_respected():
    c = drive(Car(), "WD", 6)  # turbo, because that is where grip is the binding limit
    radius = abs(c.v / c.w)
    assert radius >= car.MIN_TURN_RADIUS - 1e-3, f"turn radius {radius:.3f} m too tight"
    lateral = abs(c.v * c.w)
    assert lateral <= car.MAX_LATERAL + 1e-3, f"{lateral:.2f} m/s2 sideways, it would slide"

    # A saturated outer wheel must not straighten the car out: both duties scale
    # together, so the radius the wheels describe still matches the commanded one.
    left, right = wheel_speed(c.duty_left), wheel_speed(c.duty_right)
    assert near(((left + right) / 2) / ((right - left) / TRACK_M), c.v / c.w, 1e-2)


def test_pivot_only_near_standstill():
    assert near(max_yaw(0.0), car.PIVOT_YAW), "should turn on the spot when stopped"
    assert max_yaw(car.PIVOT_FADE) < car.PIVOT_YAW, "pivot gone once rolling"
    # Slow corners are held by the radius floor, quick ones by grip, and the two meet.
    assert near(max_yaw(0.3), 0.3 / car.MIN_TURN_RADIUS)
    assert near(max_yaw(V_MAX), car.MAX_LATERAL / V_MAX)


def test_deadband_clears_stiction():
    assert near(deadband(0.0), 0.0)
    assert deadband(0.05) >= car.MOTOR_MIN_DUTY, "a small command must still break stiction"
    assert near(deadband(-1.0), -1.0)
    assert near(wheel_speed(deadband(0.4)), 0.4 * V_MAX), "and the inverse must round-trip"


def test_release_coasts_to_a_stop():
    c = drive(Car(), "W", 5)
    drive(c, "x", V_MAX / car.DECEL + 0.5)
    assert near(c.v, 0.0, 1e-3)
    assert (c.duty_left, c.duty_right) == (0.0, 0.0)


def test_it_drives_where_it_is_pointed():
    c = drive(Car(), "w", 3)
    assert c.x > 0.5 and near(c.y, 0.0, 1e-6), "straight means straight"

    c = drive(Car(), "wd", 3)
    assert c.heading < 0.0, "d should steer right, i.e. clockwise"


def test_the_packet_is_what_the_esp32_expects():
    c = Car()
    assert c.packet() == b"0 0", "stopped is two zeroes, not an empty write"

    drive(c, "WD", 6)
    left, right = c.packet().decode().split()
    assert int(left) == round(c.duty_left * 100)
    assert int(right) == round(c.duty_right * 100)
    assert all(-100 <= int(v) <= 100 for v in (left, right)), "duty is a percentage"
    assert len(c.packet()) <= 9, "must fit a default 20-byte BLE write"


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            fn()
            print(f"  ok  {name.removeprefix('test_').replace('_', ' ')}")
    print("\nall good - the driving model behaves")
