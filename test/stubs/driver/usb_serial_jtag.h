#pragma once
extern bool fakeUsbHost;
inline bool usb_serial_jtag_is_connected() { return fakeUsbHost; }
