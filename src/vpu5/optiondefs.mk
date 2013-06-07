## T/L Conversion settings
#TL Coversion mode: Some versions support T/L conversion in the vpu
ifeq ($(VPU_INTERNAL_TL), true)
	VPU_OPTION_DEFS += -DVPU_INTERNAL_TL
endif
ifneq ($(VPU_TL_TILE_WIDTH_LOG2),)
	VPU_OPTION_DEFS += -DTL_TILE_WIDTH_LOG2=$(VPU_TL_TILE_WIDTH_LOG2)
endif
ifneq ($(VPU_TL_TILE_HEIGHT_LOG2),)
	VPU_OPTION_DEFS += -DTL_TILE_HEIGHT_LOG2=$(VPU_TL_TILE_HEIGHT_LOG2)
endif

ifeq ($(LOCAL_TL_CONV), uio)
	VPU_OPTION_DEFS += -DTL_CONV_ENABLE -DUIO_TL_CONV
else ifeq ($(LOCAL_TL_CONV), kernel)
	VPU_OPTION_DEFS += -DTL_CONV_ENABLE
endif

#MERAM Settings
ifeq ($(VPU_DECODE_WITH_MERAM), true)
	VPU_OPTION_DEFS += -DNEEDS_MERAM_LIB
	VPU_OPTION_INCLUDES += hardware/renesas/shmobile/libshmeram/include
endif
ifeq ($(VPU_DECODE_USE_2DDMAC), true)
	VPU_OPTION_DEFS += -DNEEDS_MERAM_LIB
	VPU_OPTION_INCLUDES += hardware/renesas/shmobile/libshmeram/include
endif

#2DDMAC Settings
ifeq ($(VPU_DECODE_USE_2DDMAC), true)
	VPU_OPTION_DEFS += -DDMAC_MODE
	VPU_OPTION_INCLUDES += hardware/renesas/shmobile/libtddmac/include
endif

