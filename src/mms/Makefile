PLUGIN = mms${PLUGIN_SUFFIX}

SRCS = mms.cc

include ../../buildsys.mk
include ../../extra.mk

plugindir := ${plugindir}/${TRANSPORT_PLUGIN_DIR}

LD = ${CXX}

CPPFLAGS += ${PLUGIN_CPPFLAGS} ${MMS_CFLAGS} -I../.. -Wall
CFLAGS += ${PLUGIN_CFLAGS}
LIBS += ${GLIB_LIBS} ${MMS_LIBS}
