# NewCar — controller + simulator

```
uv run main.py        # the window: drive immediately, no hardware
uv run test_model.py  # assert the driving model behaves
```

| key | action |
|-----|--------|
| `w` / `s` | forward / reverse |
| `a` / `d` | left / right |
| `shift`   | turbo — 500 rpm instead of the 300 rpm cruise |
| `c`       | connect / disconnect BLE |
| `r`       | reset |
| `Esc`     | quit |

**`car.py` is the car.** Ramps, turn limits, cruise and turbo, what a key means —
all of it is here, in plain Python. The ESP32 only puts duty on motors and stops
when the link goes quiet, so tuning is a rerun, never a reflash.

The window needs nothing else: no car, no radio, not even bleak installed. BLE runs
on a background thread and narrates itself on the terminal you launched from. Once
connected, the duties the model produces go out at 20 Hz, so what the window draws
is what the hardware is doing rather than a separate guess at it.

## Wire protocol

Two whole percentages, left then right, as ASCII: `-35 72`. Write it to the Nordic
UART RX characteristic — nRF Connect or any BLE terminal will drive the car by hand.
Nothing streams back: with no encoders the board has nothing to report that the
controller did not already tell it.

## Calibrating

1. `LOAD_FACTOR` — time it over 2 m at full throttle, set to `measured / 1.44`.
2. `MOTOR_MIN_DUTY` — lowest duty where the wheels actually turn.
3. `TRIM_LEFT` / `TRIM_RIGHT` — drive straight, correct the veer.
