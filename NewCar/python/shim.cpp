// Thin C entry points over ../../src/motor.h so Python can link the firmware's
// own motion code. No logic here: anything added below would be a second
// implementation waiting to disagree with the car.
#include "motor.h"

#ifdef _WIN32
#define API extern "C" __declspec(dllexport)
#else
#define API extern "C" __attribute__((visibility("default")))
#endif

API void motor_from_keys(const char *keys, int n, float *throttle, float *steer) {
  motor::Command c;
  motor::fromKeys(keys, (size_t)n, c);
  *throttle = c.throttle;
  *steer = c.steer;
}

// One control tick, same call the drive task makes.
API void motor_tick(float *v, float *w, float throttle, float steer, float dt,
                    float *dutyLeft, float *dutyRight) {
  motor::State s{*v, *w};
  motor::Command c{throttle, steer};
  motor::tick(s, c, dt, *dutyLeft, *dutyRight);
  *v = s.v;
  *w = s.w;
}

API float motor_max_yaw(float v) { return motor::maxYaw(v); }
API float motor_deadband(float u) { return motor::deadband(u); }

// Hand the tunables to Python by name, so derived values like V_MAX_MPS - which no
// amount of regexing config.h would evaluate - arrive already computed.
#define ENTRY(x) {#x, (float)(x)}
static const struct { const char *name; float value; } kConfig[] = {
    ENTRY(TRACK_M),        ENTRY(WHEEL_DIAMETER_M), ENTRY(MOTOR_RPM),
    ENTRY(LOAD_FACTOR),    ENTRY(V_MAX_MPS),        ENTRY(MOTOR_MIN_DUTY),
    ENTRY(MIN_TURN_RADIUS_M), ENTRY(MAX_LATERAL_MPS2), ENTRY(ACCEL_MPS2),
    ENTRY(DECEL_MPS2),     ENTRY(YAW_ACCEL_RPS2),   ENTRY(PIVOT_YAW_RATE),
    ENTRY(PIVOT_FADE_MPS), ENTRY(CONTROL_HZ),       ENTRY(CMD_TIMEOUT_MS),
    ENTRY(TRIM_LEFT),      ENTRY(TRIM_RIGHT),
};

API int motor_config_count() { return (int)(sizeof(kConfig) / sizeof(kConfig[0])); }
API const char *motor_config_name(int i) { return kConfig[i].name; }
API float motor_config_value(int i) { return kConfig[i].value; }
