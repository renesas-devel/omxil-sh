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

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := liblog  \
			libcutils \
			libbellcore \
			libuiomux

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
		hardware/renesas/shmiddle/spu_mid/include \
		external/bellagio/src \
		external/bellagio/include \
		external/libuiomux/include

LOCAL_SRC_FILES := \
	library_entry_point.c \
	omx_audiodec_component.c \
	spu2helper/spuaacdec.c \
	spu2helper/spu.c \
	spu2helper/spu_dsp.c \
	spu2helper/uio.c \
	spu2helper/uiohelper.c

# LOCAL_WHOLE_STATIC_LIBRARIES := 

LOCAL_LDFLAGS = -Lhardware/renesas/shmiddle/spu_mid/lib \
	-lshspuaacdec

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libshspu2aac
LOCAL_CFLAGS:= -DLOG_TAG=\"shspudec\" -DLIBSPUHELPERAACDEC

include $(BUILD_SHARED_LIBRARY)
