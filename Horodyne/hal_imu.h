#pragma once
// QMI8658 accelerometer: Level readings, step samples (FIFO), wake-on-motion.
#include <Arduino.h>

bool     imuBegin();                              // false if the chip doesn't answer
bool     imuReadAccel(float &x, float &y, float &z); // in g
bool     imuFacingUp();                           // for raise-to-wake (see IMU_FACE_* in config.h)
void     imuArmWakeOnMotion();                    // before light sleep
void     imuDisarmWakeOnMotion();                 // after waking
extern uint16_t imuRestarts;                      // times the FIFO had to be started again
int      imuReadSamples(float *g, int max);       // buffered samples as |a| in g, oldest first; -1 = no IMU
void     imuDebugPrint();                         // "imu" over serial: axes, wake line and motion registers