SUBDIRS = spu2helper
AM_CFLAGS = -I$(srcdir) -I$(top_srcdir)/include

omxshspu2aacdir = $(BELLAGIO_PLUGINSDIR)

if HAVE_SHSPU2
shspu2aac_la = libomxshspu2aac.la
endif
if HAVE_SHSPU2AACDEC
omxshspu2aacdec_libadd = ./spu2helper/libshspu2helperaacdec.la
omxshspu2aacdec_cflags = -DLIBSPUHELPERAACDEC
omxshspu2aacdec_component = \
	omx_audiodec_component.c \
	omx_audiodec_component.h \
	spuaacdec.h
endif
if HAVE_SHSPU2AACENC
omxshspu2aacenc_libadd = ./spu2helper/libshspu2helperaacenc.la
omxshspu2aacenc_cflags = -DLIBSPUHELPERAACENC
omxshspu2aacenc_component = \
	omx_audioenc_component.c \
	omx_audioenc_component.h \
	spuaacenc.h
endif

omxshspu2aac_LTLIBRARIES = $(shspu2aac_la)

libomxshspu2aac_la_SOURCES = \
	library_entry_point.c \
	$(omxshspu2aacdec_component) \
	$(omxshspu2aacenc_component)
libomxshspu2aac_la_LIBADD = \
	$(OMXIL_LIBS) \
	$(omxshspu2aacdec_libadd) \
	$(omxshspu2aacenc_libadd)
libomxshspu2aac_la_CFLAGS = \
	$(OMXIL_CFLAGS) \
	$(omxshspu2aacdec_cflags) \
	$(omxshspu2aacenc_cflags)
libomxshspu2aac_la_LDFLAGS = \
	$(OMXIL_LDFLAGS)
