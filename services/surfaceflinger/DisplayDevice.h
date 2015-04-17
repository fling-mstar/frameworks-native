/*
 * Copyright (C) 2007 The Android Open Source Project
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

#ifndef ANDROID_DISPLAY_DEVICE_H
#define ANDROID_DISPLAY_DEVICE_H

#include <stdlib.h>

#include <ui/PixelFormat.h>
#include <ui/Region.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <utils/Mutex.h>
#include <utils/String8.h>
#include <utils/Timers.h>

#include <hardware/hwcomposer_defs.h>

#include "Transform.h"

// MStar Android Patch Begin
#ifndef ENABLE_HWCOMPOSER_13
#define DISPLAYMODE_NORMAL                          0
#define DISPLAYMODE_LEFTRIGHT                       1
#define DISPLAYMODE_TOPBOTTOM                       2
#define DISPLAYMODE_TOPBOTTOM_LA                    3
#define DISPLAYMODE_NORMAL_LA                       4
#define DISPLAYMODE_TOP_LA                          5
#define DISPLAYMODE_BOTTOM_LA                       6
#define DISPLAYMODE_LEFT_ONLY                       7
#define DISPLAYMODE_RIGHT_ONLY                      8
#define DISPLAYMODE_TOP_ONLY                        9
#define DISPLAYMODE_BOTTOM_ONLY                     10
#define DISPLAYMODE_LEFTRIGHT_FR                    11 // Duplicate Left to right and frame sequence output
#define DISPLAYMODE_NORMAL_FR                       12 // Normal frame sequence output
#define DISPLAYMODE_TOPBOTTOM_HIGH_QUALITY          14
#define DISPLAYMODE_TOPBOTTOM_LA_HIGH_QUALITY       15
#define DISPLAYMODE_NORMAL_LA_HIGH_QUALITY          16
#define DISPLAYMODE_TOP_LA_HIGH_QUALITY             17
#define DISPLAYMODE_BOTTOM_LA_HIGH_QUALITY          18
#define DISPLAYMODE_LEFTRIGHT_FULL                  19
#define DISPLAYMODE_MAX                             20
#endif
// MStar Android Patch End

struct ANativeWindow;

namespace android {

class DisplayInfo;
class DisplaySurface;
class IGraphicBufferProducer;
class Layer;
class SurfaceFlinger;
class HWComposer;

class DisplayDevice : public LightRefBase<DisplayDevice>
{
public:
    // region in layer-stack space
    mutable Region dirtyRegion;
    // region in screen space
    mutable Region swapRegion;
    // region in screen space
    Region undefinedRegion;

    enum DisplayType {
        DISPLAY_ID_INVALID = -1,
        DISPLAY_PRIMARY     = HWC_DISPLAY_PRIMARY,
        DISPLAY_EXTERNAL    = HWC_DISPLAY_EXTERNAL,
        DISPLAY_VIRTUAL     = HWC_DISPLAY_VIRTUAL,
        NUM_BUILTIN_DISPLAY_TYPES = HWC_NUM_PHYSICAL_DISPLAY_TYPES,
    };

    enum {
        PARTIAL_UPDATES = 0x00020000, // video driver feature
        SWAP_RECTANGLE  = 0x00080000,
    };

    enum {
        NO_LAYER_STACK = 0xFFFFFFFF,
    };

    DisplayDevice(
            const sp<SurfaceFlinger>& flinger,
            DisplayType type,
            int32_t hwcId,  // negative for non-HWC-composited displays
            bool isSecure,
            const wp<IBinder>& displayToken,
            const sp<DisplaySurface>& displaySurface,
            const sp<IGraphicBufferProducer>& producer,
            EGLConfig config);

    ~DisplayDevice();

    // whether this is a valid object. An invalid DisplayDevice is returned
    // when an non existing id is requested
    bool isValid() const;

    // isSecure indicates whether this display can be trusted to display
    // secure surfaces.
    bool isSecure() const { return mIsSecure; }

    // Flip the front and back buffers if the back buffer is "dirty".  Might
    // be instantaneous, might involve copying the frame buffer around.
    void flip(const Region& dirty) const;

    int         getWidth() const;
    int         getHeight() const;
    PixelFormat getFormat() const;
    uint32_t    getFlags() const;

    // MStar Android Patch Begin
    int         getOsdWidth() const;
    int         getOsdHeight() const;
    void getStbResolution(int* Width, int* Height, bool *bResChanged);
    String8 mCurrentResState;
#ifdef BUILD_FOR_STB
    void reSetPanelSize(int panelwidth, int panelheight);
    status_t reSetGopSize(int width, int height, int hstart, int interleave, int orientation, int value);
    status_t getAdjustValue(int *adjustwidth, int *adjustheight,int * adjustloffset,int *adjusttopoffset)const;
    void setDisplayWindow();
    int getRealDisplayHeight()const {return mRealDisplayHeight;}
    int getRealDisplayWidth()const {return mRealDisplayWidth;}
    String8 getCurResState()const {return mCurrentResState;}
    void setCurResState(char* curResState) {mCurrentResState=curResState;}
    void setRealResolute(int width, int height);
#endif
    void setTransform(const Transform& newGlobalTransform) { mGlobalTransform = newGlobalTransform;}
    // MStar Android Patch End
    EGLSurface  getEGLSurface() const;

    void                    setVisibleLayersSortedByZ(const Vector< sp<Layer> >& layers);
    const Vector< sp<Layer> >& getVisibleLayersSortedByZ() const;
    bool                    getSecureLayerVisible() const;
    Region                  getDirtyRegion(bool repaintEverything) const;

    void                    setLayerStack(uint32_t stack);
    void                    setProjection(int orientation, const Rect& viewport, const Rect& frame);
    // MStar Android Patch Begin
    void                    setProjectionPlatform(int orientation,       const Rect& newViewport, const Rect& newFrame);
    const Transform*        getLeftTransform() const { return mLeftGlobalTransform; }
    const Transform*        getRightTransform() const { return mRightGlobalTransform; }
    const Rect&             getLeftScissor() const { return mLeftScissor; }
    const Rect&             getRightScissor() const { return mRightScissor; }
    // return: overscan changed?
    bool                    setOverscan(int leftOverscan, int topOverscan, int rightOverscan, int bottomOverscan);
    void                    getOverscan(int *outLeftOverscan, int *outTopOverscan, int *outRightOverscan, int *outBottomOverscan) const;
    // set true for screen, false for capture screen
    void                    setOverscanAnd3DEnabled(bool enable);
    bool                    isOverscanAnd3DEnabled() const;
    void                    recalc3DTransformAndScissor();
    // MStar Android Patch End

    int                     getOrientation() const { return mOrientation; }
    uint32_t                getOrientationTransform() const;

    // MStar Android Patch Begin
    const Transform&        getTransform() const;
    const Rect              getViewport() const;
    const Rect              getFrame() const;
    const Rect&             getScissor() const;
    bool                    needsFiltering() const;
    // MStar Android Patch End

    uint32_t                getLayerStack() const { return mLayerStack; }
    int32_t                 getDisplayType() const { return mType; }
    int32_t                 getHwcDisplayId() const { return mHwcDisplayId; }
    const wp<IBinder>&      getDisplayToken() const { return mDisplayToken; }

    status_t beginFrame() const;
    status_t prepareFrame(const HWComposer& hwc) const;

    void swapBuffers(HWComposer& hwc) const;
    status_t compositionComplete() const;

    // called after h/w composer has completed its set() call
    void onSwapBuffersCompleted(HWComposer& hwc) const;

    Rect getBounds() const {
        return Rect(mDisplayWidth, mDisplayHeight);
    }
    inline Rect bounds() const { return getBounds(); }

    void setDisplayName(const String8& displayName);
    const String8& getDisplayName() const { return mDisplayName; }

    EGLBoolean makeCurrent(EGLDisplay dpy, EGLContext ctx) const;
    void setViewportAndProjection() const;

    /* ------------------------------------------------------------------------
     * blank / unblank management
     */
    void releaseScreen() const;
    void acquireScreen() const;
    bool isScreenAcquired() const;
    bool canDraw() const;

    // release HWC resources (if any) for removable displays
    void disconnect(HWComposer& hwc);

    /* ------------------------------------------------------------------------
     * Debugging
     */
    uint32_t getPageFlipCount() const;
    void dump(String8& result) const;

private:
    /*
     *  Constants, set during initialization
     */
    sp<SurfaceFlinger> mFlinger;
    DisplayType mType;
    int32_t mHwcDisplayId;
    wp<IBinder> mDisplayToken;

    // ANativeWindow this display is rendering into
    sp<ANativeWindow> mNativeWindow;
    sp<DisplaySurface> mDisplaySurface;

    EGLDisplay      mDisplay;
    EGLSurface      mSurface;
    int             mDisplayWidth;
    int             mDisplayHeight;
    // MStar Android Patch Begin
    int             mOsdWidth;
    int             mOsdHeight;
    // MStar Android Patch End
    PixelFormat     mFormat;
    uint32_t        mFlags;
    mutable uint32_t mPageFlipCount;
    String8         mDisplayName;
    bool            mIsSecure;

    /*
     * Can only accessed from the main thread, these members
     * don't need synchronization.
     */

    // list of visible layers on that display
    Vector< sp<Layer> > mVisibleLayersSortedByZ;

    // Whether we have a visible secure layer on this display
    bool mSecureLayerVisible;

    // Whether the screen is blanked;
    mutable int mScreenAcquired;


    /*
     * Transaction state
     */
    static status_t orientationToTransfrom(int orientation,
            int w, int h, Transform* tr);

    // MStar Android Patch Begin
#ifdef BUILD_FOR_STB
    void setOrientation(int orientation);
    Transform mDisplayTransform;
    int mRealDisplayWidth;
    int mRealDisplayHeight;
    void initRealDisplaySize();
#endif
    // MStar Android Patch End

    uint32_t mLayerStack;
    int mOrientation;
    // user-provided visible area of the layer stack
    Rect mViewport;
    // user-provided rectangle where mViewport gets mapped to
    Rect mFrame;
    // pre-computed scissor to apply to the display
    Rect mScissor;
    Transform mGlobalTransform;
    bool mNeedsFiltering;

    // MStar Android branch Begin
    bool mOverscanAnd3DEnabled;
    int mLeftOverscan, mTopOverscan, mRightOverscan, mBottomOverscan;
    Rect mFramePlatfom;
    Rect mScissorPlatform;
    Transform mGlobalTransformPlatform;
    Transform* mLeftGlobalTransform, *mRightGlobalTransform;
    Rect mLeftScissor, mRightScissor;
public:
    void changeGlobalTransform(bool has4k2kLayer);
    void changeGlobalTransform(Rect& newFrame);
    void setAndroidWidthAndHeight(int width, int height){
        osdWidthForAndroid = width;
        osdHeightForAndroid = height;
    }
    int getOsdWidthForAndroid() const { return osdWidthForAndroid;}
    int getOsdHeightForAndroid() const { return osdHeightForAndroid;}
    int osdWidthForAndroid;
    int osdHeightForAndroid;
    bool skipSwapBuffer;
    bool mHas4k2kLayer;
    // MStar Android branch End
};

}; // namespace android

#endif // ANDROID_DISPLAY_DEVICE_H
