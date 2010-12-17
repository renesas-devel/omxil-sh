AM_CFLAGS = -I$(srcdir) -I$(top_srcdir)/include

shspu2helperaacdecdir = $(libdir)
shspu2helperaacencdir = $(libdir)

if HAVE_SHSPU2AACDEC
shspu2helperaacdec_la = libshspu2helperaacdec.la
endif
if HAVE_SHSPU2AACENC
shspu2helperaacenc_la = libshspu2helperaacenc.la
endif

shspu2helperaacdec_LTLIBRARIES = $(shspu2helperaacdec_la)
shspu2helperaacenc_LTLIBRARIES = $(shspu2helperaacenc_la)

libshspu2helperaacdec_la_SOURCES = \
	spuaacdec.c \
	spu.c \
	spu_dsp.c \
	uio.c \
	uiohelper.c \
	common.h \
	spuaacdec.h \
	spu.h \
	spu_dsp.h \
	uio.h \
	uiohelper.h
libshspu2helperaacdec_la_LIBADD = \
	$(UIOMUX_LIBS)
libshspu2helperaacdec_la_CFLAGS = \
	$(UIOMUX_CFLAGS) \
	-I@SPUMW_PATH@/include
libshspu2helperaacdec_la_LDFLAGS = \
	$(UIOMUX_LDFLAGS) \
	-L@SPUMW_PATH@/lib \
	-lshspuaacdec

libshspu2helperaacenc_la_SOURCES = \
	spuaacenc.c \
	spu.c \
	spu_dsp.c \
	uio.c \
	uiohelper.c \
	common.h \
	spuaacenc.h \
	spu.h \
	spu_dsp.h \
	uio.h \
	uiohelper.h
libshspu2helperaacenc_la_LIBADD = \
	$(UIOMUX_LIBS)
libshspu2helperaacenc_la_CFLAGS = \
	$(UIOMUX_CFLAGS) \
	-I@SPUMW_PATH@/include
libshspu2helperaacenc_la_LDFLAGS = \
	$(UIOMUX_LDFLAGS) \
	-L@SPUMW_PATH@/lib \
	-lshspuaacenc
