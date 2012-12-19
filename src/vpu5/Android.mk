# Copyright (C) 2008 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


LOCAL_PATH := $(call my-dir)

# HAL module implemenation, not prelinked and stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so
include $(CLEAR_VARS)

ifeq ($(VPU_MIDDLEWARE_PATH),)
	VPU_MIDDLEWARE_PATH := hardware/renesas/shmobile/prebuilt/vpu5
endif

LOCAL_TL_CONV := $(VPU_DECODE_TL_CONV)
ifeq ($(LOCAL_TL_CONV), true)
	LOCAL_TL_CONV := uio
endif

ifeq ($(TARGET_DEVICE),mackerel)
PRODUCT_VPU_VERSION := VPU_VERSION_5
VPU_DECODER_COMPONENT := true
VPU_ENCODER_COMPONENT := true
endif

ifneq (,$(findstring $(TARGET_DEVICE),ape5r kota2))
PRODUCT_VPU_VERSION := VPU_VERSION_5HA
VPU_DECODER_COMPONENT := true
VPU_ENCODER_COMPONENT := true
endif

include $(LOCAL_PATH)/versiondefs.mk


MIDDLEWARE_INCLUDE_PATH := $(VPU_MIDDLEWARE_PATH)/include
MIDDLEWARE_LIB_PATH := $(VPU_MIDDLEWARE_PATH)/lib

LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := liblog  \
			libcutils \
			libomxil-bellagio \
			libvpu5uio

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		$(TARGET_OUT_HEADERS)/libomxil-bellagio \
		$(TARGET_OUT_HEADERS)/libomxil-bellagio/bellagio \
		external/libuiomux/include \
		$(LOCAL_PATH)/../../include \
		$(MIDDLEWARE_INCLUDE_PATH)

LOCAL_SRC_FILES := 	\
	library_entry_point.c \
	shvpu5_common_queue.c \
	shvpu5_common_driver.c \
	shvpu5_common_ext.c

ifeq ($(VPU_DECODER_COMPONENT),true)
	LOCAL_SRC_FILES += \
		shvpu5_decode.c \
		shvpu5_avcdec_decode.c \
		shvpu5_decode_notify.c \
		shvpu5_decode_omx.c \
		shvpu5_avcdec_parse.c \
		shvpu5_decode_input.c
	LOCAL_CFLAGS += -DDECODER_COMPONENT
endif

ifeq ($(VPU_MPEG4_DECODER),true)
	LOCAL_SRC_FILES += \
		shvpu5_m4vdec_parse.c \
		shvpu5_m4vdec_decode.c
	LOCAL_CFLAGS += -DMPEG4_DECODER
endif

ifneq ($(VPU_TL_TILE_WIDTH_LOG2),)
	LOCAL_CFLAGS += -DTL_TILE_WIDTH_LOG2=$(VPU_TL_TILE_WIDTH_LOG2)
endif
ifneq ($(VPU_TL_TILE_HEIGHT_LOG2),)
	LOCAL_CFLAGS += -DTL_TILE_HEIGHT_LOG2=$(VPU_TL_TILE_HEIGHT_LOG2)
endif

ifeq ($(VPU_ENCODER_COMPONENT),true)
	LOCAL_SRC_FILES += \
		shvpu5_avcenc_encode.c \
		shvpu5_avcenc_omx.c
	LOCAL_CFLAGS += -DENCODER_COMPONENT
endif

ifeq ($(VPU_VERSION),VPU_VERSION_5)

LOCAL_LDFLAGS = -L$(MIDDLEWARE_LIB_PATH) \
	-lvpu5decavc \
	-lvpu5deccmn \
	-lvpu5encavc \
	-lvpu5enccmn \
	-lvpu5drvcmn \
	-lvpu5drvhg
endif

ifeq ($(PRODUCT_VPU_VERSION),VPU_VERSION_5HA)
LOCAL_LDFLAGS = -L$(MIDDLEWARE_LIB_PATH) \
	-lvpu5hadecavc -lvpu5hadeccmn \
	-lvpu5hadrvcmn -lvpu5drv \
	-lvpu5hadrvavcdec -lvpu5hadrvcmndec \
	-lvpu5hadrvavcenc -lvpu5haencavc \
	-lvpu5hadrvcmnenc -lvpu5haenccmn
endif

ifeq ($(PRODUCT_VPU_VERSION), VPU_VERSION_5HD)
LOCAL_LDFLAGS = -L$(MIDDLEWARE_LIB_PATH) \
	-lvpu5hddecavc -lvpu5hddeccmn \
	-lvpu5hddrvcmn -lvpu5drv \
	-lvpu5hddrvavcdec -lvpu5hddrvcmndec
ifeq ($(VPU_MPEG4_DECODER),true)
LOCAL_LDFLAGS += \
	-lvpu5hddecm4v -lvpu5hddrvm4vdec
endif
endif

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libshvpu5avc
LOCAL_CFLAGS += -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/vpu5/\" -DANDROID $(VPU_VERSION_DEFS)


ifeq ($(VPU_DECODE_USE_BUFFER), true)
	LOCAL_CFLAGS += -DUSE_BUFFER_MODE -DOUTPUT_BUFFER_ALIGN=32
endif
ifeq ($(VPU_DECODE_USE_2DDMAC), true)
	LOCAL_CFLAGS += -DDMAC_MODE
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libtddmac/include
	LOCAL_SRC_FILES += shvpu5_common_2ddmac.c ipmmuhelper.c
	LOCAL_SHARED_LIBRARIES += libtddmac
endif

ifeq ($(OMXIL_ANDROID_CUSTOM), true)
LOCAL_SRC_FILES += shvpu5_common_android_helper.cpp
LOCAL_C_INCLUDES += frameworks/base/include \
		    hardware/renesas/shmobile/gralloc
LOCAL_CFLAGS += -DANDROID_CUSTOM
LOCAL_SHARED_LIBRARIES += libui
endif

ifeq ($(LOCAL_TL_CONV), uio)
	LOCAL_SHARED_LIBRARIES += libmeram
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libshmeram/include
	LOCAL_SRC_FILES += shvpu5_common_ipmmu.c shvpu5_uio_tiling.c
	LOCAL_CFLAGS += -DTL_CONV_ENABLE
	TL_INV_MERAM := true
else ifeq ($(LOCAL_TL_CONV), kernel)
	LOCAL_SRC_FILES += shvpu5_common_ipmmu.c shvpu5_kernel_tiling.c
	LOCAL_CFLAGS += -DTL_CONV_ENABLE
	TL_INV_MERAM := true
endif

ifeq ($(TL_INV_MERAM), true)
ifeq ($(VPU_DECODE_USE_2DDMAC), true)
ifneq ($(VPU_DECODE_WITH_MERAM), true)
	LOCAL_SRC_FILES += shvpu5_common_meram.c
endif
endif
endif

ifeq ($(VPU_DECODE_WITH_MERAM), true)
	LOCAL_SHARED_LIBRARIES += libmeram
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libshmeram/include
	LOCAL_SRC_FILES += shvpu5_common_meram.c
	LOCAL_CFLAGS += -DMERAM_ENABLE
endif
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := liblog  \
			libcutils \
			libomxil-bellagio \
			libuiomux

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		$(TARGET_OUT_HEADERS)/libomxil-bellagio \
		$(TARGET_OUT_HEADERS)/libomxil-bellagio/bellagio \
		external/libuiomux/include \
		$(LOCAL_PATH)/../../include \
		$(MIDDLEWARE_INCLUDE_PATH)

LOCAL_SRC_FILES := 	\
	shvpu5_common_uio.c \
	shvpu5_common_log.c

ifeq ($(VPU_MEMORY_TYPE), ipmmui)
	LOCAL_SRC_FILES += shvpu5_memory_ipmmui.c
endif

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libvpu5uio
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DANDROID $(VPU_VERSION_DEFS)


ifeq ($(VPU_DECODE_USE_VPC), true)
	LOCAL_CFLAGS += -DVPC_ENABLE
endif
ifeq ($(VPU_DECODE_USE_ICBCACHE), true)
	LOCAL_CFLAGS += -DICBCACHE_FLUSH
endif
ifeq ($(LOCAL_TL_CONV), true)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libshmeram/include
	LOCAL_CFLAGS += -DTL_CONV_ENABLE
endif

ifeq ($(VPU_DECODE_WITH_MERAM), true)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libshmeram/include
	LOCAL_CFLAGS += -DMERAM_ENABLE
endif
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := liblog  \
			libcutils \
			libomxil-bellagio \
			libvpu5uio

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		$(TARGET_OUT_HEADERS)/libomxil-bellagio \
		$(TARGET_OUT_HEADERS)/libomxil-bellagio/bellagio \
		external/libuiomux/include \
		$(LOCAL_PATH)/../../include \
		$(MIDDLEWARE_INCLUDE_PATH)

ifeq ($(VPU_DECODE_USE_VPC), true)
	LOCAL_CFLAGS += -DVPC_ENABLE
endif
ifeq ($(VPU_DECODE_USE_ICBCACHE), true)
	LOCAL_CFLAGS += -DICBCACHE_FLUSH
endif

LOCAL_SRC_FILES := 	\
	shvpu5_common_sync.c \
	shvpu5_common_udfio.c

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libvpu5udf
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/vpu5/\" -DANDROID $(VPU_VERSION_DEFS)

ifeq ($(LOCAL_TL_CONV), uio)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libshmeram/include
	LOCAL_CFLAGS += -DTL_CONV_ENABLE
endif

ifeq ($(VPU_DECODE_WITH_MERAM), true)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libshmeram/include
	LOCAL_SRC_FILES += shvpu5_common_meram.c
	LOCAL_SHARED_LIBRARIES += libmeram
	LOCAL_CFLAGS += -DMERAM_ENABLE
endif
include $(BUILD_SHARED_LIBRARY)

ifeq ($(VPU_DECODER_COMPONENT),true)
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := liblog  \
			libcutils \
			libuiomux \
			libomxil-bellagio \
			libvpu5uio \

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		$(TARGET_OUT_HEADERS)/libomxil-bellagio \
		$(TARGET_OUT_HEADERS)/libomxil-bellagio/bellagio \
		external/libuiomux/include \
		hardware/renesas/shmobile/prebuilt/include \
		$(LOCAL_PATH)/../../include \
		$(MIDDLEWARE_INCLUDE_PATH)

LOCAL_SRC_FILES := 	\
	shvpu5_common_queue.c \
	shvpu5_decode_output.c \
	shvpu5_decode_input.c

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libvpu5udfdec
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/vpu5/\" -DANDROID $(VPU_VERSION_DEFS)

ifeq ($(LOCAL_TL_CONV), uio)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libshmeram/include
	LOCAL_SRC_FILES += shvpu5_common_ipmmu.c shvpu5_uio_tiling.c
	LOCAL_CFLAGS += -DTL_CONV_ENABLE
	LOCAL_SHARED_LIBRARIES += libmeram
endif

ifeq ($(VPU_DECODE_WITH_MERAM), true)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libshmeram/include
	LOCAL_SRC_FILES += shvpu5_common_meram.c
	LOCAL_SHARED_LIBRARIES += libmeram
	LOCAL_CFLAGS += -DMERAM_ENABLE
endif
include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(VPU_ENCODER_COMPONENT),true)
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := liblog  \
			libcutils \
			libuiomux \
			libomxil-bellagio \
			libvpu5uio

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		$(TARGET_OUT_HEADERS)/libomxil-bellagio \
		$(TARGET_OUT_HEADERS)/libomxil-bellagio/bellagio \
		external/libuiomux/include \
		$(LOCAL_PATH)/../../include \
		$(MIDDLEWARE_INCLUDE_PATH)

LOCAL_SRC_FILES := 	\
	shvpu5_avcenc_output.c

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libvpu5udfenc
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/vpu5/\" -DANDROID $(VPU_VERSION_DEFS)
include $(BUILD_SHARED_LIBRARY)
endif
