AM_CPPFLAGS = -I$(srcdir) -I$(top_srcdir)/include

omxshvpu5avcdir = $(BELLAGIO_PLUGINSDIR)

if HAVE_SHVPU5DEC
shvpu5avc_la = libomxshvpu5avc.la
endif

VPU5_LIBS = -L@VPUMW_PATH@/lib \
	-lvpu5encavc -lvpu5enccmn -lvpu5drvcmn -lvpu5drvhg
VPU5_CFLAGS = -I@VPUMW_PATH@/include \
	-DVPU5HG_FIRMWARE_PATH=\"@VPUMW_PATH@/lib/firmware\"

if USE_IPMMU
VPU5_CFLAGS += -DIPMMU_ENABLE $(MERAM_CFLAGS)
VPU5_LIBS += $(MERAM_LIBS)
endif
if USE_MERAM
VPU5_CFLAGS += -DMERAM_ENABLE $(MERAM_CFLAGS)
VPU5_LIBS += $(MERAM_LIBS)
endif

omxshvpu5avc_LTLIBRARIES = $(shvpu5avc_la)

libomxshvpu5avc_la_SOURCES = \
	library_entry_point.c shvpu5_common_driver.c \
	shvpu5_common_log.c shvpu5_common_queue.c \
	shvpu5_common_uio.c shvpu5_common_sync.c \
	shvpu5_avcdec_omx.c shvpu5_avcdec_omx.h \
	shvpu5_avcdec_parse.c shvpu5_avcdec_decode.c \
	shvpu5_avcdec_input.c shvpu5_avcdec_output.c \
	shvpu5_avcdec_notify.c \
	shvpu5_common_ipmmu.c shvpu5_common_udfio.c \
	shvpu5_avcdec_meram.c \
	shvpu5_avcdec_meram.h \
	shvpu5_avcdec.h \
	shvpu5_avcenc_omx.c shvpu5_avcenc_omx.h \
	shvpu5_avcenc_encode.c shvpu5_avcenc_output.c \
	shvpu5_avcenc.h
libomxshvpu5avc_la_LIBADD = \
	$(OMXIL_LIBS) $(VPU5_LIBS) $(UIOMUX_LIBS)
libomxshvpu5avc_la_CFLAGS = \
	$(OMXIL_CFLAGS) $(VPU5_CFLAGS) $(UIOMUX_CFLAGS)
libomxshvpu5avc_la_LDFLAGS = $(OMXIL_LDFLAGS) $(VPU5HG_LDFLAGS)
