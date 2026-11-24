// SPDX-License-Identifier: MIT
// What the status bar shows, read from the same places nz-admin and the battery gauge use:
//   battery   /sys/class/power_supply/<the Battery>/capacity and status (bq27220 on the device)
//   Wi-Fi     wlan0's UP flag (nz-admin's "wifi-power"), operstate up = connected,
//             link quality from /proc/net/wireless
//   USB       the dwc2 driver bound to the SoC's USB controller (nz-admin's "usb-running")
#pragma once

struct status {
	int battery;    /* per cent, or -1 without a fuel gauge */
	int charging;   /* 1 charging, 2 full on external power, 0 on battery */
	int wifi;       /* -1 radio off, 0 on but not connected, 1..4 signal bars */
	int usb;        /* 1 when the USB controller runs */
};

void status_read(struct status *s);
