################################################################################
# omxil-sh: A collection of OpenMAX IL components for SH-Mobile                #
# Copyright (C) 2009 Renesas Technology Corp.                                  #
#                                                                              #
# This library is free software; you can redistribute it and/or                #
# modify it under the terms of the GNU Lesser General Public                   #
# License as published by the Free Software Foundation; either                 #
# version 2.1 of the License, or (at your option) any later version.           #
#                                                                              #
# This library is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU            #
# Lesser General Public License for more details.                              #
#                                                                              #
# You should have received a copy of the GNU Lesser General Public             #
# License along with this library; if not, write to the Free Software          #
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA #
################################################################################

# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(omxil-sh, 0.7.0, [dhobsong@igel.co.jp])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/vpu/library_entry_point.c])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL

# Checks for system libraries.

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_CHECK_FUNCS([memset])

# Check for Bellagio
PKG_CHECK_MODULES([OMXIL], [libomxil-bellagio])

_PKG_CONFIG([pluginsdir], [variable=pluginsdir], [libomxil-bellagio])
BELLAGIO_PLUGINSDIR=${pkg_cv_pluginsdir}
AC_SUBST([BELLAGIO_PLUGINSDIR])

# Bellagio debug levels: <bellagio/omx_comp_debug_levels.h>
AC_ARG_ENABLE(
    [debug],
    [AC_HELP_STRING(
        [--enable-debug],
        [whether to enable an x debug level])],
    [with_debug_level=$enableval],
    [with_debug_level=no])

CFG_DEBUG_LEVEL=0
# Define debug messages level
if test "x$with_debug_level" = "xno"; then
 CFG_DEBUG_LEVEL=0
fi
if test "x$with_debug_level" = "xyes"; then
 CFG_DEBUG_LEVEL=255
fi
if test "x$with_debug_level" = "x1"; then
 CFG_DEBUG_LEVEL=2
fi
if test "x$with_debug_level" = "x2"; then
 CFG_DEBUG_LEVEL=4
fi
if test "x$with_debug_level" = "x3"; then
 CFG_DEBUG_LEVEL=8
fi
if test "x$with_debug_level" = "x4"; then
 CFG_DEBUG_LEVEL=16
fi
if test "x$with_debug_level" = "x5"; then
 CFG_DEBUG_LEVEL=32
fi
if test "x$with_debug_level" = "x6"; then
 CFG_DEBUG_LEVEL=255
fi

AC_SUBST(CFG_DEBUG_LEVEL)
CFLAGS="${CFLAGS} -DCONFIG_DEBUG_LEVEL=$CFG_DEBUG_LEVEL"
## Checks for codec libraries

# Check for HAACD
HAVE_HAACD="no"
HAACD_SUPPORT="no"

ac_enable_haacd=yes
AC_ARG_ENABLE(aac,
     [  --disable-aac           disable building of AAC support ],
     [ ac_enable_haacd=no ], [ ac_enable_haacd=yes] )

if test "x${ac_enable_haacd}" = xyes ; then
  #AC_CHECK_LIB(SACP1, HAACD_Open, HAVE_HAACD="maybe", , [-lm])

  # We cannot use AC_CHECK_LIB because libSACP1 requires the application to
  # implement a function called HAACD_GetData(); AC_CHECK_LIB will fail to link
  AC_MSG_CHECKING([for HAACD_Open in -lSACP1])

  ac_save_LIBS="$LIBS"
  LIBS="$LIBS -lSACP1"

  AC_TRY_COMPILE([#include <HAACD_ADL.h>
                  int HAACD_GetData (unsigned char *wpt, int ndata) { return 0; }],
                 [HAACD_AAC aac; unsigned char aac_stream[1024];
                  HAACD_Open (&aac, aac_stream, sizeof(aac_stream));],
                 [HAVE_HAACD="maybe"
                  AC_MSG_RESULT(yes)],
                 [AC_MSG_RESULT(no)])

  LIBS="$ac_save_LIBS"
                 
  # Ok, check done
  if test "x$HAVE_HAACD" = xmaybe ; then
    AC_CHECK_HEADER(HAACD_ADL.h, HAVE_HAACD="yes", HAVE_HAACD="no")
  fi
  if test "x$HAVE_HAACD" = xyes ; then
    AC_DEFINE(HAVE_HAACD, [1], [Define to 1 if you have HAACD])
    HAACD_LIBS="-lSACP1 -lm"
    AC_SUBST(HAACD_LIBS)
    HAACD_SUPPORT="yes (HAACD)"
  else
    AC_DEFINE(HAVE_HAACD, [0], [Define to 1 if you have HAACD])
  fi
else
  AC_DEFINE(HAVE_HAACD, [0], [Define to 1 if you have HAACD])
  HAACD_SUPPORT="disabled"
fi
AM_CONDITIONAL(HAVE_HAACD, [test "x$HAVE_HAACD" = "xyes"])

# Check for HMP3D
HAVE_HMP3D="no"
HMP3D_SUPPORT="no"

ac_enable_hmp3d=yes
AC_ARG_ENABLE(mp3,
     [  --disable-mp3           disable building of MP3 support ],
     [ ac_enable_hmp3d=no ], [ ac_enable_hmp3d=yes] )

if test "x${ac_enable_hmp3d}" = xyes ; then
  AC_CHECK_LIB(hmp3d, HMP3D_Open, HAVE_HMP3D="maybe", , [-lm])
  if test "x$HAVE_HMP3D" = xmaybe ; then
    AC_CHECK_HEADER(HMP3D_Mp3.h, HAVE_HMP3D="yes", HAVE_HMP3D="no")
  fi
  if test "x$HAVE_HMP3D" = xyes ; then
    AC_DEFINE(HAVE_HMP3D, [1], [Define to 1 if you have HMP3D])
    HMP3D_LIBS="-lhmp3d -lm"
    AC_SUBST(HMP3D_LIBS)
    HMP3D_SUPPORT="yes (HMP3D)"
  else
    AC_DEFINE(HAVE_HMP3D, [0], [Define to 1 if you have HMP3D])
  fi
else
  AC_DEFINE(HAVE_HMP3D, [0], [Define to 1 if you have HMP3D])
  HMP3D_SUPPORT="disabled"
fi
AM_CONDITIONAL(HAVE_HMP3D, [test "x$HAVE_HMP3D" = "xyes"])
  
# Check for shcodecs
HAVE_SHCODECS="no"
VPU_SUPPORT="no"
VPU_OPTIONS=""

ac_enable_vpu=yes
AC_ARG_WITH([vpumw-path],
	[AC_HELP_STRING(
		[--with-vpumw-path=PATH],
		[set path prefix for the VPU middleware [default=/usr/local]])
	],
	[VPUMW_PATH=$withval],
	[VPUMW_PATH=/usr/local]
)
AC_SUBST(VPUMW_PATH)
AC_ARG_ENABLE(vpu,
     [  --disable-vpu           disable building of VPU support ],
     [ ac_enable_vpu=no ], [ ac_enable_vpu=yes] )

if test "x${ac_enable_vpu}" = xyes ; then
  PKG_CHECK_MODULES([SHCODECS], [shcodecs], HAVE_SHCODECS="yes", HAVE_SHCODECS="no")

  if test "x$HAVE_SHCODECS" = "xyes" ; then
    AC_DEFINE(HAVE_SHCODECS, [1], [Define to 1 if you have SHCODECS])
    AC_SUBST(SHCODECS_LIBS)
    VPU_SUPPORT="yes (SHCODECS)"
  else
    # VPU5HG
    # We cannot use AC_CHECK_LIB because libvpu5* requires the application to
    # implement some functions; AC_CHECK_LIB will fail to link
    AC_MSG_CHECKING([for SHVPU5HG AVC libraries])

    ac_save_LIBS="$LIBS"
    ac_save_CFLAGS="$CFLAGS"
    LIBS="$LIBS $VPUMW_PATH/lib"
    CFLAGS="$CFLAGS -I$VPUMW_PATH/include"

    AC_TRY_COMPILE([
    #include <avcdec.h>
    #include <mcvdec.h>
    #include <mciph_hg.h>
    #include <mciph.h>
    void mciph_uf_ce_start(void *context, long mode, void *start_info) { return; }
    void mciph_uf_ce_sleep(void *context, long mode) { return; }
    void mciph_uf_ce_restart(void *context, long mode) { return; }
    void mciph_uf_vlc_start(void *context, long mode) { return; }
    void mciph_uf_vlc_sleep(void *context, long mode) { return; }
    void mciph_uf_vlc_restart(void *context, long mode) { return; }
    long mciph_uf_reg_table_read(unsigned long src_addr, unsigned long reg_table, long size) { return 0L; }
    long mciph_uf_reg_table_write(unsigned long dst_addr, unsigned long reg_table, long size) { return 0L; }
    long mciph_uf_mem_read(unsigned long src_addr, unsigned long dst_addr, long size) { return 0L; }
    long mciph_uf_mem_write(unsigned long src_addr, unsigned long dst_addr, long size) { return 0L; }
    void mciph_uf_set_imask(long mask_enable, long now_interrupt) { return; }
    ],
    [avcdec_get_version(); mcvdec_get_version();
     mciph_get_version(); mciph_hg_get_version();],
    [HAVE_VP5DECAVC="yes"
     AC_MSG_RESULT(yes)],
    [HAVE_VP5DECAVC="yes"
    AC_MSG_RESULT(no)])

    CFLAGS="$ac_save_CFLAGS"
    LIBS="$ac_save_LIBS"

    if test "x$HAVE_VP5DECAVC" = "xyes" ; then
      AC_DEFINE(HAVE_SHVPU5DECAVC, [1], [Define to 1 if you have VP5AVC])
      VPU_SUPPORT="yes (VP5AVC)"
    else
      AC_DEFINE(HAVE_SHVPU5DECAVC, [0], [Define to 1 if you have VP5AVC])
      VPU_SUPPORT="disabled"
    fi
    AM_CONDITIONAL(HAVE_SHVPU5DEC, [test "x$HAVE_VP5DECAVC" = "xyes"])

    # Check for libuiomux
    PKG_CHECK_MODULES([UIOMUX], [uiomux])

    AC_DEFINE(HAVE_SHCODECS, [0], [Define to 1 if you have SHCODECS])
  fi
  AM_CONDITIONAL(HAVE_SHCODECS, [test "x$HAVE_SHCODECS" = "xyes"])
fi

dnl VPU Version
AC_ARG_WITH(vpu5_version,
	[AC_HELP_STRING(
		[--with-vpu5_version=[ver]],
		[set VPU version.  [ver]=VPU5HG(default)/VPU5HA/VPU5HD])])

AS_CASE([$with_vpu5_version],
	[VPU5HA],
		[VPU_VERSION="VPU_VERSION_5HA"
		 VPU_SERIES="VPU5HA_SERIES"
		 VPU_CMN_LIBS="-lvpu5hadrvcmn -lvpu5drv"
		 VPU_DEC_LIBS="-lvpu5hadecavc -lvpu5hadeccmn  \
			       -lvpu5hadrvavcdec -lvpu5hadrvcmndec"],
	[VPU5HD],
		[VPU_VERSION="VPU_VERSION_5HD"
		 VPU_SERIES="VPU5HA_SERIES"
		 VPU_CMN_LIBS="-lmciphvpu5hdcmna -lmciphvpu5ha"
		 VPU_DEC_LIBS="-lavcdecvpu5hda -lmciphvpu5hdavcdeca \
			       -lmciphvpu5hdcmndeca -lmcvdecvpu5hda"],
dnl  	default
		[with_vpu5_version=VPU5HG
		VPU_VERSION="VPU_VERSION_5"
		VPU_SERIES="VPU5_SERIES"
		VPU_CMN_LIBS="-lvpu5drvcmn -lvpu5drvhg"
		VPU_DEC_LIBS="-lvpu5decavc -lvpu5deccmn"
		VPU_ENC_LIBS="-lvpu5encavc -lvpu5enccmn"]);

AC_SUBST(VPU_VERSION)
AC_SUBST(VPU_SERIES)
AC_SUBST(VPU_CMN_LIBS)
AC_SUBST(VPU_DEC_LIBS)
AC_SUBST(VPU_ENC_LIBS)

dnl Disable encoder/decoder components
AC_ARG_ENABLE(vpu5_decoder,
	[AC_HELP_STRING(
		[--disable-vpu5_decoder],
		[disable VPU5 decoder])])

AM_CONDITIONAL([DECODER_COMPONENT],
      [test "x$enable_vpu5_decoder" != "xno"])

AS_IF([test "x$enable_vpu5_decoder" != "xno"],
		[VPU_OPTIONS="${VPU_OPTIONS}Decoder "])

AC_ARG_ENABLE(vpu5_encoder,
	[AC_HELP_STRING(
		[--disable-vpu5_encoder],
		[disable VPU5 encoder])])

AM_CONDITIONAL([ENCODER_COMPONENT],
      [test "x$enable_vpu5_encoder" != "xno"])

AS_IF([test "x$enable_vpu5_encoder" != "xno"],
		[VPU_OPTIONS="${VPU_OPTIONS}Encoder "])

dnl Set buffer mode
AC_ARG_ENABLE(vpu5_mode,
	[AC_HELP_STRING(
		[--enable-vpu5_mode=[MODE]],
		[set VPU5 buffer mode [MODE]=usebuffer/allocatebuffer(default)])])

AM_CONDITIONAL([USE_BUFFER_MODE],
      [test "x$enable_vpu5_mode" = "xusebuffer"])

AS_IF([test "x$enable_vpu5_mode" = "xusebuffer"],
		[VPU_OPTIONS="${VPU_OPTIONS}UseBuffer "],
		[VPU_OPTIONS="${VPU_OPTIONS}AllocateBuffer "])

dnl disable 2DDMAC
AC_ARG_ENABLE(vpu5_tddmac,
	[AC_HELP_STRING(
		[--disable-vpu5_tddmac],
		[disable VPU5 TDDMAC mode (only enabled in usebuffer mode)])])

AS_IF([test "x$enable_vpu5_tddmac" != "xno" -a "x$enable_vpu5_mode" = "xusebuffer"],
	[PKG_CHECK_MODULES([TDDMAC], [tddmac],
		[VPU_OPTIONS="${VPU_OPTIONS}2DDMAC "],[enable_vpu5_tddmac=no])],
	[enable_vpu5_tddmac=no])

AM_CONDITIONAL(USE_TDDMAC, [test "x$enable_vpu5_tddmac" != "xno"])

dnl Disable TL_CONV setting
AC_ARG_ENABLE(tl_conv,
	[AC_HELP_STRING(
		[--enable-tl_conv=[internal/ipmmu/no]],
		[set T/L conversion support [default=ipmmu if supported]])])

if test "x$enable_tl_conv" != "xno" -a "x$HAVE_VP5DECAVC" = "xyes"; then
	AS_IF([test "x$enable_tl_conv" != "xinternal"],
		[PKG_CHECK_MODULES([MERAM], [meram],
			[VPU_OPTIONS="${VPU_OPTIONS}TL_CONV "],[enable_tl_conv=no])],
		[VPU_OPTIONS="${VPU_OPTIONS}TL_CONV(internal) "])
fi

AM_CONDITIONAL(USE_TL_CONV,
      [test "x$enable_tl_conv" != "xno" -a "x$HAVE_VP5DECAVC" = "xyes" -a "x$enable_tl_conv" != "xinternal"])

AM_CONDITIONAL(USE_TL_CONV_INT,
      [test "x$HAVE_VP5DECAVC" = "xyes" -a "$enable_tl_conv" = "internal"])

dnl Disable MERAM setting
AC_ARG_ENABLE(meram,
	[AC_HELP_STRING(
		[--disable-meram], [disable building of MERAM support ])])

if test "x$enable_meram" != "xno" -a "x$HAVE_VP5DECAVC" = "xyes"; then
	PKG_CHECK_MODULES([MERAM], [meram],
		[VPU_OPTIONS="${VPU_OPTIONS}MERAM "], [enable_meram=no])
fi

AS_IF([test "x$enable_vpu5_tddmac" != "xno" -a "x$enable_meram" = "xno"],
	[AC_MSG_ERROR([TDDMAC functionality requires MERAM])])

AM_CONDITIONAL(USE_MERAM,
      [test "x$enable_meram" != "xno" -a "x$HAVE_VP5DECAVC" = "xyes"])

dnl Set VPU memroy allocation type
AC_ARG_WITH(vpu5_memtype,
	[AC_HELP_STRING(
		[--with-vpu5_memtype=[uio/ipmmu]],
		[set the VPU memory allocation type mechanism. [default=uio]])])

AS_CASE([$with_vpu5_memtype],
	[ipmmui],[VPU_OPTIONS="${VPU_OPTIONS}memory:IPMMU"],
	[VPU_OPTIONS="${VPU_OPTIONS}memory:UIO"])

AM_CONDITIONAL(USE_IPMMU_MEMORY, [test "$with_vpu5_memtype" = "xipmmu"])

AC_ARG_WITH(spumw-path,
	[AC_HELP_STRING(
		[--with-spumw-path=PATH],
		[set path prefix for the SPU middleware [default=/usr/local]])
	],
	[SPUMW_PATH=$withval],
	[SPUMW_PATH=/usr/local]
)
AC_SUBST(SPUMW_PATH)
AC_ARG_ENABLE(spu2aacdec,
	[AC_HELP_STRING(
		[--enable-spu2aacdec],
		[enable building of SPU2 AAC decoder support])
	],
	[ac_enable_spu2aacdec=yes],
	[ac_enable_spu2aacdec=no]
)
AC_ARG_ENABLE(spu2aacenc,
	[AC_HELP_STRING(
		[--enable-spu2aacenc],
		[enable building of SPU2 AAC encoder support])
	],
	[ac_enable_spu2aacenc=yes],
	[ac_enable_spu2aacenc=no]
)
SPU_SUPPORT_MSG=
test "$ac_enable_spu2aacdec" = yes && SPU_SUPPORT_MSG="d$SPU_SUPPORT_MSG"
test "$ac_enable_spu2aacenc" = yes && SPU_SUPPORT_MSG="e$SPU_SUPPORT_MSG"
case "$SPU_SUPPORT_MSG" in
ed)	SPU_SUPPORT="yes (SPU2AACDEC, SPU2AACENC)";;
e)	SPU_SUPPORT="yes (SPU2AACENC)";;
d)	SPU_SUPPORT="yes (SPU2AACDEC)";;
*)	SPU_SUPPORT="no";;
esac
if test "$SPU_SUPPORT_MSG"; then
	PKG_CHECK_MODULES([UIOMUX], [uiomux])
fi
AM_CONDITIONAL(HAVE_SHSPU2AACDEC, [test "$ac_enable_spu2aacdec" = yes])
AM_CONDITIONAL(HAVE_SHSPU2AACENC, [test "$ac_enable_spu2aacenc" = yes])
AM_CONDITIONAL(HAVE_SHSPU2, [test "$ac_enable_spu2aacdec" = yes -o "$ac_enable_spu2aacenc" = yes])

AC_CONFIG_FILES([
Makefile
src/Makefile
src/aac/Makefile
src/mp3/Makefile
src/vpu/Makefile
src/vpu5/Makefile
src/tools/Makefile
src/spu2/Makefile
src/spu2/spu2helper/Makefile
include/Makefile
])
AC_OUTPUT

AC_MSG_RESULT([
------------------------------------------------------------------------
  $PACKAGE $VERSION:  Automatic configuration OK.

    AAC support: ................. $HAACD_SUPPORT
    MP3 support: ................. $HMP3D_SUPPORT
    SPU support: ................. $SPU_SUPPORT
    VPU support: ................. $VPU_SUPPORT
    VPU options: $with_vpu5_version $VPU_OPTIONS

------------------------------------------------------------------------
])

