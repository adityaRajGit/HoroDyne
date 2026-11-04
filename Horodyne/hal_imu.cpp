#include "hal_imu.h"
#include "config.h"
#include <Wire.h>
#include <ImuDrv.hpp>   // SensorLib 0.5

// Which QMI8658 interrupt line reaches GPIO38. Waveshare's docs name both INT1
// and INT2 for it; if raise-to-wake never fires, change this to PIN1.
#define IMU_WAKE_PIN SensorQMI8658::IntPin::PIN2

static const uint8_t REG_CTRL8 = 0x09, REG_STATUS1 = 0x2F;

static SensorQMI8658 imu;
static bool ok = false;
uint16_t imuRestarts = 0;

static bool regRead(uint8_t r, uint8_t &v) {
    Wire.beginTransmission(ADDR_IMU); Wire.write(r);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)ADDR_IMU, (uint8_t)1) != 1) return false;
    v = Wire.read();
    return true;
}
static void regWrite(uint8_t r, uint8_t v) {
    Wire.beginTransmission(ADDR_IMU); Wire.write(r); Wire.write(v); Wire.endTransmission();
}

// The chip's own pedometer never counts on this board (four walk tests 2026-10-26, and Waveshare
// issue #109), so steps are counted in software (svc_activity.cpp) from samples the chip buffers
// in its FIFO. The any-motion detector runs alongside for raise-to-wake. Configured once, all day.
bool imuBegin() {
    pinMode(PIN_IMU_INT, INPUT_PULLUP);
    if (!imu.begin(Wire, ADDR_IMU, PIN_I2C_SDA, PIN_I2C_SCL)) return false;
    // Accel-only ODR code 8 = 31.25 Hz; SensorLib's table names that code by its 6DOF rate, 28 Hz.
    bool acc = imu.configAccel(AccelFullScaleRange::FS_4G, 28.0f) && imu.enableAccel();
    bool mot = imu.configMotionDetect(SensorQMI8658::MotionType::ANY_MOTION,
                                      WOM_THRESHOLD_MG, WOM_THRESHOLD_MG, WOM_THRESHOLD_MG, 4)
            && imu.enableMotionDetect(IMU_WAKE_PIN);
    // SensorLib always routes motion events to INT1 (CTRL8 bit 6); clear it for INT2.
    // A failed read must not write 0 back: that switches every motion engine off.
    uint8_t c8;
    if (IMU_WAKE_PIN == SensorQMI8658::IntPin::PIN2) {
        if (regRead(REG_CTRL8, c8)) regWrite(REG_CTRL8, c8 & ~0x40);
        else mot = false;
    }
    // Stream mode keeps the newest 128 samples (~4 s): the sleeping watch drains it every STEP_POLL_MS.
    bool fifo = imu.configFifo(SensorQMI8658::FifoMode::STREAM, SensorQMI8658::FifoSamples::SAMPLES_128);
    imu.disableGyro();
    Serial.printf("imu: accel %s, step fifo %s, motion wake %s\n",
                  acc ? "ok" : "FAILED", fifo ? "ok" : "FAILED", mot ? "ok" : "FAILED");
    ok = acc;
    return acc && fifo;
}

bool imuReadAccel(float &x, float &y, float &z) {
    if (!ok) return false;
    AccelerometerData d;
    if (!imu.readAccel(d)) return false;
    const float G = 9.80665f;
    x = d.mps2.x / G;
    y = d.mps2.y / G;
    z = d.mps2.z / G;
    return true;
}

bool imuFacingUp() {
    float a[3];
    if (!imuReadAccel(a[0], a[1], a[2])) return true;   // can't tell: wake rather than miss a glance
    return a[IMU_FACE_AXIS] * IMU_FACE_SIGN >= RAISE_MIN_G;
}

// Motion detection runs all the time; the ESP32 decides whether GPIO38 may wake it.
// The motion interrupt stays latched until STATUS1 is read, so clear it before each sleep.
void imuArmWakeOnMotion() {
    uint8_t s;
    if (ok) regRead(REG_STATUS1, s);
}

void imuDisarmWakeOnMotion() {}

int imuReadSamples(float *g, int max) {
    if (!ok) return -1;
    static AccelerometerData d[128];
    int n = imu.readFromFifo(d, 128, nullptr, 0);
    // A read that fails halfway leaves the chip in FIFO read mode (FIFO_CTRL bit 7) with a torn
    // count: it never fills again. Data waiting but nothing read = jammed. Samples arrive 31 times
    // a second, so three empty reads in a row mean the same. Either way, start the FIFO over.
    static uint8_t empty = 0;
    uint8_t st;
    empty = n ? 0 : empty + 1;
    if (n == 0 && (empty >= 3 || (regRead(0x16, st) && (st & 0x10)))) {
        empty = 0;
        imuRestarts++;
        imu.enableAccel();
        imu.configFifo(SensorQMI8658::FifoMode::STREAM, SensorQMI8658::FifoSamples::SAMPLES_128);
    }
    if (n > max) n = max;
    const float G = 9.80665f;
    for (int i = 0; i < n; i++)
        g[i] = sqrtf(d[i].mps2.x * d[i].mps2.x + d[i].mps2.y * d[i].mps2.y + d[i].mps2.z * d[i].mps2.z) / G;
    return n;
}

