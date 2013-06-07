#This file defines any version specific defines to enable/disable or
#change modes of options that vary by VPU version.
#
#define VPU "series's". i.e. groups of VPU versions that share the majority
#of settings

ifeq ($(PRODUCT_VPU_VERSION), VPU_VERSION_5)
	VPU_SERIES := VPU5_SERIES
endif
ifeq ($(PRODUCT_VPU_VERSION), VPU_VERSION_5HA)
	VPU_SERIES := VPU5HA_SERIES
endif
ifeq ($(PRODUCT_VPU_VERSION), VPU_VERSION_5HD)
	VPU_SERIES := VPU5HA_SERIES
endif
ifeq ($(PRODUCT_VPU_VERSION), VPU_VERSION_VCP1)
	VPU_SERIES := VPU5HA_SERIES
endif

ifeq ($(VPU_SERIES),)
$(error Unknown VPU setting: $(PRODUCT_VPU_VERSION))
endif

VPU_VERSION_DEFS += -D$(PRODUCT_VPU_VERSION) -D$(VPU_SERIES)

#VPU memory type: IPMMUI allocation or UIOMUX(default) allocation
ifeq ($(VPU_MEMORY_TYPE),)
	VPU_MEMORY_TYPE := uio
endif
ifeq ($(VPU_MEMORY_TYPE),uio)
	VPU_VERSION_DEFS += -DVPU_UIO_MEMORY
endif
ifeq ($(VPU_MEMORY_TYPE),ipmmui)
	VPU_VERSION_DEFS += -DVPU_IPMMU_MEMORY
endif
