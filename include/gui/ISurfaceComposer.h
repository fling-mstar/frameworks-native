/*
 * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_GUI_ISURFACE_COMPOSER_H
#define ANDROID_GUI_ISURFACE_COMPOSER_H

#include <stdint.h>
#include <sys/types.h>

#include <utils/RefBase.h>
#include <utils/Errors.h>

#include <binder/IInterface.h>

#include <ui/PixelFormat.h>

#include <gui/IGraphicBufferAlloc.h>
#include <gui/ISurfaceComposerClient.h>

// MStar Android Patch Begin
#ifdef ENABLE_HWCURSOR
#include <binder/IMemory.h>
#endif
// MStar Android Patch End

namespace android {
// ----------------------------------------------------------------------------

class ComposerState;
class DisplayState;
class DisplayInfo;
class IDisplayEventConnection;
class IMemoryHeap;

/*
 * This class defines the Binder IPC interface for accessing various
 * SurfaceFlinger features.
 */
class ISurfaceComposer: public IInterface {
public:
    DECLARE_META_INTERFACE(SurfaceComposer);

    // flags for setTransactionState()
    enum {
        eSynchronous = 0x01,
        eAnimation   = 0x02,
    };

    enum {
        eDisplayIdMain = 0,
        eDisplayIdHdmi = 1
    };

    /* create connection with surface flinger, requires
     * ACCESS_SURFACE_FLINGER permission
     */
    virtual sp<ISurfaceComposerClient> createConnection() = 0;

    /* create a graphic buffer allocator
     */
    virtual sp<IGraphicBufferAlloc> createGraphicBufferAlloc() = 0;

    /* return an IDisplayEventConnection */
    virtual sp<IDisplayEventConnection> createDisplayEventConnection() = 0;

    /* create a virtual display
     * requires ACCESS_SURFACE_FLINGER permission.
     */
    virtual sp<IBinder> createDisplay(const String8& displayName,
            bool secure) = 0;

    /* destroy a virtual display
     * requires ACCESS_SURFACE_FLINGER permission.
     */
    virtual void destroyDisplay(const sp<IBinder>& display) = 0;

    /* get the token for the existing default displays. possible values
     * for id are eDisplayIdMain and eDisplayIdHdmi.
     */
    virtual sp<IBinder> getBuiltInDisplay(int32_t id) = 0;

    /* open/close transactions. requires ACCESS_SURFACE_FLINGER permission */
    virtual void setTransactionState(const Vector<ComposerState>& state,
            const Vector<DisplayState>& displays, uint32_t flags) = 0;

    /* signal that we're done booting.
     * Requires ACCESS_SURFACE_FLINGER permission
     */
    virtual void bootFinished() = 0;

    /* verify that an IGraphicBufferProducer was created by SurfaceFlinger.
     */
    virtual bool authenticateSurfaceTexture(
            const sp<IGraphicBufferProducer>& surface) const = 0;

    /* triggers screen off and waits for it to complete
     * requires ACCESS_SURFACE_FLINGER permission.
     */
    virtual void blank(const sp<IBinder>& display) = 0;

    /* triggers screen on and waits for it to complete
     * requires ACCESS_SURFACE_FLINGER permission.
     */
    virtual void unblank(const sp<IBinder>& display) = 0;

    /* returns information about a display
     * intended to be used to get information about built-in displays */
    virtual status_t getDisplayInfo(const sp<IBinder>& display, DisplayInfo* info) = 0;

    /* Capture the specified screen. requires READ_FRAME_BUFFER permission
     * This function will fail if there is a secure window on screen.
     */
    virtual status_t captureScreen(const sp<IBinder>& display,
            const sp<IGraphicBufferProducer>& producer,
            uint32_t reqWidth, uint32_t reqHeight,
            uint32_t minLayerZ, uint32_t maxLayerZ) = 0;

    // MStar Android Patch Begin
    virtual status_t setAutoStereoMode(int32_t identity, int32_t autoStereo) = 0;
    virtual status_t setBypassTransformMode(int32_t identity, int32_t bypassTransform) = 0;
    virtual status_t setPanelMode(int32_t panelMode) = 0;
    virtual status_t getPanelMode(int32_t *panelMode) = 0;
    virtual status_t setGopStretchWin(int32_t gopNo, int32_t dest_Width, int32_t dest_Height) = 0;
#ifdef ENABLE_HWCURSOR
    virtual status_t setHwCursorShow() = 0;
    virtual status_t setHwCursorHide() = 0;
    virtual status_t setHwCursorMatrix(float dsdx, float dtdx, float dsdy, float dtdy) = 0;
    virtual status_t setHwCursorPosition(float positionX, float positionY, float hotSpotX, float hotSpotY, int iconWidth, int iconHeight) =0;
    virtual status_t setHwCursorAlpha(float alpha) = 0;
    virtual status_t changeHwCursorIcon() = 0;
    virtual status_t doHwCursorTransaction() = 0;
    virtual status_t getHwCursorWidth(int32_t* cursorWidth) = 0;
    virtual status_t getHwCursorHeight(int32_t* cursorHeight) = 0;
    virtual status_t getHwCursorStride(int32_t* cursorStride) = 0;
    virtual sp<IMemory> getHwCursorIconBuf() = 0;
    virtual status_t loadHwCursorModule() = 0;
#endif
    virtual status_t setSurfaceResolutionMode(int32_t width, int32_t height, int32_t hstart,int32_t interleave,int32_t orientation, int32_t value) = 0;
    // MStar Android Patch End
};

// ----------------------------------------------------------------------------

class BnSurfaceComposer: public BnInterface<ISurfaceComposer> {
public:
    enum {
        // Note: BOOT_FINISHED must remain this value, it is called from
        // Java by ActivityManagerService.
        BOOT_FINISHED = IBinder::FIRST_CALL_TRANSACTION,
        CREATE_CONNECTION,
        CREATE_GRAPHIC_BUFFER_ALLOC,
        CREATE_DISPLAY_EVENT_CONNECTION,
        CREATE_DISPLAY,
        DESTROY_DISPLAY,
        GET_BUILT_IN_DISPLAY,
        SET_TRANSACTION_STATE,
        AUTHENTICATE_SURFACE,
        BLANK,
        UNBLANK,
        GET_DISPLAY_INFO,
        CONNECT_DISPLAY,
        CAPTURE_SCREEN,
        // MStar Android Patch Begin
        SET_AUTO_STEREO_MODE,
        SET_BYPASS_TRANSFORM_MODE,
        SET_PANEL_MODE,
        SET_SURFACE_RESOLUTION_MODE,
        GET_PANEL_MODE,
        SET_GOP_STRETCH_WIN,
 #ifdef ENABLE_HWCURSOR
        SET_HWCURSOR_SHOW,
        SET_HWCURSOR_HIDE,
        SET_HWCURSOR_MATRIX,
        SET_HWCURSOR_POSITION,
        SET_HWCURSOR_ALPHA,
        CHANGE_HWCURSOR_ICON,
        DO_HWCURSOR_TRANSACTION,
        GET_HWCURSOR_ICONBUF,
        GET_HWCURSOR_WIDTH,
        GET_HWCURSOR_HEIGHT,
        GET_HWCURSOR_STRIDE,
        LOAD_HWCURSOR_MODULE,
 #endif
        // MStar Android Patch End
    };

    virtual status_t onTransact(uint32_t code, const Parcel& data,
            Parcel* reply, uint32_t flags = 0);
};

// ----------------------------------------------------------------------------

}; // namespace android

#endif // ANDROID_GUI_ISURFACE_COMPOSER_H
