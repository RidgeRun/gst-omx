include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
        GST_STATE_IGNORE_ELEMENTS="$(STATE_IGNORE_ELEMENTS)"	\
        $(REGISTRY_ENVIRONMENT)					\
        GST_PLUGIN_SYSTEM_PATH=					\
        GST_PLUGIN_PATH=$(top_builddir)/omx

plugindir = $(libdir)/gstreamer-@GST_API_VERSION@

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# the core dumps of some machines have PIDs appended, test registry and
# profiling data
CLEANFILES = core core.* test-registry.*

SUPPRESSIONS = $(top_srcdir)/common/gst.supp

clean-local: clean-local-check

check_PROGRAMS =				\
	generic/states

# failing tests
noinst_PROGRAMS =

# elements to ignore for the state tests
# STATE_IGNORE_ELEMENTS =

TESTS = $(check_PROGRAMS)

noinst_HEADERS =

EXTRA_DIST = \
	libs/test_transform.c

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS) \
	$(GST_OPTION_CFLAGS) $(GST_CFLAGS) -DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\"" \
	-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

# valgrind testing
# these just need valgrind fixing, period
VALGRIND_TO_FIX =

VALGRIND_IGNORE =


VALGRIND_TESTS_DISABLE = 					\
	$(VALGRIND_IGNORE)					\
	$(VALGRIND_TO_FIX)