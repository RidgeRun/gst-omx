encodedir = ${prefix}/../pipeline-examples

encode_PROGRAMS = \
		recording

# Common flags
AM_CFLAGS=$(GST_CFLAGS)
AM_LDFLAGS=$(GST_LIBS)
SOURCES=core.c

# Snapshot and recording at 1080p30
recording_SOURCES = recording.h $(SOURCES)
recording_CFLAGS = $(AM_CFLAGS) -DHEADER='"recording.h"' -DNOSTREAM -DNAME='"recording"'
recording_LDFLAGS = $(AM_LDFLAGS)
