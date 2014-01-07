plugin_LTLIBRARIES = libgstrrh264parser.la

# sources used to compile this plug-in
libgstrrh264parser_la_SOURCES = gstrrh264parser.c

libgstrrh264parser_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstrrh264parser_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS)
	$(GST_LIBS)
libgstrrh264parser_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrrh264parser_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstrrh264parser.h
