AM_CPPFLAGS = -I$(srcdir) -I$(top_srcdir)/include

omxshspu2aac = $(BELLAGIO_PLUGINSDIR)

if HAVE_SHSPU2
shspu2aac_la = libomxshspu2aac.la
endif

omxshspu2aac_LTLIBRARIES = $(shspu2aac_la)

libomxshspu2aac_la_SOURCES = \
	library_entry_point.c
libomxshspu2aac_la_LIBADD = \
	$(OMXIL_LIBS) \
        $(UIOMUX_LIBS)
libomxshspu2aac_la_CFLAGS = \
	$(OMXIL_CFLAGS) \
	$(UIOMUX_CFLAGS) \
libomxshspu2aac_la_LDFLAGS = $(OMXIL_LDFLAGS)
