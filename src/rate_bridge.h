/* Internals of rate_bridge.c that the unit tests call. Not exported from the .so (built with -fvisibility=hidden). */
#pragma once
#include <stddef.h>

/* "hw:<card>[,<dev>]", "hw:CARD=<id>[,DEV=<n>]" or "hw:<id>[,<dev>]" -> 1 with card/dev (id copied when named), else 0. */
int rb_parse_hw_name(const char *name, int *card, int *dev, char *id, size_t idn);

/* From a stream0 text, for the playback (capture=0) or capture section: has44 = 44100 listed; best = 48000 if listed, else the
 * first rate. Returns the number of Rates lines in that section. */
int rb_stream_rates(const char *stream0, int capture, int *has44, unsigned *best);

/* From an MPC.settings text: when <DEVICESETUP>'s audioOutputDeviceName or audioInputDeviceName (XML-escaped value, n bytes) is
 * one bridged() accepts, *out = a malloc'd copy with both values emptied and 1 is returned; else 0 (-1: out of memory). */
int rb_reset_saved_device(const char *in, char **out, int (*bridged)(const char *, size_t));
