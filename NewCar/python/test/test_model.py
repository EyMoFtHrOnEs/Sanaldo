"""Asserts against the firmware's motion code, through the same bridge the game uses.

    uv run test/test_model.py
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))  # car.py sits one level up


from car import CFG, Car, DT, MIN_DUTY, TRACK, V_MAX, _wheel_speed, deadband, max_yaw


def near(a, b, eps=1e-3):
    return abs(a - b) < eps


def drive(car, payload, seconds):
    car.send(payload)
    for _ in range(int(seconds / DT)):
        car.tick()
    return car


def test_keys_mean_what_they_say():
    car = Car()
    car.send("w")
    assert (car.throttle, car.steer) == (1.0, 0.0)
    car.send("s")
    assert car.throttle == -1.0
    car.send("ad")  # both turn keys cancel rather than fighting
    assert car.steer == 0.0
    car.send("wd")
    assert (car.throttle, car.steer) == (1.0, 1.0)
    car.send("x")
    assert (car.throttle, car.steer) == (0.0, 0.0)
    car.send("")  # an empty packet is neutral too
    assert (car.throttle, car.steer) == (0.0, 0.0)


def test_acceleration_is_gradual():
    car = Car()
    car.send("w")
    car.tick()
    assert car.v <= CFG["ACCEL_MPS2"] * DT + 1e-6, "no jack-rabbit start"
    assert car.v > 0.0

    drive(car, "w", 5)
    assert near(car.v, V_MAX, 1e-2)
    assert near(car.duty_left, car.duty_right), "straight line, matched wheels"

    # ...and it takes roughly the distance the accel limit implies, not instantly.
    fresh = Car()
    drive(fresh, "w", V_MAX / CFG["ACCEL_MPS2"] / 2)
    assert fresh.v < V_MAX * 0.75


def test_turns_are_not_sudden():
    car = drive(Car(), "w", 5)
    before = car.w
    car.send("wd")
    car.tick()
    assert abs(car.w - before) <= CFG["YAW_ACCEL_RPS2"] * DT + 1e-6


def test_ackermann_radius_is_respected():
    car = drive(Car(), "wd", 6)
    radius = abs(car.v / car.w)
    assert radius >= CFG["MIN_TURN_RADIUS_M"] - 1e-3, f"turn radius {radius:.3f} m too tight"
    lateral = abs(car.v * car.w)
    assert lateral <= CFG["MAX_LATERAL_MPS2"] + 1e-3, f"{lateral:.2f} m/s2 sideways, it would slide"

    # A saturated outer wheel must not straighten the car out: both duties scale
    # together, so the radius the wheels describe still matches the commanded one.
    vl = _wheel_speed(car.duty_left)
    vr = _wheel_speed(car.duty_right)
    assert near(((vl + vr) / 2) / ((vr - vl) / TRACK), car.v / car.w, 1e-2)


def test_pivot_only_near_standstill():
    assert near(max_yaw(0.0), CFG["PIVOT_YAW_RATE"]), "should turn on the spot when stopped"
    assert max_yaw(CFG["PIVOT_FADE_MPS"]) < CFG["PIVOT_YAW_RATE"], "pivot gone once rolling"
    # Slow corners are held by the radius floor, quick ones by grip, and the two meet.
    assert near(max_yaw(0.3), 0.3 / CFG["MIN_TURN_RADIUS_M"])
    assert near(max_yaw(V_MAX), CFG["MAX_LATERAL_MPS2"] / V_MAX)


def test_deadband_clears_stiction():
    assert near(deadband(0.0), 0.0)
    assert deadband(0.05) >= MIN_DUTY, "a small command must still break stiction"
    assert near(deadband(-1.0), -1.0)


def test_release_coasts_to_a_stop():
    car = drive(Car(), "w", 5)
    drive(car, "x", V_MAX / CFG["DECEL_MPS2"] + 0.5)
    assert near(car.v, 0.0, 1e-3)
    assert (car.duty_left, car.duty_right) == (0.0, 0.0)


def test_it_drives_where_it_is_pointed():
    car = drive(Car(), "w", 3)
    assert car.x > 0.5 and near(car.y, 0.0, 1e-6), "straight means straight"

    car = drive(Car(), "wd", 3)
    assert car.heading < 0.0, "d should steer right, i.e. clockwise"




if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            fn()
            print(f"  ok  {name.removeprefix('test_').replace('_', ' ')}")
    print("\nall good - the car model matches what src/motor.h does")
