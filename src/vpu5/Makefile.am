AM_CPPFLAGS = -I$(srcdir) -I$(top_srcdir)/include

omxshvpu5avcdir = $(BELLAGIO_PLUGINSDIR)

if HAVE_SHVPU5DEC
shvpu5avc_la = libomxshvpu5avc.la
endif

VPU5_LIBS = -L@VPUMW_PATH@/lib \
	@VPU_CMN_LIBS@
VPU5_CFLAGS = -I@VPUMW_PATH@/include \
	-DVPU5HG_FIRMWARE_PATH=\"@VPUMW_PATH@/lib/firmware\" \
	-D$(VPU_VERSION) -D$(VPU_SERIES)

OPTIONAL_SOURCES =

if USE_TL_CONV
VPU5_CFLAGS += -DTL_CONV_ENABLE
if SET_TL_WIDTH
VPU5_CFLAGS += -DTL_TILE_WIDTH_LOG2=@TL_CONV_WIDTH@
endif
if SET_TL_HEIGHT
VPU5_CFLAGS += -DTL_TILE_HEIGHT_LOG2=@TL_CONV_HEIGHT@
endif
OPTIONAL_SOURCES += shvpu5_common_ipmmu.c
endif

if USE_UIO_TL
VPU5_CFLAGS += $(MERAM_CFLAGS) -DUIO_TL_CONV
VPU5_LIBS += $(MERAM_LIBS)
OPTIONAL_SOURCES += shvpu5_uio_tiling.c
endif

if USE_VPC
VPU5_CFLAGS += -DVPC_ENABLE
endif

if USE_KERNEL_TL
OPTIONAL_SOURCES += shvpu5_kernel_tiling.c
endif

if USE_TL_CONV_INT
VPU5_CFLAGS += -DVPU_INTERNAL_TL
endif

if USE_MERAM
VPU5_CFLAGS += -DMERAM_ENABLE $(MERAM_CFLAGS)
VPU5_LIBS += $(MERAM_LIBS)
OPTIONAL_SOURCES += shvpu5_common_meram.c \
		shvpu5_common_meram.h
endif

if USE_TDDMAC
VPU5_CFLAGS += -DDMAC_MODE $(TDDMAC_CFLAGS)
OPTIONAL_SOURCES += shvpu5_common_2ddmac.c ipmmuhelper.c
VPU5_LIBS += $(TDDMAC_LIBS)
endif

if WITH_MPEG4_DECODE
VPU5_CFLAGS += -DMPEG4_DECODER
OPTIONAL_SOURCES += shvpu5_m4vdec_parse.c shvpu5_m4vdec_decode.c
endif

if DECODER_COMPONENT
VPU5_CFLAGS += -DDECODER_COMPONENT
VPU5_LIBS += @VPU_DEC_LIBS@
VPU5_DEC_SOURCES = \
	shvpu5_decode_omx.c shvpu5_decode_omx.h \
	shvpu5_avcdec_parse.c shvpu5_decode.c \
	shvpu5_decode_input.c shvpu5_decode_output.c \
	shvpu5_decode.h \
	shvpu5_avcdec_decode.c
endif

if ENCODER_COMPONENT
VPU5_CFLAGS += -DENCODER_COMPONENT
VPU5_LIBS += @VPU_ENC_LIBS@
VPU5_ENC_SOURCES = \
	shvpu5_avcenc_omx.c shvpu5_avcenc_omx.h \
	shvpu5_avcenc_encode.c shvpu5_avcenc_output.c \
	shvpu5_avcenc.h
endif

if ICBCACHE_FLUSH
VPU5_CFLAGS += -DICBCACHE_FLUSH
endif

if USE_BUFFER_MODE
VPU5_CFLAGS += -DUSE_BUFFER_MODE
endif

if USE_IPMMU_MEMORY
VPU5_CFLAGS += -DVPU_IPMMU_MEMORY
OPTIONAL_SOURCES += shvpu5_memory_ipmmui.c
else
VPU5_CFLAGS += -DVPU_UIO_MEMORY
endif

omxshvpu5avc_LTLIBRARIES = $(shvpu5avc_la)

libomxshvpu5avc_la_SOURCES = \
	library_entry_point.c shvpu5_common_driver.c \
	shvpu5_common_log.c shvpu5_common_queue.c \
	shvpu5_common_uio.c shvpu5_common_sync.c \
	shvpu5_common_udfio.c shvpu5_common_ext.c \
	shvpu5_decode_notify.c $(VPU5_DEC_SOURCES) \
	$(VPU5_ENC_SOURCES) \
	$(OPTIONAL_SOURCES)

libomxshvpu5avc_la_LIBADD = \
	$(OMXIL_LIBS) $(VPU5_LIBS) $(UIOMUX_LIBS)
libomxshvpu5avc_la_CFLAGS = \
	$(OMXIL_CFLAGS) $(VPU5_CFLAGS) $(UIOMUX_CFLAGS)
libomxshvpu5avc_la_LDFLAGS = $(OMXIL_LDFLAGS) $(VPU5HG_LDFLAGS)
