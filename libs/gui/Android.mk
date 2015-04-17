LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	IGraphicBufferConsumer.cpp \
	IConsumerListener.cpp \
	BitTube.cpp \
	BufferItemConsumer.cpp \
	BufferQueue.cpp \
	ConsumerBase.cpp \
	CpuConsumer.cpp \
	DisplayEventReceiver.cpp \
	GLConsumer.cpp \
	GraphicBufferAlloc.cpp \
	GuiConfig.cpp \
	IDisplayEventConnection.cpp \
	IGraphicBufferAlloc.cpp \
	IGraphicBufferProducer.cpp \
	ISensorEventConnection.cpp \
	ISensorServer.cpp \
	ISurfaceComposer.cpp \
	ISurfaceComposerClient.cpp \
	LayerState.cpp \
	Sensor.cpp \
	SensorEventQueue.cpp \
	SensorManager.cpp \
	Surface.cpp \
	SurfaceControl.cpp \
	SurfaceComposerClient.cpp \
	SyncFeatures.cpp \

LOCAL_SHARED_LIBRARIES := \
	libbinder \
	libcutils \
	libEGL \
	libGLESv2 \
	libsync \
	libui \
	libutils \
	liblog


LOCAL_MODULE:= libgui

# MStar Android Patch Begin
ifneq ($(BUILD_MSTARTV),)
	LOCAL_C_INCLUDES += \
		$(TARGET_UTOPIA_LIBS_DIR)/include \
		hardware/mstar/libvsyncbridge \
		hardware/mstar/libstagefrighthw/yuv \
		hardware/mstar/omx/ms_codecs/video/mm_asic/include
	LOCAL_CFLAGS += -DBUILD_MSTARTV
	LOCAL_SHARED_LIBRARIES += \
		libvsyncbridge \
		libstagefrighthw_yuv
endif
# MStar Android Patch End

ifeq ($(TARGET_BOARD_PLATFORM), tegra)
	LOCAL_CFLAGS += -DDONT_USE_FENCE_SYNC
endif
ifeq ($(TARGET_BOARD_PLATFORM), tegra3)
	LOCAL_CFLAGS += -DDONT_USE_FENCE_SYNC
endif

# MStar Android Patch Begin
ifeq ($(ENABLE_HWCOMPOSER_13),true)
       LOCAL_CFLAGS += -DENABLE_HWCOMPOSER_13
endif
ifeq ($(ENABLE_HWCURSOR),true)
    LOCAL_CFLAGS += -DENABLE_HWCURSOR
endif
ifeq ($(BUILD_FOR_STB),true)
    LOCAL_CFLAGS += -DBUILD_FOR_STB
endif
ifeq ($(ENABLE_GOP_HW_COMPOSER),true)
    LOCAL_CFLAGS += -DENABLE_GOP_HW_COMPOSER
endif
# MStar Android Patch End

include $(BUILD_SHARED_LIBRARY)

ifeq (,$(ONE_SHOT_MAKEFILE))
include $(call first-makefiles-under,$(LOCAL_PATH))
endif
