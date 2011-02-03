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
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := liblog  \
			libcutils \
			libbellcore \
			libvpu5uio

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		external/bellagio/src \
		external/bellagio/include \
		external/libuiomux/include \
		$(LOCAL_PATH)/../vpu5_mid/include \
		$(LOCAL_PATH)/../../include

LOCAL_SRC_FILES := 	\
	library_entry_point.c \
	shvpu5_avcdec_decode.c \
	shvpu5_common_log.c \
	shvpu5_avcdec_notify.c \
	shvpu5_avcdec_omx.c \
	shvpu5_avcdec_parse.c \
	shvpu5_avcdec_input.c \
	shvpu5_common_queue.c \
	shvpu5_avcdec_bufcalc.c \
	shvpu5_avcenc_encode.c \
	shvpu5_avcenc_omx.c \
	shvpu5_common_driver.c \
	shvpu5_common_ipmmu.c

LOCAL_LDFLAGS = -L$(LOCAL_PATH)/../vpu5_mid/lib \
	-lvpu5decavc \
	-lvpu5deccmn \
	-lvpu5encavc \
	-lvpu5enccmn \
	-lvpu5drvcmn \
	-lvpu5drvhg

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libshvpu5avc
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/avc_dec/\" -DANDROID
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := liblog  \
			libcutils \
			libuiomux

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		external/bellagio/src \
		external/bellagio/include \
		external/libuiomux/include \
		$(LOCAL_PATH)/../vpu5_mid/include \
		$(LOCAL_PATH)/../../include

LOCAL_SRC_FILES := 	\
	shvpu5_common_uio.c \
	shvpu5_common_log.c

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libvpu5uio
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DANDROID
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := liblog  \
			libcutils \
			libbellcore \
			libvpu5uio

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		external/bellagio/src \
		external/bellagio/include \
		external/libuiomux/include \
		$(LOCAL_PATH)/../vpu5_mid/include \
		$(LOCAL_PATH)/../../include

LOCAL_SRC_FILES := 	\
	shvpu5_common_sync.c \
	shvpu5_common_udfio.c \
	shvpu5_common_log.c

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libvpu5udf
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/avc_dec/\" -DANDROID
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := liblog  \
			libcutils \
			libuiomux \
			libbellcore \
			libvpu5uio

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		external/bellagio/src \
		external/bellagio/include \
		external/libuiomux/include \
		$(LOCAL_PATH)/../vpu5_mid/include \
		$(LOCAL_PATH)/../../include

LOCAL_SRC_FILES := 	\
	shvpu5_common_queue.c \
	shvpu5_avcdec_output.c \
	shvpu5_avcdec_input.c \
	shvpu5_common_log.c

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libvpu5udfdec
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/avc_dec/\" -DANDROID
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := liblog  \
			libcutils \
			libuiomux \
			libbellcore \
			libvpu5uio

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		external/bellagio/src \
		external/bellagio/include \
		external/libuiomux/include \
		$(LOCAL_PATH)/../vpu5_mid/include \
		$(LOCAL_PATH)/../../include

LOCAL_SRC_FILES := 	\
	shvpu5_avcenc_output.c \
	shvpu5_common_log.c

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libvpu5udfenc
LOCAL_CFLAGS:= -DLOG_TAG=\"shvpudec\" -DVPU5HG_FIRMWARE_PATH=\"/system/lib/firmware/avc_dec/\" -DANDROID
include $(BUILD_SHARED_LIBRARY)
