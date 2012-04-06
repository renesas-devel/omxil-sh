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

ifeq ($(TARGET_DEVICE),mackerel)
VPU_VERSION := VPU_VERSION_5
DECODER_COMPONENT := 1
ENCODER_COMPONENT := 1
MIDDLEWARE_INCLUDE_PATH := \
		hardware/renesas/shmobile/prebuilt/vpu5/include
endif

ifneq (,$(findstring $(TARGET_DEVICE),ape5r kota2))
VPU_VERSION := VPU_VERSION_5HA
DECODER_COMPONENT := 1
ENCODER_COMPONENT := 1
MIDDLEWARE_INCLUDE_PATH := \
		hardware/renesas/shmobile/prebuilt/vpu5ha/include
endif

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

ifeq ($(DECODER_COMPONENT),1)
	LOCAL_SRC_FILES += \
		shvpu5_avcdec_decode.c \
		shvpu5_avcdec_notify.c \
		shvpu5_avcdec_omx.c \
		shvpu5_avcdec_parse.c \
		shvpu5_avcdec_input.c
	LOCAL_CFLAGS += -DDECODER_COMPONENT
endif
ifeq ($(ENCODER_COMPONENT),1)
	LOCAL_SRC_FILES += \
		shvpu5_avcenc_encode.c \
		shvpu5_avcenc_omx.c
	LOCAL_CFLAGS += -DENCODER_COMPONENT
endif

ifeq ($(TARGET_DEVICE),mackerel)

LOCAL_LDFLAGS = -Lhardware/renesas/shmobile/prebuilt/vpu5/lib \
	-lvpu5decavc \
	-lvpu5deccmn \
	-lvpu5encavc \
	-lvpu5enccmn \
	-lvpu5drvcmn \
	-lvpu5drvhg
endif

ifneq (,$(findstring $(TARGET_DEVICE),ape5r kota2))
LOCAL_LDFLAGS = -Lhardware/renesas/shmobile/prebuilt/vpu5ha/lib \
	-lvpu5hadecavc -lvpu5hadeccmn \
	-lvpu5hadrvcmn -lvpu5drv \
	-lvpu5hadrvavcdec -lvpu5hadrvcmndec \
	-lvpu5hadrvavcenc -lvpu5haencavc \
	-lvpu5hadrvcmnenc -lvpu5haenccmn
endif


LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libshvpu5avc
LOCAL_CFLAGS += -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/vpu5/\" -DANDROID -D$(VPU_VERSION)


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

ifeq ($(VPU_DECODE_TL_CONV), true)
	LOCAL_SHARED_LIBRARIES += libmeram
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libmeram/include
	LOCAL_SRC_FILES += shvpu5_common_ipmmu.c
	LOCAL_CFLAGS += -DTL_CONV_ENABLE
ifeq ($(VPU_DECODE_USE_2DDMAC), true)
ifneq ($(VPU_DECODE_WITH_MERAM), true)
	LOCAL_SRC_FILES += shvpu5_avcdec_meram.c
endif
endif
endif

ifeq ($(VPU_DECODE_WITH_MERAM), true)
	LOCAL_SHARED_LIBRARIES += libmeram
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libmeram/include
	LOCAL_SRC_FILES += shvpu5_avcdec_meram.c
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

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libvpu5uio
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DANDROID -D$(VPU_VERSION)

ifeq ($(VPU_DECODE_TL_CONV), true)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libmeram/include
	LOCAL_CFLAGS += -DTL_CONV_ENABLE
endif

ifeq ($(VPU_DECODE_WITH_MERAM), true)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libmeram/include
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

LOCAL_SRC_FILES := 	\
	shvpu5_common_sync.c \
	shvpu5_common_udfio.c

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libvpu5udf
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/vpu5/\" -DANDROI -D$(VPU_VERSION)D

ifeq ($(VPU_DECODE_TL_CONV), true)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libmeram/include
	LOCAL_CFLAGS += -DTL_CONV_ENABLE
endif

ifeq ($(VPU_DECODE_WITH_MERAM), true)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libmeram/include
	LOCAL_SRC_FILES += shvpu5_avcdec_meram.c
	LOCAL_SHARED_LIBRARIES += libmeram
	LOCAL_CFLAGS += -DMERAM_ENABLE
endif
include $(BUILD_SHARED_LIBRARY)

ifeq ($(DECODER_COMPONENT),1)
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
	shvpu5_avcdec_output.c \
	shvpu5_avcdec_input.c

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libvpu5udfdec
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/vpu5/\" -DANDROI -D$(VPU_VERSION)D

ifeq ($(VPU_DECODE_TL_CONV), true)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libmeram/include
	LOCAL_SRC_FILES += shvpu5_common_ipmmu.c
	LOCAL_CFLAGS += -DTL_CONV_ENABLE
	LOCAL_SHARED_LIBRARIES += libmeram
endif

ifeq ($(VPU_DECODE_WITH_MERAM), true)
	LOCAL_C_INCLUDES += hardware/renesas/shmobile/libmeram/include
	LOCAL_SRC_FILES += shvpu5_avcdec_meram.c
	LOCAL_SHARED_LIBRARIES += libmeram
	LOCAL_CFLAGS += -DMERAM_ENABLE
endif
include $(BUILD_SHARED_LIBRARY)
endif

ifeq ($(ENCODER_COMPONENT),1)
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
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/vpu5/\" -DANDROID -D$(VPU_VERSION)
include $(BUILD_SHARED_LIBRARY)
endif
