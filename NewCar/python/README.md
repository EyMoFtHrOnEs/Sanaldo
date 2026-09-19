# NewCar — simulator + controller

```
uv run main.py             # the window: drive immediately, no hardware
uv run test/test_model.py  # assert the motion model behaves
```

| key | action |
|-----|--------|
| `w` / `s` | forward / reverse |
| `a` / `d` | left / right |
| `c`       | connect / disconnect BLE |
| `r`       | reset the simulated car |
| `Esc`     | quit |

The window is the simulator and needs nothing else — no car, no radio, not even
bleak installed. BLE lives on a background thread and narrates itself on the
terminal you launched from, so the window never waits on a scan. Once connected
the same keys stream to the car at 20 Hz and its telemetry comes back as a purple
ghost; ghost drifting from the simulated car means `LOAD_FACTOR` in `src/config.h`
is off, which is what the two traces are for.

None of this reimplements the car. `shim.cpp` compiles `../src/motor.h` into a
shared library and `car.py` calls it, so the ramps, the turn-radius limit and the
deadband on screen are the ones that ship. Edit `src/config.h` and the library
rebuilds itself on the next run. Needs `g++` on PATH (`scoop install mingw`).

The window owns the keyboard, so it has to be focused — and if it loses focus
mid-turn `KeyRelease` never arrives, so the car holds its last command until the
400 ms failsafe on the ESP32 stops it.
