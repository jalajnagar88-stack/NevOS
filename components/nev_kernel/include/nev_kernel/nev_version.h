/* NEVOS L1 — the firmware version, in one place. */
#ifndef NEV_KERNEL_NEV_VERSION_H
#define NEV_KERNEL_NEV_VERSION_H

/*
 * Shown in the about screen, sent to the daemon in `hello`, and compared
 * against an OTA offer. A version that lived in three files would disagree
 * with itself the first time one of them was updated.
 */
#define NEVOS_VERSION       "0.1.0"
#define NEVOS_VERSION_MAJOR 0
#define NEVOS_VERSION_MINOR 1
#define NEVOS_VERSION_PATCH 0

#endif /* NEV_KERNEL_NEV_VERSION_H */
