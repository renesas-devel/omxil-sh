AM_CPPFLAGS = -I$(srcdir) -I$(top_srcdir)/include

omxshvpudecdir = $(BELLAGIO_PLUGINSDIR)

if HAVE_SHVPU5DEC
shvpudec_la = libomxshvpudec.la
endif

omxshvpudec_LTLIBRARIES = $(shvpudec_la)

libomxshvpudec_la_SOURCES = \
	library_entry_point.c \
	shvpu_avcdec_omx.c shvpu_avcdec_omx.h \
	shvpu_avcdec_parse.c shvpu_avcdec_decode.c \
	shvpu_avcdec_input.c shvpu_avcdec_output.c \
	shvpu_avcdec_uio.c shvpu_avcdec_sync.c \
	shvpu_avcdec_notify.c shvpu_avcdec_log.c \
	shvpu_avcdec_queue.c shvpu_avcdec_bufcalc.c
libomxshvpudec_la_LIBADD = \
	$(OMXIL_LIBS) \
	-L@VPUMW_PATH@/lib -lvpu5decavc \
        -lvpu5deccmn \
        -lvpu5drvcmn \
        -lvpu5drvhg \
        $(UIOMUX_LIBS)
libomxshvpudec_la_CFLAGS = \
	$(OMXIL_CFLAGS) \
	-I@VPUMW_PATH@/include $(UIOMUX_CFLAGS)
libomxshvpudec_la_LDFLAGS = $(OMXIL_LDFLAGS) $(VPU5HG_LDFLAGS)
