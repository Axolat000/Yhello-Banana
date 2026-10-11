#ifndef ACPI_H
#define ACPI_H

/* Power the machine off the ACPI way (the S5 sleep state, read from the
 * firmware's FADT and DSDT). Returns only if that did not work. */
void acpi_s5_poweroff(void);
/* the power button shuts the computer down (a task watching it) */
void acpi_power_button_init(void);
/* the firmware's DSDT / SSDTs declare a device with this _HID ("APP0001") */
int  acpi_has_device(const char* hid);

#endif
