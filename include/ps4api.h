/*
 * PS4 Save Remote — LAN API (headless agent)
 * Forked from Apollo Save Tool guts; GPL-3.0.
 */
#ifndef PS4API_H
#define PS4API_H

#include <stdint.h>

#define PS4API_PORT           8741
#define PS4API_VERSION        "1.0.0"
#define PS4API_ROOT           "/data/ps4save-remote/"
#define PS4API_INBOX          PS4API_ROOT "inbox/"
#define PS4API_OUTBOX         PS4API_ROOT "outbox/"
#define PS4API_WORK           PS4API_ROOT "work/"
#define PS4API_FAKEUSB_SAVE   "/data/fakeusb/PS4/SAVEDATA/"

/* Start non-blocking LAN API after jailbreak init. Returns 1 on success. */
int ps4api_start(void);
void ps4api_stop(void);
int ps4api_is_running(void);

/* Filled after start; empty string if unknown. */
const char* ps4api_ip(void);
int ps4api_port(void);
const char* ps4api_status(void);

#endif /* PS4API_H */
