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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <stdint.h>
#include <sys/types.h>
#include <errno.h>
#include <math.h>
#include <dlfcn.h>

#include <EGL/egl.h>

#include <cutils/log.h>
#include <cutils/properties.h>

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/MemoryHeapBase.h>
#include <binder/PermissionCache.h>

#include <ui/DisplayInfo.h>

#include <gui/BitTube.h>
#include <gui/BufferQueue.h>
#include <gui/GuiConfig.h>
#include <gui/IDisplayEventConnection.h>
#include <gui/Surface.h>
#include <gui/GraphicBufferAlloc.h>

#include <ui/GraphicBufferAllocator.h>
#include <ui/PixelFormat.h>
#include <ui/UiConfig.h>

#include <utils/misc.h>
#include <utils/String8.h>
#include <utils/String16.h>
#include <utils/StopWatch.h>
#include <utils/Trace.h>

#include <private/android_filesystem_config.h>
#include <private/gui/SyncFeatures.h>

// MStar Android Patch Begin
#include <camera/Camera.h>
#include <camera/CameraParameters.h>
// MStar Android Patch End

#include "Client.h"
#include "clz.h"
#include "Colorizer.h"
#include "DdmConnection.h"
#include "DisplayDevice.h"
#include "DispSync.h"
#include "EventControlThread.h"
#include "EventThread.h"
#include "Layer.h"
#include "LayerDim.h"
#include "SurfaceFlinger.h"

#include "DisplayHardware/FramebufferSurface.h"
#include "DisplayHardware/HWComposer.h"
#include "DisplayHardware/VirtualDisplaySurface.h"

#include "Effects/Daltonizer.h"

#include "RenderEngine/RenderEngine.h"
#include <cutils/compiler.h>

// MStar Android Patch Begin
#include <android/configuration.h>

#define MSTAR_DESK_DISPLAY_MODE    "mstar.desk-display-mode"
#define SCREENCAP_WIDTH            1920
#define SCREENCAP_HEIGHT           1080
#define CAMERA_ID_XCDIP2           7
#define CAMERA_ID_XCDIP1           6
#define CAMERA_ID_XCDIP0           5
#define URSA_7                     7
#define URSA_9                     9
#ifdef ENABLE_HWCURSOR
#define HWCURSOR_ICON_BUFFER       "HwcursorIconBuffer"
#define HWCURSOR_WIDTH             128
#define HWCURSOR_HEIGHT            128
#endif
// MStar Android Patch End
#define DISPLAY_COUNT       1

/*
 * DEBUG_SCREENSHOTS: set to true to check that screenshots are not all
 * black pixels.
 */
#define DEBUG_SCREENSHOTS   false

EGLAPI const char* eglQueryStringImplementationANDROID(EGLDisplay dpy, EGLint name);

namespace android {

// This works around the lack of support for the sync framework on some
// devices.
#ifdef RUNNING_WITHOUT_SYNC_FRAMEWORK
static const bool runningWithoutSyncFramework = true;
#else
static const bool runningWithoutSyncFramework = false;
#endif

// This is the phase offset in nanoseconds of the software vsync event
// relative to the vsync event reported by HWComposer.  The software vsync
// event is when SurfaceFlinger and Choreographer-based applications run each
// frame.
//
// This phase offset allows adjustment of the minimum latency from application
// wake-up (by Choregographer) time to the time at which the resulting window
// image is displayed.  This value may be either positive (after the HW vsync)
// or negative (before the HW vsync).  Setting it to 0 will result in a
// minimum latency of two vsync periods because the app and SurfaceFlinger
// will run just after the HW vsync.  Setting it to a positive number will
// result in the minimum latency being:
//
//     (2 * VSYNC_PERIOD - (vsyncPhaseOffsetNs % VSYNC_PERIOD))
//
// Note that reducing this latency makes it more likely for the applications
// to not have their window content image ready in time.  When this happens
// the latency will end up being an additional vsync period, and animations
// will hiccup.  Therefore, this latency should be tuned somewhat
// conservatively (or at least with awareness of the trade-off being made).
static const int64_t vsyncPhaseOffsetNs = VSYNC_EVENT_PHASE_OFFSET_NS;

// This is the phase offset at which SurfaceFlinger's composition runs.
static const int64_t sfVsyncPhaseOffsetNs = SF_VSYNC_EVENT_PHASE_OFFSET_NS;

// ---------------------------------------------------------------------------

const String16 sHardwareTest("android.permission.HARDWARE_TEST");
const String16 sAccessSurfaceFlinger("android.permission.ACCESS_SURFACE_FLINGER");
const String16 sReadFramebuffer("android.permission.READ_FRAME_BUFFER");
const String16 sDump("android.permission.DUMP");

// ---------------------------------------------------------------------------

// MStar Android Patch Begin
class ScreenCaptureListener: public CameraListener {
public:
    ScreenCaptureListener(const sp<SurfaceFlinger> &surfaceflinger);
    virtual void notify(int32_t msgType, int32_t ext1, int32_t ext2);
    virtual void postData(int32_t msgType, const sp<IMemory>& dataPtr,camera_frame_metadata_t *metadata);
    virtual void postDataTimestamp(nsecs_t timestamp, int32_t msgType, const sp<IMemory>& dataPtr);

protected:
    virtual ~ScreenCaptureListener();

private:
    sp<SurfaceFlinger> mSurfaceFlinger;
};

ScreenCaptureListener::ScreenCaptureListener(const sp<SurfaceFlinger> &surfaceflinger)
    : mSurfaceFlinger(surfaceflinger) {
}

ScreenCaptureListener::~ScreenCaptureListener() {
}

void ScreenCaptureListener::notify(int32_t msgType, int32_t ext1, int32_t ext2) {
}

void ScreenCaptureListener::postData(int32_t msgType, const sp<IMemory>& dataPtr,camera_frame_metadata_t *metadata) {
    ssize_t offset;
    size_t size;
    sp<IMemoryHeap> heap = dataPtr->getMemory(&offset, &size);

    Mutex::Autolock _l(mSurfaceFlinger->mFrameAvailableLock);
    mSurfaceFlinger->mCaptureData = dataPtr;
    mSurfaceFlinger->mFrameAvailableCV.signal();
    // MStar Android Patch Begin
    //add debug infomation :save capture data to /data/capture/
    char property[PROPERTY_VALUE_MAX];
    if (property_get("mstar.debug.capturedata", property, "0") > 0) {
        if (strcmp(property, "1") == 0) {
            char filename[100];
            int num  = -1;
            while (++num < 10000) {
                snprintf(filename, 100, "%s/%s_%04d.raw",
                         "/data/capture/","captureDate",  num);

                if (access(filename, F_OK) != 0) {
                    snprintf(filename, 100, "%s/%s_%04d.raw",
                             "/data/capture","captureDate",  num);

                    if (access(filename, F_OK) != 0)
                        break;
                }
            }
            ALOGD("captureDate filename  = %s",filename);
            int fd_p = open(filename, O_EXCL | O_CREAT | O_WRONLY, 0644);
            if (fd_p < 0) {
                ALOGD("%s  can't open",filename);
                return;
            }
            for (int i=0; i<SCREENCAP_HEIGHT; i++) {
                uint8_t* date = ((uint8_t *)heap->base() + offset)+i*SCREENCAP_WIDTH*4;
                write(fd_p, date, SCREENCAP_WIDTH*4);
            }
            close(fd_p);
        }
    }
    // MStar Android Patch End
}

void ScreenCaptureListener::postDataTimestamp(nsecs_t timestamp, int32_t msgType, const sp<IMemory>& dataPtr) {
}
// MStar Android Patch End

SurfaceFlinger::SurfaceFlinger()
    :   BnSurfaceComposer(),
        mTransactionFlags(0),
        mTransactionPending(false),
        mAnimTransactionPending(false),
        mLayersRemoved(false),
        mRepaintEverything(0),
        mRenderEngine(NULL),
        mBootTime(systemTime()),
        mVisibleRegionsDirty(false),
        mHwWorkListDirty(false),
        mAnimCompositionPending(false),
        mDebugRegion(0),
        mDebugDDMS(0),
        mDebugDisableHWC(0),
        mDebugDisableTransformHint(0),
        mDebugInSwapBuffers(0),
        mLastSwapBufferTime(0),
        mDebugInTransaction(0),
        mLastTransactionTime(0),
        mBootFinished(false),
        mPrimaryHWVsyncEnabled(false),
        mHWVsyncAvailable(false),
        mDaltonize(false)
        // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
        ,mLeftOverscan(0),
        mTopOverscan(0),
        mRightOverscan(0),
        mBottomOverscan(0)
#endif
        // MStar Android Patch End
{
    ALOGI("SurfaceFlinger is starting");

    // debugging stuff...
    char value[PROPERTY_VALUE_MAX];

    property_get("ro.bq.gpu_to_cpu_unsupported", value, "0");
    mGpuToCpuSupported = !atoi(value);

    property_get("debug.sf.showupdates", value, "0");
    mDebugRegion = atoi(value);

    property_get("debug.sf.ddms", value, "0");
    mDebugDDMS = atoi(value);
    if (mDebugDDMS) {
        if (!startDdmConnection()) {
            // start failed, and DDMS debugging not enabled
            mDebugDDMS = 0;
        }
    }
    ALOGI_IF(mDebugRegion, "showupdates enabled");
    ALOGI_IF(mDebugDDMS, "DDMS debugging enabled");
}

void SurfaceFlinger::onFirstRef()
{
    mEventQueue.init(this);
}

SurfaceFlinger::~SurfaceFlinger()
{
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(display);
    // MStar Android Patch Begin
#ifdef ENABLE_HWCURSOR
    mHwcursorIcon = NULL;
#endif
    // MStar Android Patch End
}

void SurfaceFlinger::binderDied(const wp<IBinder>& who)
{
    // the window manager died on us. prepare its eulogy.

    // restore initial conditions (default device unblank, etc)
    initializeDisplays();

    // restart the boot-animation
    startBootAnim();
}

sp<ISurfaceComposerClient> SurfaceFlinger::createConnection()
{
    sp<ISurfaceComposerClient> bclient;
    sp<Client> client(new Client(this));
    status_t err = client->initCheck();
    if (err == NO_ERROR) {
        bclient = client;
    }
    return bclient;
}

sp<IBinder> SurfaceFlinger::createDisplay(const String8& displayName,
        bool secure)
{
    class DisplayToken : public BBinder {
        sp<SurfaceFlinger> flinger;
        virtual ~DisplayToken() {
             // no more references, this display must be terminated
             Mutex::Autolock _l(flinger->mStateLock);
             flinger->mCurrentState.displays.removeItem(this);
             flinger->setTransactionFlags(eDisplayTransactionNeeded);
         }
     public:
        DisplayToken(const sp<SurfaceFlinger>& flinger)
            : flinger(flinger) {
        }
    };

    sp<BBinder> token = new DisplayToken(this);

    Mutex::Autolock _l(mStateLock);
    DisplayDeviceState info(DisplayDevice::DISPLAY_VIRTUAL);
    info.displayName = displayName;
    info.isSecure = secure;
    mCurrentState.displays.add(token, info);

    return token;
}

void SurfaceFlinger::destroyDisplay(const sp<IBinder>& display) {
    Mutex::Autolock _l(mStateLock);

    ssize_t idx = mCurrentState.displays.indexOfKey(display);
    if (idx < 0) {
        ALOGW("destroyDisplay: invalid display token");
        return;
    }

    const DisplayDeviceState& info(mCurrentState.displays.valueAt(idx));
    if (!info.isVirtualDisplay()) {
        ALOGE("destroyDisplay called for non-virtual display");
        return;
    }

    mCurrentState.displays.removeItemsAt(idx);
    setTransactionFlags(eDisplayTransactionNeeded);
}

void SurfaceFlinger::createBuiltinDisplayLocked(DisplayDevice::DisplayType type) {
    ALOGW_IF(mBuiltinDisplays[type],
            "Overwriting display token for display type %d", type);
    mBuiltinDisplays[type] = new BBinder();
    DisplayDeviceState info(type);
    // All non-virtual displays are currently considered secure.
    info.isSecure = true;
    mCurrentState.displays.add(mBuiltinDisplays[type], info);
}

sp<IBinder> SurfaceFlinger::getBuiltInDisplay(int32_t id) {
    if (uint32_t(id) >= DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES) {
        ALOGE("getDefaultDisplay: id=%d is not a valid default display id", id);
        return NULL;
    }
    return mBuiltinDisplays[id];
}

sp<IGraphicBufferAlloc> SurfaceFlinger::createGraphicBufferAlloc()
{
    sp<GraphicBufferAlloc> gba(new GraphicBufferAlloc());
    return gba;
}

void SurfaceFlinger::bootFinished()
{
    const nsecs_t now = systemTime();
    const nsecs_t duration = now - mBootTime;
    ALOGI("Boot is finished (%ld ms)", long(ns2ms(duration)) );
    mBootFinished = true;

    // MStar Android Patch Begin
    struct timespec t = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &t);
    ALOGI("[AT][AN][finish boot][%u]\n", (long(ns2ms(nsecs_t(t.tv_sec)*1000000000LL + t.tv_nsec))) );
    // MStar Android Patch End

    // wait patiently for the window manager death
    const String16 name("window");
    sp<IBinder> window(defaultServiceManager()->getService(name));
    if (window != 0) {
        window->linkToDeath(static_cast<IBinder::DeathRecipient*>(this));
    }

    // stop boot animation
    // formerly we would just kill the process, but we now ask it to exit so it
    // can choose where to stop the animation.
    property_set("service.bootanim.exit", "1");
}

void SurfaceFlinger::deleteTextureAsync(uint32_t texture) {
    class MessageDestroyGLTexture : public MessageBase {
        RenderEngine& engine;
        uint32_t texture;
    public:
        MessageDestroyGLTexture(RenderEngine& engine, uint32_t texture)
            : engine(engine), texture(texture) {
        }
        virtual bool handler() {
            engine.deleteTextures(1, &texture);
            return true;
        }
    };
    postMessageAsync(new MessageDestroyGLTexture(getRenderEngine(), texture));
}

status_t SurfaceFlinger::selectConfigForAttribute(
        EGLDisplay dpy,
        EGLint const* attrs,
        EGLint attribute, EGLint wanted,
        EGLConfig* outConfig)
{
    EGLConfig config = NULL;
    EGLint numConfigs = -1, n=0;
    eglGetConfigs(dpy, NULL, 0, &numConfigs);
    EGLConfig* const configs = new EGLConfig[numConfigs];
    eglChooseConfig(dpy, attrs, configs, numConfigs, &n);

    if (n) {
        if (attribute != EGL_NONE) {
            for (int i=0 ; i<n ; i++) {
                EGLint value = 0;
                eglGetConfigAttrib(dpy, configs[i], attribute, &value);
                if (wanted == value) {
                    *outConfig = configs[i];
                    delete [] configs;
                    return NO_ERROR;
                }
            }
        } else {
            // just pick the first one
            *outConfig = configs[0];
            delete [] configs;
            return NO_ERROR;
        }
    }
    delete [] configs;
    return NAME_NOT_FOUND;
}

class EGLAttributeVector {
    struct Attribute;
    class Adder;
    friend class Adder;
    KeyedVector<Attribute, EGLint> mList;
    struct Attribute {
        Attribute() {};
        Attribute(EGLint v) : v(v) { }
        EGLint v;
        bool operator < (const Attribute& other) const {
            // this places EGL_NONE at the end
            EGLint lhs(v);
            EGLint rhs(other.v);
            if (lhs == EGL_NONE) lhs = 0x7FFFFFFF;
            if (rhs == EGL_NONE) rhs = 0x7FFFFFFF;
            return lhs < rhs;
        }
    };
    class Adder {
        friend class EGLAttributeVector;
        EGLAttributeVector& v;
        EGLint attribute;
        Adder(EGLAttributeVector& v, EGLint attribute)
            : v(v), attribute(attribute) {
        }
    public:
        void operator = (EGLint value) {
            if (attribute != EGL_NONE) {
                v.mList.add(attribute, value);
            }
        }
        operator EGLint () const { return v.mList[attribute]; }
    };
public:
    EGLAttributeVector() {
        mList.add(EGL_NONE, EGL_NONE);
    }
    void remove(EGLint attribute) {
        if (attribute != EGL_NONE) {
            mList.removeItem(attribute);
        }
    }
    Adder operator [] (EGLint attribute) {
        return Adder(*this, attribute);
    }
    EGLint operator [] (EGLint attribute) const {
       return mList[attribute];
    }
    // cast-operator to (EGLint const*)
    operator EGLint const* () const { return &mList.keyAt(0).v; }
};

status_t SurfaceFlinger::selectEGLConfig(EGLDisplay display, EGLint nativeVisualId,
    EGLint renderableType, EGLConfig* config) {
    // select our EGLConfig. It must support EGL_RECORDABLE_ANDROID if
    // it is to be used with WIFI displays
    status_t err;
    EGLint wantedAttribute;
    EGLint wantedAttributeValue;

    EGLAttributeVector attribs;
    if (renderableType) {
        attribs[EGL_RENDERABLE_TYPE]            = renderableType;
        attribs[EGL_RECORDABLE_ANDROID]         = EGL_TRUE;
        attribs[EGL_SURFACE_TYPE]               = EGL_WINDOW_BIT|EGL_PBUFFER_BIT;
        attribs[EGL_FRAMEBUFFER_TARGET_ANDROID] = EGL_TRUE;
        attribs[EGL_RED_SIZE]                   = 8;
        attribs[EGL_GREEN_SIZE]                 = 8;
        attribs[EGL_BLUE_SIZE]                  = 8;
        wantedAttribute                         = EGL_NONE;
        wantedAttributeValue                    = EGL_NONE;

    } else {
        // if no renderable type specified, fallback to a simplified query
        wantedAttribute                         = EGL_NATIVE_VISUAL_ID;
        wantedAttributeValue                    = nativeVisualId;
    }

    err = selectConfigForAttribute(display, attribs, wantedAttribute,
        wantedAttributeValue, config);
    if (err == NO_ERROR) {
        EGLint caveat;
        if (eglGetConfigAttrib(display, *config, EGL_CONFIG_CAVEAT, &caveat))
            ALOGW_IF(caveat == EGL_SLOW_CONFIG, "EGL_SLOW_CONFIG selected!");
    }
    return err;
}

class DispSyncSource : public VSyncSource, private DispSync::Callback {
public:
    DispSyncSource(DispSync* dispSync, nsecs_t phaseOffset, bool traceVsync) :
            mValue(0),
            mPhaseOffset(phaseOffset),
            mTraceVsync(traceVsync),
            mDispSync(dispSync) {}

    virtual ~DispSyncSource() {}

    virtual void setVSyncEnabled(bool enable) {
        // Do NOT lock the mutex here so as to avoid any mutex ordering issues
        // with locking it in the onDispSyncEvent callback.
        if (enable) {
            status_t err = mDispSync->addEventListener(mPhaseOffset,
                    static_cast<DispSync::Callback*>(this));
            if (err != NO_ERROR) {
                ALOGE("error registering vsync callback: %s (%d)",
                        strerror(-err), err);
            }
            ATRACE_INT("VsyncOn", 1);
        } else {
            status_t err = mDispSync->removeEventListener(
                    static_cast<DispSync::Callback*>(this));
            if (err != NO_ERROR) {
                ALOGE("error unregistering vsync callback: %s (%d)",
                        strerror(-err), err);
            }
            ATRACE_INT("VsyncOn", 0);
        }
    }

    virtual void setCallback(const sp<VSyncSource::Callback>& callback) {
        Mutex::Autolock lock(mMutex);
        mCallback = callback;
    }

private:
    virtual void onDispSyncEvent(nsecs_t when) {
        sp<VSyncSource::Callback> callback;
        {
            Mutex::Autolock lock(mMutex);
            callback = mCallback;

            if (mTraceVsync) {
                mValue = (mValue + 1) % 2;
                ATRACE_INT("VSYNC", mValue);
            }
        }

        if (callback != NULL) {
            callback->onVSyncEvent(when);
        }
    }

    int mValue;

    const nsecs_t mPhaseOffset;
    const bool mTraceVsync;

    DispSync* mDispSync;
    sp<VSyncSource::Callback> mCallback;
    Mutex mMutex;
};

void SurfaceFlinger::init() {
    ALOGI(  "SurfaceFlinger's main thread ready to run. "
            "Initializing graphics H/W...");

    status_t err;
    Mutex::Autolock _l(mStateLock);

    // initialize EGL for the default display
    mEGLDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(mEGLDisplay, NULL, NULL);

    // Initialize the H/W composer object.  There may or may not be an
    // actual hardware composer underneath.
    mHwc = new HWComposer(this,
            *static_cast<HWComposer::EventHandler *>(this));

    // First try to get an ES2 config
    err = selectEGLConfig(mEGLDisplay, mHwc->getVisualID(), EGL_OPENGL_ES2_BIT,
            &mEGLConfig);

    if (err != NO_ERROR) {
        // If ES2 fails, try ES1
        err = selectEGLConfig(mEGLDisplay, mHwc->getVisualID(),
                EGL_OPENGL_ES_BIT, &mEGLConfig);
    }

    if (err != NO_ERROR) {
        // still didn't work, probably because we're on the emulator...
        // try a simplified query
        ALOGW("no suitable EGLConfig found, trying a simpler query");
        err = selectEGLConfig(mEGLDisplay, mHwc->getVisualID(), 0, &mEGLConfig);
    }

    if (err != NO_ERROR) {
        // this EGL is too lame for android
        LOG_ALWAYS_FATAL("no suitable EGLConfig found, giving up");
    }

    // print some debugging info
    EGLint r,g,b,a;
    eglGetConfigAttrib(mEGLDisplay, mEGLConfig, EGL_RED_SIZE,   &r);
    eglGetConfigAttrib(mEGLDisplay, mEGLConfig, EGL_GREEN_SIZE, &g);
    eglGetConfigAttrib(mEGLDisplay, mEGLConfig, EGL_BLUE_SIZE,  &b);
    eglGetConfigAttrib(mEGLDisplay, mEGLConfig, EGL_ALPHA_SIZE, &a);
    ALOGI("EGL informations:");
    ALOGI("vendor    : %s", eglQueryString(mEGLDisplay, EGL_VENDOR));
    ALOGI("version   : %s", eglQueryString(mEGLDisplay, EGL_VERSION));
    ALOGI("extensions: %s", eglQueryString(mEGLDisplay, EGL_EXTENSIONS));
    ALOGI("Client API: %s", eglQueryString(mEGLDisplay, EGL_CLIENT_APIS)?:"Not Supported");
    ALOGI("EGLSurface: %d-%d-%d-%d, config=%p", r, g, b, a, mEGLConfig);

    // get a RenderEngine for the given display / config (can't fail)
    mRenderEngine = RenderEngine::create(mEGLDisplay, mEGLConfig);

    // retrieve the EGL context that was selected/created
    mEGLContext = mRenderEngine->getEGLContext();

    // figure out which format we got
    eglGetConfigAttrib(mEGLDisplay, mEGLConfig,
            EGL_NATIVE_VISUAL_ID, &mEGLNativeVisualId);

    LOG_ALWAYS_FATAL_IF(mEGLContext == EGL_NO_CONTEXT,
            "couldn't create EGLContext");

    // MStar Android Patch Begin
    char property[PROPERTY_VALUE_MAX] = {0};
    property_get("mstar.4k2k.2k1k.coexist", property, "false");
    if (strcmp(property, "true") == 0) {
        m4k2kAnd2k1kCoexistEnable = true;
    } else {
        m4k2kAnd2k1kCoexistEnable = false;
    }
    // MStar Android Patch End

    // initialize our non-virtual displays
    for (size_t i=0 ; i<DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES ; i++) {
        DisplayDevice::DisplayType type((DisplayDevice::DisplayType)i);
        // set-up the displays that are already connected
        if (mHwc->isConnected(i) || type==DisplayDevice::DISPLAY_PRIMARY) {
            // All non-virtual displays are currently considered secure.
            bool isSecure = true;
            createBuiltinDisplayLocked(type);
            wp<IBinder> token = mBuiltinDisplays[i];

            sp<BufferQueue> bq = new BufferQueue(new GraphicBufferAlloc());
            sp<FramebufferSurface> fbs = new FramebufferSurface(*mHwc, i, bq);
            sp<DisplayDevice> hw = new DisplayDevice(this,
                    type, allocateHwcDisplayId(type), isSecure, token,
                    fbs, bq,
                    mEGLConfig);
            if (i > DisplayDevice::DISPLAY_PRIMARY) {
                // FIXME: currently we don't get blank/unblank requests
                // for displays other than the main display, so we always
                // assume a connected display is unblanked.
                ALOGD("marking display %d as acquired/unblanked", i);
                hw->acquireScreen();
            }
            mDisplays.add(token, hw);
        }
    }

    // make the GLContext current so that we can create textures when creating Layers
    // (which may happens before we render something)
    getDefaultDisplayDevice()->makeCurrent(mEGLDisplay, mEGLContext);

    // start the EventThread
    sp<VSyncSource> vsyncSrc = new DispSyncSource(&mPrimaryDispSync,
            vsyncPhaseOffsetNs, true);
    mEventThread = new EventThread(vsyncSrc);
    sp<VSyncSource> sfVsyncSrc = new DispSyncSource(&mPrimaryDispSync,
            sfVsyncPhaseOffsetNs, false);
    mSFEventThread = new EventThread(sfVsyncSrc);
    mEventQueue.setEventThread(mSFEventThread);

    // MStar Android Patch Begin
#ifndef ENABLE_HWCOMPOSER_13
    mVsyncMonitor.Init(this, mSFEventThread);
#endif
    // MStar Android Patch End

    mEventControlThread = new EventControlThread(this);
    mEventControlThread->run("EventControl", PRIORITY_URGENT_DISPLAY);

    // set a fake vsync period if there is no HWComposer
    if (mHwc->initCheck() != NO_ERROR) {
        mPrimaryDispSync.setPeriod(16666667);
    }

    // initialize our drawing state
    mDrawingState = mCurrentState;

    // set initial conditions (e.g. unblank default device)
    initializeDisplays();

    // start boot animation
    startBootAnim();
    // MStar Android Patch Begin
#ifdef ENABLE_HWCURSOR
    unsigned int sharedBufferSize = HWCURSOR_WIDTH * HWCURSOR_HEIGHT * 4;
    sp<MemoryHeapBase> heap = new MemoryHeapBase(sharedBufferSize,
                            0, HWCURSOR_ICON_BUFFER);
    if (heap != NULL) {
        mHwcursorIcon = new MemoryBase(heap, 0, sharedBufferSize);
        void* data = mHwcursorIcon->pointer();
        if (data !=NULL) {
            memset(data, 0, sharedBufferSize);
        }
    }
#endif

#ifdef ENABLE_STR
    mStrBootAnimExit = false;
    mStandbyRecording = false;
#endif

#ifdef ENABLE_HWCOMPOSER_13
    if (property_get("mstar.reproducerate",property,NULL) > 0) {
        int value = atoi(property);
        mTopOverscan    = (value>>24)&0xff;
        mBottomOverscan = (value>>16)&0xff;
        mLeftOverscan   = (value>>8)&0xff;
        mRightOverscan  = (value)&0xff;
     }
    property_set("mstar.surfaceflinger.running", "true");
#endif
    // MStar Android Patch End
}

int32_t SurfaceFlinger::allocateHwcDisplayId(DisplayDevice::DisplayType type) {
    return (uint32_t(type) < DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES) ?
            type : mHwc->allocateDisplayId();
}

void SurfaceFlinger::startBootAnim() {
    // start boot animation
    // MStar Android Patch Begin
    struct timespec t = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &t);
    ALOGI("[AT][AN][start anim][%u]\n", (long(ns2ms(nsecs_t(t.tv_sec)*1000000000LL + t.tv_nsec))) );
    // MStar Android Patch End
    property_set("service.bootanim.exit", "0");
    property_set("ctl.start", "bootanim");
}

size_t SurfaceFlinger::getMaxTextureSize() const {
    return mRenderEngine->getMaxTextureSize();
}

size_t SurfaceFlinger::getMaxViewportDims() const {
    return mRenderEngine->getMaxViewportDims();
}

// ----------------------------------------------------------------------------

bool SurfaceFlinger::authenticateSurfaceTexture(
        const sp<IGraphicBufferProducer>& bufferProducer) const {
    Mutex::Autolock _l(mStateLock);
    sp<IBinder> surfaceTextureBinder(bufferProducer->asBinder());
    return mGraphicBufferProducerList.indexOf(surfaceTextureBinder) >= 0;
}

status_t SurfaceFlinger::getDisplayInfo(const sp<IBinder>& display, DisplayInfo* info) {
    int32_t type = NAME_NOT_FOUND;
    for (int i=0 ; i<DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES ; i++) {
        if (display == mBuiltinDisplays[i]) {
            type = i;
            break;
        }
    }

    if (type < 0) {
        return type;
    }

    const HWComposer& hwc(getHwComposer());
    float xdpi = hwc.getDpiX(type);
    float ydpi = hwc.getDpiY(type);

    // TODO: Not sure if display density should handled by SF any longer
    class Density {
        static int getDensityFromProperty(char const* propName) {
            char property[PROPERTY_VALUE_MAX];
            int density = 0;
            if (property_get(propName, property, NULL) > 0) {
                density = atoi(property);
            }
            return density;
        }
    public:
        static int getEmuDensity() {
            return getDensityFromProperty("qemu.sf.lcd_density"); }
        static int getBuildDensity()  {
            return getDensityFromProperty("ro.sf.lcd_density"); }
    };

    if (type == DisplayDevice::DISPLAY_PRIMARY) {
        // The density of the device is provided by a build property
        float density = Density::getBuildDensity() / 160.0f;
        if (density == 0) {
            // the build doesn't provide a density -- this is wrong!
            // use xdpi instead
            ALOGE("ro.sf.lcd_density must be defined as a build property");
            density = xdpi / 160.0f;
        }
        if (Density::getEmuDensity()) {
            // if "qemu.sf.lcd_density" is specified, it overrides everything
            xdpi = ydpi = density = Density::getEmuDensity();
            density /= 160.0f;
        }
        info->density = density;

        // TODO: this needs to go away (currently needed only by webkit)
        sp<const DisplayDevice> hw(getDefaultDisplayDevice());
        info->orientation = hw->getOrientation();
    } else {
        // TODO: where should this value come from?
        static const int TV_DENSITY = 213;
        info->density = TV_DENSITY / 160.0f;
        info->orientation = 0;
    }

    // MStar Android Patch Begin
    if (type == DisplayDevice::DISPLAY_PRIMARY && m4k2kAnd2k1kCoexistEnable) {
        info->w = OSD_2K1K_WIDTH;
        info->h = OSD_2K1K_HEIGHT;
    } else {
        info->w = hwc.getOsdWidth(type);
        info->h = hwc.getOsdHeight(type);
    }
    getDisplayDevice(mBuiltinDisplays[type])->setAndroidWidthAndHeight(info->w,info->h);
    // MStar Android Patch End
    info->xdpi = xdpi;
    info->ydpi = ydpi;
    info->fps = float(1e9 / hwc.getRefreshPeriod(type));

    // All non-virtual displays are currently considered secure.
    info->secure = true;

    return NO_ERROR;
}

// ----------------------------------------------------------------------------

sp<IDisplayEventConnection> SurfaceFlinger::createDisplayEventConnection() {
    return mEventThread->createEventConnection();
}

// ----------------------------------------------------------------------------

void SurfaceFlinger::waitForEvent() {
    mEventQueue.waitMessage();
}

void SurfaceFlinger::signalTransaction() {
    mEventQueue.invalidate();
}

void SurfaceFlinger::signalLayerUpdate() {
    mEventQueue.invalidate();
}

void SurfaceFlinger::signalRefresh() {
    mEventQueue.refresh();
}

// MStar Android Patch Begin
#ifdef ENABLE_STR
void SurfaceFlinger::signalSuspend() {
    mEventQueue.suspend();
}
#endif
// MStar Android Patch End

status_t SurfaceFlinger::postMessageAsync(const sp<MessageBase>& msg,
        nsecs_t reltime, uint32_t flags) {
    return mEventQueue.postMessage(msg, reltime);
}

status_t SurfaceFlinger::postMessageSync(const sp<MessageBase>& msg,
        nsecs_t reltime, uint32_t flags) {
    status_t res = mEventQueue.postMessage(msg, reltime);
    if (res == NO_ERROR) {
        msg->wait();
    }
    return res;
}

void SurfaceFlinger::run() {
    do {
        waitForEvent();
    } while (true);
}

void SurfaceFlinger::enableHardwareVsync() {
    Mutex::Autolock _l(mHWVsyncLock);
    if (!mPrimaryHWVsyncEnabled && mHWVsyncAvailable) {
        mPrimaryDispSync.beginResync();
        //eventControl(HWC_DISPLAY_PRIMARY, SurfaceFlinger::EVENT_VSYNC, true);
        mEventControlThread->setVsyncEnabled(true);
        mPrimaryHWVsyncEnabled = true;
    }
}

void SurfaceFlinger::resyncToHardwareVsync(bool makeAvailable) {
    Mutex::Autolock _l(mHWVsyncLock);

    if (makeAvailable) {
        mHWVsyncAvailable = true;
    } else if (!mHWVsyncAvailable) {
        ALOGE("resyncToHardwareVsync called when HW vsync unavailable");
        return;
    }

    const nsecs_t period =
            getHwComposer().getRefreshPeriod(HWC_DISPLAY_PRIMARY);

    mPrimaryDispSync.reset();
    mPrimaryDispSync.setPeriod(period);

    if (!mPrimaryHWVsyncEnabled) {
        mPrimaryDispSync.beginResync();
        //eventControl(HWC_DISPLAY_PRIMARY, SurfaceFlinger::EVENT_VSYNC, true);
        mEventControlThread->setVsyncEnabled(true);
        mPrimaryHWVsyncEnabled = true;
    }
}

void SurfaceFlinger::disableHardwareVsync(bool makeUnavailable) {
    Mutex::Autolock _l(mHWVsyncLock);
    if (mPrimaryHWVsyncEnabled) {
        //eventControl(HWC_DISPLAY_PRIMARY, SurfaceFlinger::EVENT_VSYNC, false);
        mEventControlThread->setVsyncEnabled(false);
        mPrimaryDispSync.endResync();
        mPrimaryHWVsyncEnabled = false;
    }
    if (makeUnavailable) {
        mHWVsyncAvailable = false;
    }
}

void SurfaceFlinger::onVSyncReceived(int type, nsecs_t timestamp) {
    bool needsHwVsync = false;

    { // Scope for the lock
        Mutex::Autolock _l(mHWVsyncLock);
        if (type == 0 && mPrimaryHWVsyncEnabled) {
            needsHwVsync = mPrimaryDispSync.addResyncSample(timestamp);
        }
    }

    if (needsHwVsync) {
        enableHardwareVsync();
    } else {
        disableHardwareVsync(false);
    }
}

void SurfaceFlinger::onHotplugReceived(int type, bool connected) {
    if (mEventThread == NULL) {
        // This is a temporary workaround for b/7145521.  A non-null pointer
        // does not mean EventThread has finished initializing, so this
        // is not a correct fix.
        ALOGW("WARNING: EventThread not started, ignoring hotplug");
        return;
    }

    if (uint32_t(type) < DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES) {
        Mutex::Autolock _l(mStateLock);
        if (connected) {
            createBuiltinDisplayLocked((DisplayDevice::DisplayType)type);
        } else {
            mCurrentState.displays.removeItem(mBuiltinDisplays[type]);
            mBuiltinDisplays[type].clear();
        }
        setTransactionFlags(eDisplayTransactionNeeded);

        // Defer EventThread notification until SF has updated mDisplays.
    }
}

void SurfaceFlinger::eventControl(int disp, int event, int enabled) {
    ATRACE_CALL();
    getHwComposer().eventControl(disp, event, enabled);
}

void SurfaceFlinger::onMessageReceived(int32_t what) {
    ATRACE_CALL();
    switch (what) {
    case MessageQueue::TRANSACTION:
        handleMessageTransaction();
        break;
    case MessageQueue::INVALIDATE:
        handleMessageTransaction();
        handleMessageInvalidate();
        signalRefresh();
        break;
    case MessageQueue::REFRESH:
        handleMessageRefresh();
        break;
    // MStar Android Patch Begin
#ifdef ENABLE_STR
    case MessageQueue::SUSPEND:
        property_set(MSTAR_DESK_DISPLAY_MODE, "0");
#ifndef ENABLE_HWCOMPOSER_13
        mHwc->setDisplayMode(DISPLAYMODE_NORMAL);
#endif
        char property[PROPERTY_VALUE_MAX] = {0};
        property_get("mstar.pvr.standby.recording", property, "false");

        if (strcmp("true",property)==0) {
            mStandbyRecording = true;
        } else {
            mStandbyRecording = false;
            startBootAnim();
        }
        break;
#endif
    // MStar Android Patch End
    }
}

void SurfaceFlinger::handleMessageTransaction() {
    uint32_t transactionFlags = peekTransactionFlags(eTransactionMask);
    if (transactionFlags) {
        handleTransaction(transactionFlags);
    }
}

void SurfaceFlinger::handleMessageInvalidate() {
    ATRACE_CALL();
    handlePageFlip();
}

void SurfaceFlinger::handleMessageRefresh() {
    ATRACE_CALL();
    preComposition();
    rebuildLayerStacks();
    setUpHWComposer();
    doDebugFlashRegions();
    doComposition();
    postComposition();
}

void SurfaceFlinger::doDebugFlashRegions()
{
    // is debugging enabled
    if (CC_LIKELY(!mDebugRegion))
        return;

    const bool repaintEverything = mRepaintEverything;
    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        const sp<DisplayDevice>& hw(mDisplays[dpy]);
        if (hw->canDraw()) {
            // transform the dirty region into this screen's coordinate space
            const Region dirtyRegion(hw->getDirtyRegion(repaintEverything));
            if (!dirtyRegion.isEmpty()) {
                // redraw the whole screen
                doComposeSurfaces(hw, Region(hw->bounds()));

                // and draw the dirty region
                const int32_t height = hw->getHeight();
                RenderEngine& engine(getRenderEngine());
                engine.fillRegionWithColor(dirtyRegion, height, 1, 0, 1, 1);

                hw->compositionComplete();
                hw->swapBuffers(getHwComposer());
            }
        }
    }

    postFramebuffer();

    if (mDebugRegion > 1) {
        usleep(mDebugRegion * 1000);
    }

    HWComposer& hwc(getHwComposer());
    if (hwc.initCheck() == NO_ERROR) {
        status_t err = hwc.prepare();
        ALOGE_IF(err, "HWComposer::prepare failed (%s)", strerror(-err));
    }
}

void SurfaceFlinger::preComposition()
{
    bool needExtraInvalidate = false;
    const LayerVector& layers(mDrawingState.layersSortedByZ);
    const size_t count = layers.size();
    for (size_t i=0 ; i<count ; i++) {
        if (layers[i]->onPreComposition()) {
            needExtraInvalidate = true;
        }
    }
    if (needExtraInvalidate) {
        signalLayerUpdate();
    }
}

void SurfaceFlinger::postComposition()
{
    const LayerVector& layers(mDrawingState.layersSortedByZ);
    const size_t count = layers.size();
    for (size_t i=0 ; i<count ; i++) {
        layers[i]->onPostComposition();
    }

    const HWComposer& hwc = getHwComposer();
    sp<Fence> presentFence = hwc.getDisplayFence(HWC_DISPLAY_PRIMARY);

    if (presentFence->isValid()) {
        if (mPrimaryDispSync.addPresentFence(presentFence)) {
            enableHardwareVsync();
        } else {
            disableHardwareVsync(false);
        }
    }

    if (runningWithoutSyncFramework) {
        const sp<const DisplayDevice> hw(getDefaultDisplayDevice());
        if (hw->isScreenAcquired()) {
            enableHardwareVsync();
        }
    }

    if (mAnimCompositionPending) {
        mAnimCompositionPending = false;

        if (presentFence->isValid()) {
            mAnimFrameTracker.setActualPresentFence(presentFence);
        } else {
            // The HWC doesn't support present fences, so use the refresh
            // timestamp instead.
            nsecs_t presentTime = hwc.getRefreshTimestamp(HWC_DISPLAY_PRIMARY);
            mAnimFrameTracker.setActualPresentTime(presentTime);
        }
        mAnimFrameTracker.advanceFrame();
    }
}

void SurfaceFlinger::rebuildLayerStacks() {
    // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
    wp<IBinder> token = mBuiltinDisplays[DisplayDevice::DISPLAY_PRIMARY];
    const sp<DisplayDevice>& hw(mDisplays.valueFor(token));
    bool overscanChanged;
    {
        Mutex::Autolock _l(mOverscanLock);
        overscanChanged = hw->setOverscan(mLeftOverscan, mTopOverscan, mRightOverscan, mBottomOverscan);
        //ALOGI("Overscan main thread: left:%d top:%d right:%d bottom:%d",mLeftOverscan, mTopOverscan, mRightOverscan, mBottomOverscan);
    }

    if (overscanChanged) {
#ifdef ENABLE_HWCURSOR
        mHwc->hwCursorsetOverscan(mLeftOverscan,mTopOverscan,mRightOverscan,mBottomOverscan);
#endif
        mVisibleRegionsDirty = true;
    }
#endif
    // MStar Android Patch End

    // rebuild the visible layer list per screen
    if (CC_UNLIKELY(mVisibleRegionsDirty)) {
        ATRACE_CALL();
        mVisibleRegionsDirty = false;
        invalidateHwcGeometry();

        const LayerVector& layers(mDrawingState.layersSortedByZ);
        // MStar Android Patch Begin
        //Modify for xts test case.Virtual display device's layerstack is equals primary display device.
        //So layer will draw on all the display device.
        //The flow of SurfaceFlinger will not compute dirty region if the layer has computed.so we get
        //dirty region from cache.
        KeyedVector<uint32_t, Region> dirtyRegionCache;
        // MStar Android Patch End
        for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
            Region opaqueRegion;
            Region dirtyRegion;
            Vector< sp<Layer> > layersSortedByZ;
            const sp<DisplayDevice>& hw(mDisplays[dpy]);
            const Transform& tr(hw->getTransform());
            const Rect bounds(hw->getBounds());
            if (hw->canDraw()) {
                SurfaceFlinger::computeVisibleRegions(layers,
                        hw->getLayerStack(), dirtyRegion, opaqueRegion);

                // MStar Android Patch Begin
                const ssize_t j = dirtyRegionCache.indexOfKey(hw->getLayerStack());
                if (j >= 0) {
                    dirtyRegion.orSelf(dirtyRegionCache.valueFor(hw->getLayerStack()));
                } else {
                    dirtyRegionCache.add(hw->getLayerStack(),dirtyRegion);
                }
                // MStar Android Patch End

                const size_t count = layers.size();
                for (size_t i=0 ; i<count ; i++) {
                    const sp<Layer>& layer(layers[i]);
                    const Layer::State& s(layer->getDrawingState());
                    if (s.layerStack == hw->getLayerStack()) {
                        Region drawRegion(tr.transform(
                                layer->visibleNonTransparentRegion));
                        drawRegion.andSelf(bounds);
                        if (!drawRegion.isEmpty()) {
                            layersSortedByZ.add(layer);
                        }
                    }
                }
            }
            // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
            Transform *TFirst=NULL,*TSecond=NULL;
            Rect rect(hw->getFrame());
            mHwc->recalc3DTransform(&TFirst, &TSecond, rect);
            if (TFirst != NULL || TSecond != NULL) {
                Region region;
                if (TFirst != NULL) {
                    region = region.merge((*TFirst).transform(opaqueRegion));
                    delete TFirst;
                }
                if (TSecond != NULL) {
                    region = region.merge((*TSecond).transform(opaqueRegion));
                    delete TSecond;
                }
                opaqueRegion = region;
            }

#else
            char property[PROPERTY_VALUE_MAX] = {0};
            int displayMode = DISPLAYMODE_NORMAL;
            memset(property,0,sizeof(property));
            if (property_get(MSTAR_DESK_DISPLAY_MODE, property, NULL) > 0) {
                displayMode = atoi(property);
            }
            if (displayMode < 0 || displayMode >= DISPLAYMODE_MAX) {
                displayMode = DISPLAYMODE_NORMAL;
                ALOGE("The property mstar.desk-display-mode is incorrect that it will reset to default value");
            }
            Transform TR;
            switch (displayMode) {
                case DISPLAYMODE_TOP_LA:
                case DISPLAYMODE_TOP_ONLY: {
                    TR.set(1.0f,0,0,0.5f);
                    break;
                }
                case DISPLAYMODE_BOTTOM_LA:
                case DISPLAYMODE_BOTTOM_ONLY: {
                    TR.set(0,bounds.getHeight()>>1);
                    TR.set(1.0f,0,0,0.5f);
                    break;
                }
                case DISPLAYMODE_LEFT_ONLY: {
                    TR.set(0.5f,0,0,1.0f);
                    break;
                }
                case DISPLAYMODE_RIGHT_ONLY: {
                    TR.set(bounds.getHeight()>>1,0);
                    TR.set(0.5f,0,0,1.0f);
                    break;
                }
                default:
                    break;
            }
            opaqueRegion = TR.transform(opaqueRegion);
#endif
            // MStar Android Patch End
            hw->setVisibleLayersSortedByZ(layersSortedByZ);
            hw->undefinedRegion.set(bounds);
            hw->undefinedRegion.subtractSelf(tr.transform(opaqueRegion));
            hw->dirtyRegion.orSelf(dirtyRegion);
        }
    }
}

void SurfaceFlinger::setUpHWComposer() {
    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        mDisplays[dpy]->beginFrame();
    }

    HWComposer& hwc(getHwComposer());
    if (hwc.initCheck() == NO_ERROR) {
        // build the h/w work list
        if (CC_UNLIKELY(mHwWorkListDirty)) {
            mHwWorkListDirty = false;
            for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
                // MStar Android Patch Begin
                sp<DisplayDevice> hw(mDisplays[dpy]);
                // MStar Android Patch End
                const int32_t id = hw->getHwcDisplayId();
                if (id >= 0) {
                    const Vector< sp<Layer> >& currentLayers(
                        hw->getVisibleLayersSortedByZ());
                    const size_t count = currentLayers.size();
                    if (hwc.createWorkList(id, count) == NO_ERROR) {
                        HWComposer::LayerListIterator cur = hwc.begin(id);
                        const HWComposer::LayerListIterator end = hwc.end(id);
                        int Width = 1920;
                        int Height = 1080;
                        // MStar Android Patch Begin
                        if (m4k2kAnd2k1kCoexistEnable &&
                            (hw->getDisplayType() == DisplayDevice::DISPLAY_PRIMARY)) {
                            bool has4k2kLayer = false;
                            bool bChangeTo4k2kTiming = false;
                            int opTimingWidth = 1920;
                            int opTimingHeight = 1080;
                            int ursaVer = 0;
                            bool hasOverlay = false;
                            if (mHwc) {
                                mHwc->getCurOPTiming(&opTimingWidth, &opTimingHeight);
                                ursaVer = mHwc->getUrsaVsersion();
                            }
                            for (size_t i=0 ; i<count ; ++i) {
                                const sp<Layer>& layer(currentLayers[i]);
                                Rect rect = layer->getContentCrop();
                                if (layer->getActiveBuffer() != NULL) {
                                    rect = layer->getActiveBuffer()->getBounds();
                                }

                                if ((rect.getWidth()== OSD_4K2K_WIDTH && rect.getHeight()== OSD_4K2K_HEIGHT)
                                    && (strcmp(layer->getName().string(),"com.android.systemui.ImageWallpaper") != 0)) {
                                    bChangeTo4k2kTiming = true;
                                }

                                if ((rect.getWidth()== OSD_4K2K_WIDTH && rect.getHeight()== OSD_4K2K_HEIGHT)
                                    &&(opTimingWidth ==  OSD_4K2K_WIDTH && opTimingHeight == OSD_4K2K_HEIGHT)
                                    && (strcmp(layer->getName().string(),"com.android.systemui.ImageWallpaper") != 0)
                                    && (strcmp(layer->getName().string(),"ScreenshotSurface") != 0)) {
                                    has4k2kLayer = true;
                                }
                                if (layer->isOverlay()) {
                                    hasOverlay = true;
                                }
                            }
#ifndef BUILD_MSTARTV_MI
                            mHwc->require4k2kOutputTiming(bChangeTo4k2kTiming? 1:0);
#endif
                            hw->changeGlobalTransform(has4k2kLayer);
                            mChangeTo2k1kMode = !has4k2kLayer;
                            hwc.updateFramebufferTargetFrameSize(id);
                            if (hwc.isStbTarget() && (!has4k2kLayer)) {
                                bool resolutionChanged = false;
                                hw->getStbResolution(&Width, &Height, &resolutionChanged);
                                if (resolutionChanged) {
#ifdef ENABLE_HWCURSOR
                                    changeHwCursorResolution();
#endif
                                }
                                Rect  frame = Rect(Width,Height);
                                hw->changeGlobalTransform(frame);
                                hwc.updateFramebufferTargetFrameSize(id, frame);
                            }
                        } else if (hwc.isStbTarget()) {
                            Rect frame;
                            bool resolutionChanged = false;
                            hw->getStbResolution(&Width, &Height, &resolutionChanged);
                            if (resolutionChanged) {
#ifdef ENABLE_HWCURSOR
                                changeHwCursorResolution();
#endif
                            }
                            if ((Width > hw->getWidth())||(Height > hw->getHeight())) {
                                 frame = Rect(hw->getWidth(),hw->getHeight());
                            } else {
                                 frame = Rect(Width,Height);
                            }
                            hw->changeGlobalTransform(frame);
                            hwc.updateFramebufferTargetFrameSize(id, frame);
                        } else if (hw->getDisplayType() == DisplayDevice::DISPLAY_PRIMARY){
                            const DisplayDeviceState& state(mCurrentState.displays.valueFor(getBuiltInDisplay(DisplayDevice::DISPLAY_PRIMARY)));
                            Rect frame(state.frame);
                            hw->changeGlobalTransform(frame);
                        }
                        if (hwc.isOsdNeedResize()) {
                            char property[PROPERTY_VALUE_MAX];
                            if (property_get("mstar.change.osd.size", property, "NULL") > 0) {
                                char* p = strstr(property, "x");
                                if (p != NULL) {
                                    char * substring = (char *) malloc(p - property + 1);
                                    strncpy(substring,property,p - property);
                                    int width = atoi(substring);
                                    int height = atoi(p + 1);
                                    Rect  frame = Rect(width,height);
                                    hw->changeGlobalTransform(frame);
                                    hwc.updateFramebufferTargetFrameSize(id, frame);
                                    free(substring);
                                }
                            }
                        }
                        // MStar Android Patch End
                        for (size_t i=0 ; cur!=end && i<count ; ++i, ++cur) {
                            const sp<Layer>& layer(currentLayers[i]);
                            layer->setGeometry(hw, *cur);
                            if (mDebugDisableHWC || mDebugRegion || mDaltonize) {
                                cur->setSkip(true);
                            }
                        }
                    }
                }
            }
        }

        // set the per-frame data
        for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
            sp<const DisplayDevice> hw(mDisplays[dpy]);
            const int32_t id = hw->getHwcDisplayId();
            if (id >= 0) {
                const Vector< sp<Layer> >& currentLayers(
                    hw->getVisibleLayersSortedByZ());
                const size_t count = currentLayers.size();
                HWComposer::LayerListIterator cur = hwc.begin(id);
                const HWComposer::LayerListIterator end = hwc.end(id);
                for (size_t i=0 ; cur!=end && i<count ; ++i, ++cur) {
                    /*
                     * update the per-frame h/w composer data for each layer
                     * and build the transparent region of the FB
                     */
                    const sp<Layer>& layer(currentLayers[i]);
                    layer->setPerFrameData(hw, *cur);
                }
            }
        }

        status_t err = hwc.prepare();
        ALOGE_IF(err, "HWComposer::prepare failed (%s)", strerror(-err));

        // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
        for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
            sp<const DisplayDevice> hw(mDisplays[dpy]);
            const int32_t id = hw->getHwcDisplayId();
            if (id >= 0) {
                const Vector< sp<Layer> >& currentLayers(
                    hw->getVisibleLayersSortedByZ());
                const size_t count = currentLayers.size();
                HWComposer::LayerListIterator cur = hwc.begin(id);
                const HWComposer::LayerListIterator end = hwc.end(id);
                for (size_t i=0 ; cur!=end && i<count ; ++i, ++cur) {
                    const sp<Layer>& layer(currentLayers[i]);
                    layer->UpdateConsumerUsageBits( *cur);
                }
            }
        }
#endif
        // MStar Android Patch End

        for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
            sp<const DisplayDevice> hw(mDisplays[dpy]);
            hw->prepareFrame(hwc);
        }
    }
}

void SurfaceFlinger::doComposition() {
    ATRACE_CALL();
    const bool repaintEverything = android_atomic_and(0, &mRepaintEverything);
    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        const sp<DisplayDevice>& hw(mDisplays[dpy]);
        if (hw->canDraw()) {
            // transform the dirty region into this screen's coordinate space
            const Region dirtyRegion(hw->getDirtyRegion(repaintEverything));

            // MStar Android Patch Begin
            hw->skipSwapBuffer = false;
            if (doDisplayComposition(hw, dirtyRegion) == false && hw->getDisplayType() < DisplayDevice::DISPLAY_EXTERNAL) {
                //return directly if primary display device's dirty
                //is empty,it will cause: SurfaceFlinger don't
                //compose other display device.so modify to continue.
                hw->skipSwapBuffer = true;
                continue;
            }
            // MStar Android Patch End

            hw->dirtyRegion.clear();
            hw->flip(hw->swapRegion);
            hw->swapRegion.clear();
        }
        // inform the h/w that we're done compositing
        hw->compositionComplete();
    }
    postFramebuffer();
}

void SurfaceFlinger::postFramebuffer()
{
    ATRACE_CALL();

    const nsecs_t now = systemTime();
    mDebugInSwapBuffers = now;

    HWComposer& hwc(getHwComposer());
    if (hwc.initCheck() == NO_ERROR) {
        if (!hwc.supportsFramebufferTarget()) {
            // EGL spec says:
            //   "surface must be bound to the calling thread's current context,
            //    for the current rendering API."
            getDefaultDisplayDevice()->makeCurrent(mEGLDisplay, mEGLContext);
        }
        hwc.commit();
    }

    // make the default display current because the VirtualDisplayDevice code cannot
    // deal with dequeueBuffer() being called outside of the composition loop; however
    // the code below can call glFlush() which is allowed (and does in some case) call
    // dequeueBuffer().
    getDefaultDisplayDevice()->makeCurrent(mEGLDisplay, mEGLContext);

    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        sp<const DisplayDevice> hw(mDisplays[dpy]);
        const Vector< sp<Layer> >& currentLayers(hw->getVisibleLayersSortedByZ());
        hw->onSwapBuffersCompleted(hwc);
        const size_t count = currentLayers.size();
        int32_t id = hw->getHwcDisplayId();
        if (id >=0 && hwc.initCheck() == NO_ERROR) {
            HWComposer::LayerListIterator cur = hwc.begin(id);
            const HWComposer::LayerListIterator end = hwc.end(id);
            for (size_t i = 0; cur != end && i < count; ++i, ++cur) {
                currentLayers[i]->onLayerDisplayed(hw, &*cur);
            }
        } else {
            for (size_t i = 0; i < count; i++) {
                currentLayers[i]->onLayerDisplayed(hw, NULL);
            }
        }
    }

    mLastSwapBufferTime = systemTime() - now;
    mDebugInSwapBuffers = 0;

    uint32_t flipCount = getDefaultDisplayDevice()->getPageFlipCount();
    if (flipCount % LOG_FRAME_STATS_PERIOD == 0) {
        logFrameStats();
    }
}

void SurfaceFlinger::handleTransaction(uint32_t transactionFlags)
{
    ATRACE_CALL();

    // here we keep a copy of the drawing state (that is the state that's
    // going to be overwritten by handleTransactionLocked()) outside of
    // mStateLock so that the side-effects of the State assignment
    // don't happen with mStateLock held (which can cause deadlocks).
    State drawingState(mDrawingState);

    Mutex::Autolock _l(mStateLock);
    const nsecs_t now = systemTime();
    mDebugInTransaction = now;

    // Here we're guaranteed that some transaction flags are set
    // so we can call handleTransactionLocked() unconditionally.
    // We call getTransactionFlags(), which will also clear the flags,
    // with mStateLock held to guarantee that mCurrentState won't change
    // until the transaction is committed.

    transactionFlags = getTransactionFlags(eTransactionMask);
    handleTransactionLocked(transactionFlags);

    mLastTransactionTime = systemTime() - now;
    mDebugInTransaction = 0;
    invalidateHwcGeometry();
    // here the transaction has been committed
}

void SurfaceFlinger::handleTransactionLocked(uint32_t transactionFlags)
{
    const LayerVector& currentLayers(mCurrentState.layersSortedByZ);
    const size_t count = currentLayers.size();

    /*
     * Traversal of the children
     * (perform the transaction for each of them if needed)
     */

    if (transactionFlags & eTraversalNeeded) {
        for (size_t i=0 ; i<count ; i++) {
            const sp<Layer>& layer(currentLayers[i]);
            uint32_t trFlags = layer->getTransactionFlags(eTransactionNeeded);
            if (!trFlags) continue;

            const uint32_t flags = layer->doTransaction(0);
            if (flags & Layer::eVisibleRegion)
                mVisibleRegionsDirty = true;
        }
    }

    /*
     * Perform display own transactions if needed
     */

    if (transactionFlags & eDisplayTransactionNeeded) {
        // here we take advantage of Vector's copy-on-write semantics to
        // improve performance by skipping the transaction entirely when
        // know that the lists are identical
        const KeyedVector<  wp<IBinder>, DisplayDeviceState>& curr(mCurrentState.displays);
        const KeyedVector<  wp<IBinder>, DisplayDeviceState>& draw(mDrawingState.displays);
        if (!curr.isIdenticalTo(draw)) {
            mVisibleRegionsDirty = true;
            const size_t cc = curr.size();
                  size_t dc = draw.size();

            // find the displays that were removed
            // (ie: in drawing state but not in current state)
            // also handle displays that changed
            // (ie: displays that are in both lists)
            for (size_t i=0 ; i<dc ; i++) {
                const ssize_t j = curr.indexOfKey(draw.keyAt(i));
                if (j < 0) {
                    // in drawing state but not in current state
                    if (!draw[i].isMainDisplay()) {
                        // Call makeCurrent() on the primary display so we can
                        // be sure that nothing associated with this display
                        // is current.
                        const sp<const DisplayDevice> defaultDisplay(getDefaultDisplayDevice());
                        defaultDisplay->makeCurrent(mEGLDisplay, mEGLContext);
                        sp<DisplayDevice> hw(getDisplayDevice(draw.keyAt(i)));
                        if (hw != NULL)
                            hw->disconnect(getHwComposer());
                        if (draw[i].type < DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES)
                            mEventThread->onHotplugReceived(draw[i].type, false);
                        mDisplays.removeItem(draw.keyAt(i));
                    } else {
                        ALOGW("trying to remove the main display");
                    }
                } else {
                    // this display is in both lists. see if something changed.
                    const DisplayDeviceState& state(curr[j]);
                    const wp<IBinder>& display(curr.keyAt(j));
                    if (state.surface->asBinder() != draw[i].surface->asBinder()) {
                        // changing the surface is like destroying and
                        // recreating the DisplayDevice, so we just remove it
                        // from the drawing state, so that it get re-added
                        // below.
                        sp<DisplayDevice> hw(getDisplayDevice(display));
                        if (hw != NULL)
                            hw->disconnect(getHwComposer());
                        mDisplays.removeItem(display);
                        mDrawingState.displays.removeItemsAt(i);
                        dc--; i--;
                        // at this point we must loop to the next item
                        continue;
                    }

                    const sp<DisplayDevice> disp(getDisplayDevice(display));
                    if (disp != NULL) {
                        if (state.layerStack != draw[i].layerStack) {
                            disp->setLayerStack(state.layerStack);
                        }
                        if ((state.orientation != draw[i].orientation)
                                || (state.viewport != draw[i].viewport)
                                || (state.frame != draw[i].frame))
                        {
                            disp->setProjection(state.orientation,
                                    state.viewport, state.frame);
                        }
                    }
                }
            }

            // find displays that were added
            // (ie: in current state but not in drawing state)
            for (size_t i=0 ; i<cc ; i++) {
                if (draw.indexOfKey(curr.keyAt(i)) < 0) {
                    const DisplayDeviceState& state(curr[i]);

                    sp<DisplaySurface> dispSurface;
                    sp<IGraphicBufferProducer> producer;
                    sp<BufferQueue> bq = new BufferQueue(new GraphicBufferAlloc());

                    int32_t hwcDisplayId = -1;
                    if (state.isVirtualDisplay()) {
                        // Virtual displays without a surface are dormant:
                        // they have external state (layer stack, projection,
                        // etc.) but no internal state (i.e. a DisplayDevice).
                        if (state.surface != NULL) {

                            hwcDisplayId = allocateHwcDisplayId(state.type);
                            sp<VirtualDisplaySurface> vds = new VirtualDisplaySurface(
                                    *mHwc, hwcDisplayId, state.surface, bq,
                                    state.displayName);

                            dispSurface = vds;
                            if (hwcDisplayId >= 0) {
                                producer = vds;
                            } else {
                                // There won't be any interaction with HWC for this virtual display,
                                // so the GLES driver can pass buffers directly to the sink.
                                producer = state.surface;
                            }
                        }
                    } else {
                        ALOGE_IF(state.surface!=NULL,
                                "adding a supported display, but rendering "
                                "surface is provided (%p), ignoring it",
                                state.surface.get());
                        hwcDisplayId = allocateHwcDisplayId(state.type);
                        // for supported (by hwc) displays we provide our
                        // own rendering surface
                        dispSurface = new FramebufferSurface(*mHwc, state.type, bq);
                        producer = bq;
                    }

                    const wp<IBinder>& display(curr.keyAt(i));
                    if (dispSurface != NULL) {
                        sp<DisplayDevice> hw = new DisplayDevice(this,
                                state.type, hwcDisplayId, state.isSecure,
                                display, dispSurface, producer, mEGLConfig);
                        hw->setLayerStack(state.layerStack);
                        hw->setProjection(state.orientation,
                                state.viewport, state.frame);
                        hw->setDisplayName(state.displayName);
                        mDisplays.add(display, hw);
                        if (state.isVirtualDisplay()) {
                            if (hwcDisplayId >= 0) {
                                mHwc->setVirtualDisplayProperties(hwcDisplayId,
                                        hw->getWidth(), hw->getHeight(),
                                        hw->getFormat());
                            }
                        } else {
                            mEventThread->onHotplugReceived(state.type, true);
                        }
                    }
                }
            }
        }
    }

    if (transactionFlags & (eTraversalNeeded|eDisplayTransactionNeeded)) {
        // The transform hint might have changed for some layers
        // (either because a display has changed, or because a layer
        // as changed).
        //
        // Walk through all the layers in currentLayers,
        // and update their transform hint.
        //
        // If a layer is visible only on a single display, then that
        // display is used to calculate the hint, otherwise we use the
        // default display.
        //
        // NOTE: we do this here, rather than in rebuildLayerStacks() so that
        // the hint is set before we acquire a buffer from the surface texture.
        //
        // NOTE: layer transactions have taken place already, so we use their
        // drawing state. However, SurfaceFlinger's own transaction has not
        // happened yet, so we must use the current state layer list
        // (soon to become the drawing state list).
        //
        sp<const DisplayDevice> disp;
        uint32_t currentlayerStack = 0;
        for (size_t i=0; i<count; i++) {
            // NOTE: we rely on the fact that layers are sorted by
            // layerStack first (so we don't have to traverse the list
            // of displays for every layer).
            const sp<Layer>& layer(currentLayers[i]);
            uint32_t layerStack = layer->getDrawingState().layerStack;
            if (i==0 || currentlayerStack != layerStack) {
                currentlayerStack = layerStack;
                // figure out if this layerstack is mirrored
                // (more than one display) if so, pick the default display,
                // if not, pick the only display it's on.
                disp.clear();
                for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
                    sp<const DisplayDevice> hw(mDisplays[dpy]);
                    if (hw->getLayerStack() == currentlayerStack) {
                        if (disp == NULL) {
                            disp = hw;
                        } else {
                            disp = NULL;
                            break;
                        }
                    }
                }
            }
            if (disp == NULL) {
                // NOTE: TEMPORARY FIX ONLY. Real fix should cause layers to
                // redraw after transform hint changes. See bug 8508397.

                // could be null when this layer is using a layerStack
                // that is not visible on any display. Also can occur at
                // screen off/on times.
                disp = getDefaultDisplayDevice();
            }
            layer->updateTransformHint(disp);
        }
    }


    /*
     * Perform our own transaction if needed
     */

    const LayerVector& layers(mDrawingState.layersSortedByZ);
    if (currentLayers.size() > layers.size()) {
        // layers have been added
        mVisibleRegionsDirty = true;
    }

    // some layers might have been removed, so
    // we need to update the regions they're exposing.
    if (mLayersRemoved) {
        mLayersRemoved = false;
        mVisibleRegionsDirty = true;
        const size_t count = layers.size();
        for (size_t i=0 ; i<count ; i++) {
            const sp<Layer>& layer(layers[i]);
            if (currentLayers.indexOf(layer) < 0) {
                // this layer is not visible anymore
                // TODO: we could traverse the tree from front to back and
                //       compute the actual visible region
                // TODO: we could cache the transformed region
                const Layer::State& s(layer->getDrawingState());
                Region visibleReg = s.transform.transform(
                        Region(Rect(s.active.w, s.active.h)));
                invalidateLayerStack(s.layerStack, visibleReg);
            }
        }
    }

    commitTransaction();
}

void SurfaceFlinger::commitTransaction()
{
    if (!mLayersPendingRemoval.isEmpty()) {
        // Notify removed layers now that they can't be drawn from
        for (size_t i = 0; i < mLayersPendingRemoval.size(); i++) {
            mLayersPendingRemoval[i]->onRemoved();
        }
        mLayersPendingRemoval.clear();
    }

    // If this transaction is part of a window animation then the next frame
    // we composite should be considered an animation as well.
    mAnimCompositionPending = mAnimTransactionPending;

    mDrawingState = mCurrentState;
    mTransactionPending = false;
    mAnimTransactionPending = false;
    mTransactionCV.broadcast();
}

void SurfaceFlinger::computeVisibleRegions(
        const LayerVector& currentLayers, uint32_t layerStack,
        Region& outDirtyRegion, Region& outOpaqueRegion)
{
    ATRACE_CALL();

    Region aboveOpaqueLayers;
    Region aboveCoveredLayers;
    Region dirty;

    outDirtyRegion.clear();

    size_t i = currentLayers.size();
    while (i--) {
        const sp<Layer>& layer = currentLayers[i];

        // start with the whole surface at its current location
        const Layer::State& s(layer->getDrawingState());

        // only consider the layers on the given layer stack
        if (s.layerStack != layerStack)
            continue;

        /*
         * opaqueRegion: area of a surface that is fully opaque.
         */
        Region opaqueRegion;

        /*
         * visibleRegion: area of a surface that is visible on screen
         * and not fully transparent. This is essentially the layer's
         * footprint minus the opaque regions above it.
         * Areas covered by a translucent surface are considered visible.
         */
        Region visibleRegion;

        /*
         * coveredRegion: area of a surface that is covered by all
         * visible regions above it (which includes the translucent areas).
         */
        Region coveredRegion;

        /*
         * transparentRegion: area of a surface that is hinted to be completely
         * transparent. This is only used to tell when the layer has no visible
         * non-transparent regions and can be removed from the layer list. It
         * does not affect the visibleRegion of this layer or any layers
         * beneath it. The hint may not be correct if apps don't respect the
         * SurfaceView restrictions (which, sadly, some don't).
         */
        Region transparentRegion;


        // handle hidden surfaces by setting the visible region to empty
        if (CC_LIKELY(layer->isVisible())) {
            const bool translucent = !layer->isOpaque();
            Rect bounds(s.transform.transform(layer->computeBounds()));
            visibleRegion.set(bounds);
            if (!visibleRegion.isEmpty()) {
                // Remove the transparent area from the visible region
                if (translucent) {
                    const Transform tr(s.transform);
                    if (tr.transformed()) {
                        if (tr.preserveRects()) {
                            // transform the transparent region
                            transparentRegion = tr.transform(s.activeTransparentRegion);
                        } else {
                            // transformation too complex, can't do the
                            // transparent region optimization.
                            transparentRegion.clear();
                        }
                    } else {
                        transparentRegion = s.activeTransparentRegion;
                    }
                }

                // compute the opaque region
                const int32_t layerOrientation = s.transform.getOrientation();
                if (s.alpha==255 && !translucent &&
                        ((layerOrientation & Transform::ROT_INVALID) == false)) {
                    // the opaque region is the layer's footprint
                    opaqueRegion = visibleRegion;
                }
            }
        }

        // Clip the covered region to the visible region
        coveredRegion = aboveCoveredLayers.intersect(visibleRegion);

        // Update aboveCoveredLayers for next (lower) layer
        aboveCoveredLayers.orSelf(visibleRegion);

        // subtract the opaque region covered by the layers above us
        visibleRegion.subtractSelf(aboveOpaqueLayers);

        // compute this layer's dirty region
        if (layer->contentDirty) {
            // we need to invalidate the whole region
            dirty = visibleRegion;
            // as well, as the old visible region
            dirty.orSelf(layer->visibleRegion);
            layer->contentDirty = false;
        } else {
            /* compute the exposed region:
             *   the exposed region consists of two components:
             *   1) what's VISIBLE now and was COVERED before
             *   2) what's EXPOSED now less what was EXPOSED before
             *
             * note that (1) is conservative, we start with the whole
             * visible region but only keep what used to be covered by
             * something -- which mean it may have been exposed.
             *
             * (2) handles areas that were not covered by anything but got
             * exposed because of a resize.
             */
            const Region newExposed = visibleRegion - coveredRegion;
            const Region oldVisibleRegion = layer->visibleRegion;
            const Region oldCoveredRegion = layer->coveredRegion;
            const Region oldExposed = oldVisibleRegion - oldCoveredRegion;
            dirty = (visibleRegion&oldCoveredRegion) | (newExposed-oldExposed);
        }
        dirty.subtractSelf(aboveOpaqueLayers);

        // accumulate to the screen dirty region
        outDirtyRegion.orSelf(dirty);

        // Update aboveOpaqueLayers for next (lower) layer
        aboveOpaqueLayers.orSelf(opaqueRegion);

        // Store the visible region in screen space
        layer->setVisibleRegion(visibleRegion);
        layer->setCoveredRegion(coveredRegion);
        layer->setVisibleNonTransparentRegion(
                visibleRegion.subtract(transparentRegion));
    }

    outOpaqueRegion = aboveOpaqueLayers;
}

void SurfaceFlinger::invalidateLayerStack(uint32_t layerStack,
        const Region& dirty) {
    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        const sp<DisplayDevice>& hw(mDisplays[dpy]);
        if (hw->getLayerStack() == layerStack) {
            hw->dirtyRegion.orSelf(dirty);
        }
    }
}

void SurfaceFlinger::handlePageFlip()
{
    Region dirtyRegion;

    bool visibleRegions = false;
    const LayerVector& layers(mDrawingState.layersSortedByZ);
    const size_t count = layers.size();
    for (size_t i=0 ; i<count ; i++) {
        const sp<Layer>& layer(layers[i]);
        const Region dirty(layer->latchBuffer(visibleRegions));
        const Layer::State& s(layer->getDrawingState());
        invalidateLayerStack(s.layerStack, dirty);
    }

    mVisibleRegionsDirty |= visibleRegions;
}

void SurfaceFlinger::invalidateHwcGeometry()
{
    mHwWorkListDirty = true;
}


// MStar Android Patch Begin
bool SurfaceFlinger::doDisplayComposition(const sp<const DisplayDevice>& hw,
        const Region& inDirtyRegion)
// MStar Android Patch End
{
    Region dirtyRegion(inDirtyRegion);

    // compute the invalid region
    hw->swapRegion.orSelf(dirtyRegion);

    uint32_t flags = hw->getFlags();
    if (flags & DisplayDevice::SWAP_RECTANGLE) {
        // we can redraw only what's dirty, but since SWAP_RECTANGLE only
        // takes a rectangle, we must make sure to update that whole
        // rectangle in that case
        dirtyRegion.set(hw->swapRegion.bounds());
    } else {
        if (flags & DisplayDevice::PARTIAL_UPDATES) {
            // We need to redraw the rectangle that will be updated
            // (pushed to the framebuffer).
            // This is needed because PARTIAL_UPDATES only takes one
            // rectangle instead of a region (see DisplayDevice::flip())
            dirtyRegion.set(hw->swapRegion.bounds());
        } else {
            // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
            if (dirtyRegion.bounds().isEmpty() && hw->getDisplayType() < DisplayDevice::DISPLAY_EXTERNAL) {
                return false;
            }
#else
            if (hw->getDisplayType() == DisplayDevice::DISPLAY_VIRTUAL) {
                const Vector< sp<Layer> >& layers(hw->getVisibleLayersSortedByZ());
                static size_t prelayercount = 0;
                if (dirtyRegion.bounds().isEmpty() || (layers.size() == 0 && prelayercount == 0)) {
                    prelayercount = layers.size();
                    return false;
                }
                prelayercount = layers.size();
            } else {
                if (dirtyRegion.bounds().isEmpty() && hw->getDisplayType() < DisplayDevice::DISPLAY_EXTERNAL) {
                    return false;
                }
            }
#endif
            // MStar Android Patch End

            // we need to redraw everything (the whole screen)
            dirtyRegion.set(hw->bounds());
            hw->swapRegion = dirtyRegion;
        }
    }

    if (CC_LIKELY(!mDaltonize)) {
        doComposeSurfaces(hw, dirtyRegion);
    } else {
        RenderEngine& engine(getRenderEngine());
        engine.beginGroup(mDaltonizer());
        doComposeSurfaces(hw, dirtyRegion);
        engine.endGroup();
    }

    // update the swap region and clear the dirty region
    hw->swapRegion.orSelf(dirtyRegion);

    // swap buffers (presentation)
    hw->swapBuffers(getHwComposer());

    // MStar Android Patch Begin
    return true;
    // MStar Android Patch End
}

void SurfaceFlinger::doComposeSurfaces(const sp<const DisplayDevice>& hw, const Region& dirty)
{
    RenderEngine& engine(getRenderEngine());
    const int32_t id = hw->getHwcDisplayId();
    HWComposer& hwc(getHwComposer());
    HWComposer::LayerListIterator cur = hwc.begin(id);
    const HWComposer::LayerListIterator end = hwc.end(id);

    // MStar Android Patch Begin
#ifndef ENABLE_HWCOMPOSER_13
    static int preDisplayMode = DISPLAYMODE_NORMAL;
    char property[PROPERTY_VALUE_MAX] = {0};
    int displayMode = DISPLAYMODE_NORMAL;
    memset(property,0,sizeof(property));
    if (property_get(MSTAR_DESK_DISPLAY_MODE, property, NULL) > 0) {
        displayMode = atoi(property);
    }
    if (displayMode < 0 || displayMode >= DISPLAYMODE_MAX) {
        displayMode = DISPLAYMODE_NORMAL;
        ALOGE("The property mstar.desk-display-mode is incorrect that it will reset to default value");
    }
#endif
    // MStar Android Patch End


    bool hasGlesComposition = hwc.hasGlesComposition(id);
    if (hasGlesComposition) {
        if (!hw->makeCurrent(mEGLDisplay, mEGLContext)) {
            ALOGW("DisplayDevice::makeCurrent failed. Aborting surface composition for display %s",
                  hw->getDisplayName().string());
            return;
        }

        // Never touch the framebuffer if we don't have any framebuffer layers
        const bool hasHwcComposition = hwc.hasHwcComposition(id);

        // MStar Android Patch Begin
        if (hasHwcComposition) {
            // when using overlays, we assume a fully transparent framebuffer
            // NOTE: we could reduce how much we need to clear, for instance
            // remove where there are opaque FB layers. however, on some
            // GPUs doing a "clean slate" clear might be more efficient.
            // We'll revisit later if needed.
            const int fbHeight = hw->getHeight();
            const int fbWidth = hw->getWidth();
            int realHeight = fbHeight;
            int realWidth = fbWidth;
#ifdef BUILD_FOR_STB
            realHeight = hw->getRealDisplayHeight();
            realWidth = hw->getRealDisplayWidth();
#else
            float hScale = 1.0f;
            float vScale = 1.0f;
            getScaleParamByCurrentResolutionMode(&hScale, &vScale);
            realHeight = fbHeight * hScale;
            realWidth = fbWidth * vScale;
#endif
            int xScissorOffset = 0;
            int yScissorOffset = 0;
            int scissorWidth = 0;
            int scissorHeight = 0;
            int deltaHeight = fbHeight - realHeight;
            // defined used for STB reproducerate
            int adjustloffset = 0;
            int adjusttopoffset = 0;
            int adjustwidth = 0;
            int adjustHeight = 0;
            int xReproduceRateOffset = 0;
            int yReproduceRateOffset = 0;

#ifdef BUILD_FOR_STB
            hw->getAdjustValue(&adjustwidth, &adjustHeight, &adjustloffset, &adjusttopoffset);
            yReproduceRateOffset = realHeight - adjustHeight - adjusttopoffset;
            xReproduceRateOffset = adjustloffset;
#endif
#ifndef BUILD_FOR_STB
            xScissorOffset = xReproduceRateOffset;
            yScissorOffset = deltaHeight + yReproduceRateOffset;

            if (adjustwidth == 0 || adjustHeight == 0) {
                scissorWidth = realWidth;
                scissorHeight = realHeight;
            } else {
                scissorWidth = adjustwidth;
                scissorHeight = adjustHeight;
            }
            engine.setScissor(xScissorOffset, yScissorOffset, scissorWidth, scissorHeight);
#endif
            engine.clearWithColor(0, 0, 0, 0);
#ifndef BUILD_FOR_STB
            engine.disableScissor();
#endif
        // MStar Android Patch End
        } else {
            // MStar Android Patch Begin
            // Patch for EncodeVirtualDisplay cts on HWC 1.0
#ifndef ENABLE_HWCOMPOSER_13
            if (hw->getLayerStack()==0) {
#endif
            // MStar Android Patch End
            // we start with the whole screen area
            const Region bounds(hw->getBounds());

            // we remove the scissor part
            // we're left with the letterbox region
            // (common case is that letterbox ends-up being empty)
            const Region letterbox(bounds.subtract(hw->getScissor()));

            // compute the area to clear
            Region region(hw->undefinedRegion.merge(letterbox));

            // but limit it to the dirty region
            region.andSelf(dirty);

            // screen is already cleared here
            if (!region.isEmpty()) {
                // can happen with SurfaceView
                drawWormhole(hw, region);
            }
            // MStar Android Patch Begin
            // Patch for EncodeVirtualDisplay cts on HWC 1.0
#ifndef ENABLE_HWCOMPOSER_13
            }
#endif
            // MStar Android Patch End
        }

        // MStar Android Patch Begin
#ifndef ENABLE_HWCOMPOSER_13
        if (hw->getDisplayType() != DisplayDevice::DISPLAY_PRIMARY) {
#else
        if (1) {
#endif
        // MStar Android Patch End
            // just to be on the safe side, we don't set the
            // scissor on the main display. It should never be needed
            // anyways (though in theory it could since the API allows it).
            const Rect& bounds(hw->getBounds());
            const Rect& scissor(hw->getScissor());
            if (scissor != bounds) {
                // scissor doesn't match the screen's dimensions, so we
                // need to clear everything outside of it and enable
                // the GL scissor so we don't draw anything where we shouldn't

                // enable scissor for this frame
                const uint32_t height = hw->getHeight();
                engine.setScissor(scissor.left, height - scissor.bottom,
                        scissor.getWidth(), scissor.getHeight());
            }
        }
    }

    /*
     * and then, render the layers targeted at the framebuffer
     */

    const Vector< sp<Layer> >& layers(hw->getVisibleLayersSortedByZ());
    const size_t count = layers.size();
    const Transform& tr = hw->getTransform();
    if (cur != end) {
        // we're using h/w composer
        for (size_t i=0 ; i<count && cur!=end ; ++i, ++cur) {
            const sp<Layer>& layer(layers[i]);
            const Region clip(dirty.intersect(tr.transform(layer->visibleRegion)));
            if (!clip.isEmpty()) {
                switch (cur->getCompositionType()) {
                    case HWC_OVERLAY: {
                        const Layer::State& state(layer->getDrawingState());
                        // MStar Android Patch Begin
                        if ((cur->getHints() & HWC_HINT_CLEAR_FB)
                                && layer->isOpaque() && (state.alpha == 0xFF)) {
                            // never clear the very first layer since we're
                            // guaranteed the FB is already cleared
#ifdef ENABLE_HWCOMPOSER_13
                            layer->clearWithOpenGL(hw, clip);
#else
                            wrapDrawLayer(layer, hw, clip, HWC_OVERLAY, displayMode);
#endif
                        }
                        // MStar Android Patch End
                        break;
                    }
                    case HWC_FRAMEBUFFER: {
                        // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
                        layer->draw(hw, clip);
#else
                        wrapDrawLayer(layer, hw, clip, HWC_FRAMEBUFFER, displayMode);
#endif
                        // MStar Android Patch End
                        break;
                    }
                    case HWC_FRAMEBUFFER_TARGET: {
                        // this should not happen as the iterator shouldn't
                        // let us get there.
                        ALOGW("HWC_FRAMEBUFFER_TARGET found in hwc list (index=%d)", i);
                        break;
                    }
                }
            }
            layer->setAcquireFence(hw, *cur);
        }
    } else {
        // we're not using h/w composer
        for (size_t i=0 ; i<count ; ++i) {
            const sp<Layer>& layer(layers[i]);
            const Region clip(dirty.intersect(
                    tr.transform(layer->visibleRegion)));
            if (!clip.isEmpty()) {
                // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
                layer->draw(hw, clip);
#else
                wrapDrawLayer(layer, hw, clip, HWC_FRAMEBUFFER, displayMode);
#endif
                // MStar Android Patch End
            }
        }
    }

    // MStar Android Patch Begin
#ifndef ENABLE_HWCOMPOSER_13
    if ( preDisplayMode != displayMode) {
        // it seems not proper set the DisplayMode here
        // fbpost is the proper place and not need the forceDrawCallFinish
        ALOGI("SurfaceFlinger::doComposeSurface displayMode change! display model change!!");
        preDisplayMode = displayMode;
        engine.forceDrawCallFinish();
        mHwc->setDisplayMode(preDisplayMode);
    }
#endif
    // MStar Android Patch End

    // disable scissor at the end of the frame
    engine.disableScissor();
}

// MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
void SurfaceFlinger::wrapDrawLayer(const sp<Layer>& layer, const sp<const DisplayDevice>& hw, const Region& dirty, int compositionType, int displayMode) {
    // for the hwcomposer 1.3 ,no need do wrapDrawLayer
    return ;
}
#else
void SurfaceFlinger::wrapDrawLayer(const sp<Layer>& layer, const sp<const DisplayDevice>& hw, const Region& dirty, int compositionType, int displayMode) {
    RenderEngine& engine(getRenderEngine());
    const int fbHeight = hw->getHeight();
    const int fbWidth = hw->getWidth();
    int realHeight = fbHeight;
    int realWidth = fbWidth;
#ifdef BUILD_FOR_STB
   realHeight = hw->getRealDisplayHeight();
   realWidth = hw->getRealDisplayWidth();
#else
    float hScale = 1.0f;
    float vScale = 1.0f;
    getScaleParamByCurrentResolutionMode(&hScale, &vScale);
    realHeight = fbHeight * hScale;
    realWidth = fbWidth * vScale;
#endif
    int xScissorOffset = 0;
    int yScissorOffset = 0;
    int scissorWidth = 0;
    int scissorHeight = 0;
    int deltaHeight = fbHeight - realHeight;
    //defined used for STB reproducerate
    int adjustloffset = 0;
    int adjusttopoffset = 0;
    int adjustwidth = 0;
    int adjustHeight = 0;
    int xReproduceRateOffset = 0;
    int yReproduceRateOffset = 0;

#ifdef BUILD_FOR_STB
    hw->getAdjustValue(&adjustwidth, &adjustHeight, &adjustloffset, &adjusttopoffset);
    yReproduceRateOffset = realHeight - adjustHeight - adjusttopoffset;
    xReproduceRateOffset = adjustloffset;
#endif
    Transform tr(hw->getTransform());
    bool bneedChangeFiltering = (tr[0][0] != 1.0f) || (tr[1][1] != 1.0f);
    bool bFiltering = false;
    Region clip(dirty);
    Region AutoStereoRgn;
    AutoStereoRgn.set(hw->getBounds());

#define DRAW_ALL_LAYERS_MACRO \
    do { \
        size_t clipRegionCount; \
        Region clipTmp = clip; \
        Rect const *const clipRegionRects = clip.getArray(&clipRegionCount); \
        for (size_t ii=0 ; ii<clipRegionCount ; ii++) { \
            Rect clipRegionRectTmp = clipRegionRects[ii]; \
            clipRegionRectTmp.left = STEREO_RATE_W*clipRegionRectTmp.left + STEREO_OFFSET_W; \
            clipRegionRectTmp.top = STEREO_RATE_H*clipRegionRectTmp.top + STEREO_OFFSET_H; \
            clipRegionRectTmp.right = STEREO_RATE_W*clipRegionRectTmp.right + STEREO_OFFSET_W; \
            clipRegionRectTmp.bottom = STEREO_RATE_H*clipRegionRectTmp.bottom + STEREO_OFFSET_H; \
            clipTmp.orSelf(clipRegionRectTmp); \
        } \
        clip = clipTmp; \
        if (layer->getAutoStereo()) {\
            SWITH_2_STEREO;\
        } else {\
            SWITH_2_SINGLE;\
        }\
        if (bneedChangeFiltering) { \
            bFiltering = layer->getFiltering(); \
            layer->setFiltering(true); \
        }\
        clip = clip.intersect(AutoStereoRgn); \
        if (compositionType == HWC_OVERLAY) { \
            layer->clearWithOpenGL(hw, clip); \
        } \
        else if (compositionType == HWC_FRAMEBUFFER) { \
            layer->draw(hw, clip); \
        } \
        if (bneedChangeFiltering) { \
            layer->setFiltering(bFiltering); \
        } \
    } while(0)

    // process stereo mode
    if (displayMode == DISPLAYMODE_LEFTRIGHT || displayMode == DISPLAYMODE_LEFTRIGHT_FR) {
        bool stereo_mode;
#define SWITH_2_SINGLE \
        while (stereo_mode) { \
            engine.setGLViewPort(0, 0, fbWidth, fbHeight); \
            stereo_mode = false; \
            break; \
        }
#define STEREO_RATE_W 0.5
#define STEREO_RATE_H 1
#define STEREO_OFFSET_W 0
#define STEREO_OFFSET_H 0

#define SWITH_2_STEREO \
    while (!stereo_mode) { \
        bneedChangeFiltering = true; \
        engine.setGLViewPort(0, 0, fbWidth/2, fbHeight); \
        stereo_mode = true; \
        break; \
    }

    stereo_mode = true;
    bneedChangeFiltering = true;
    engine.setGLViewPort(0, 0, fbWidth/2, fbHeight);
    AutoStereoRgn.set(Rect(0, 0, fbWidth/2, fbHeight));
    //scissor for STB reproducerate
    xScissorOffset = xReproduceRateOffset/2;
    yScissorOffset = deltaHeight + yReproduceRateOffset;
    if (adjustwidth==0 || adjustHeight==0) { //TV  not support reproducerate
        scissorWidth = realWidth / 2;
        scissorHeight = realHeight;
    } else {
        scissorWidth = adjustwidth / 2;
        scissorHeight = adjustHeight;
    }
    engine.setScissor(xScissorOffset, yScissorOffset, scissorWidth, scissorHeight);
    DRAW_ALL_LAYERS_MACRO ;
    engine.disableScissor();

#undef STEREO_OFFSET_W
#undef SWITH_2_STEREO

#define STEREO_OFFSET_W (hw->bounds().width()/2)
#define SWITH_2_STEREO \
    while (!stereo_mode) { \
        bneedChangeFiltering = true; \
        engine.setGLViewPort(fbWidth/2, 0, fbWidth/2, fbHeight); \
        stereo_mode = true; \
        break; \
    }

    stereo_mode = true;
    bneedChangeFiltering = true;
    engine.setGLViewPort(realWidth/2, 0, fbWidth/2, fbHeight);
    AutoStereoRgn.set(Rect(fbWidth/2, 0, fbWidth, fbHeight));
    //scissor for STB reproducerate
    xScissorOffset = realWidth / 2 + xReproduceRateOffset/2;
    engine.setScissor(xScissorOffset, yScissorOffset, scissorWidth, scissorHeight);
    DRAW_ALL_LAYERS_MACRO ;
    engine.disableScissor();

#undef SWITH_2_STEREO
#undef STEREO_RATE_W
#undef STEREO_RATE_H
#undef STEREO_OFFSET_W
#undef STEREO_OFFSET_H
#undef SWITH_2_SINGLE

    engine.setGLViewPort(0, 0, fbWidth, fbHeight);
    AutoStereoRgn.set(hw->getBounds());
  } else if (displayMode == DISPLAYMODE_TOPBOTTOM || displayMode == DISPLAYMODE_TOPBOTTOM_LA) {
        bool stereo_mode;
#define SWITH_2_SINGLE \
        while (stereo_mode) { \
            engine.setGLViewPort(0, 0, fbWidth, fbHeight); \
            stereo_mode = false; \
            break; \
        }

#define STEREO_RATE_W 1
#define STEREO_RATE_H 0.5
#define STEREO_OFFSET_W 0
#define STEREO_OFFSET_H (hw->bounds().height()/2)

#define SWITH_2_STEREO \
        while (!stereo_mode) { \
            bneedChangeFiltering = true; \
            engine.setGLViewPort(0, 0, fbWidth, fbHeight/2); \
            stereo_mode = true; \
            break; \
        }

        stereo_mode = true;
        bneedChangeFiltering = true;
        //BOTTOM part
        engine.setGLViewPort(0, deltaHeight / 2, fbWidth, fbHeight/2);
        AutoStereoRgn.set(Rect(0, fbHeight/2, fbWidth, fbHeight));
        xScissorOffset = xReproduceRateOffset;
        yScissorOffset = deltaHeight + yReproduceRateOffset/2;
        if (adjustwidth==0 || adjustHeight==0) { //TV not support reproducerate
            scissorWidth = realWidth;
            scissorHeight = realHeight / 2;
        } else {
            scissorWidth = adjustwidth;
            scissorHeight = adjustHeight / 2;
        }
        engine.setScissor(xScissorOffset, yScissorOffset, scissorWidth, scissorHeight);
        DRAW_ALL_LAYERS_MACRO ;

#undef STEREO_OFFSET_H
#undef SWITH_2_STEREO

#define STEREO_OFFSET_H 0
#define SWITH_2_STEREO \
        while (!stereo_mode) { \
            bneedChangeFiltering = true; \
            engine.setGLViewPort(0, fbHeight/2, fbWidth, fbHeight/2); \
            stereo_mode = true; \
            break; \
        }

        stereo_mode = true;
        bneedChangeFiltering = true;
        //TOP part
        engine.setGLViewPort(0, fbHeight/2, fbWidth, fbHeight/2);
        AutoStereoRgn.set(Rect(0, 0, fbWidth, fbHeight/2));
        yScissorOffset = deltaHeight + realHeight/2 + yReproduceRateOffset/2;
        engine.setScissor(xScissorOffset, yScissorOffset, scissorWidth, scissorHeight);
        DRAW_ALL_LAYERS_MACRO ;
        engine.disableScissor();

#undef SWITH_2_STEREO
#undef STEREO_RATE_W
#undef STEREO_RATE_H
#undef STEREO_OFFSET_W
#undef STEREO_OFFSET_H
#undef SWITH_2_SINGLE

    engine.setGLViewPort(0, 0, fbWidth, fbHeight);
    AutoStereoRgn.set(hw->getBounds());
  } else if (displayMode == DISPLAYMODE_TOP_LA || displayMode == DISPLAYMODE_TOP_ONLY) {
        bool stereo_mode;
#define SWITH_2_SINGLE \
        while (stereo_mode) { \
            engine.setGLViewPort(0, 0, fbWidth, fbHeight); \
            stereo_mode = false; \
            break; \
        }

#define STEREO_RATE_W 1
#define STEREO_RATE_H 0.5
#define STEREO_OFFSET_W 0
#define STEREO_OFFSET_H 0

#define SWITH_2_STEREO \
        while (!stereo_mode) { \
            bneedChangeFiltering = true; \
            engine.setGLViewPort(0, fbHeight/2, fbWidth, fbHeight/2); \
            stereo_mode = true; \
            break; \
        }

        stereo_mode = true;
        bneedChangeFiltering = true;
        engine.setGLViewPort(0, fbHeight/2, fbWidth, fbHeight/2);
        AutoStereoRgn.set(Rect(0, 0, fbWidth, fbHeight/2));
        //Scissor for TOP_ONLY
        xScissorOffset =  xReproduceRateOffset;
        yScissorOffset =  (fbHeight - realHeight/ 2) + yReproduceRateOffset/ 2;
        if (adjustwidth==0 || adjustHeight==0) {
            scissorWidth = realWidth;
            scissorHeight = realHeight / 2;
        } else {
            scissorWidth = adjustwidth;
            scissorHeight = adjustHeight / 2;
        }
        engine.setScissor(xScissorOffset, yScissorOffset, scissorWidth, scissorHeight);
        DRAW_ALL_LAYERS_MACRO ;
        engine.disableScissor();

#undef SWITH_2_STEREO
#undef STEREO_RATE_W
#undef STEREO_RATE_H
#undef STEREO_OFFSET_W
#undef STEREO_OFFSET_H
#undef SWITH_2_SINGLE

        engine.setGLViewPort(0, 0, fbWidth, fbHeight);
        AutoStereoRgn.set(hw->getBounds());
    } else if (displayMode == DISPLAYMODE_BOTTOM_LA || displayMode == DISPLAYMODE_BOTTOM_ONLY) {
        bool stereo_mode;
#define SWITH_2_SINGLE \
        while (stereo_mode) { \
            engine.setGLViewPort(0, 0, fbWidth, fbHeight); \
            stereo_mode = false; \
            break; \
        }

#define STEREO_RATE_W 1
#define STEREO_RATE_H 0.5
#define STEREO_OFFSET_W 0
#define STEREO_OFFSET_H (hw->bounds().height()/2)

#define SWITH_2_STEREO \
        while (!stereo_mode) { \
            bneedChangeFiltering = true; \
            engine.setGLViewPort(0, 0, fbWidth, fbHeight/2); \
            stereo_mode = true; \
            break; \
        }

        stereo_mode = true;
        bneedChangeFiltering = true;
        engine.setGLViewPort(0, deltaHeight / 2, fbWidth, fbHeight/2);
        AutoStereoRgn.set(Rect(0, fbHeight/2, fbWidth, fbHeight));
        //Scissor for BOTTOM_ONLY
        xScissorOffset = xReproduceRateOffset;
        yScissorOffset = deltaHeight + yReproduceRateOffset/2;
        if (adjustwidth==0 || adjustHeight==0) { //TV not support reproduce rate
            scissorWidth = realWidth;
            scissorHeight = realHeight/ 2;
        } else {
            scissorWidth = adjustwidth;
            scissorHeight = adjustHeight / 2;
        }
        engine.setScissor(xScissorOffset, yScissorOffset, scissorWidth, scissorHeight);
        DRAW_ALL_LAYERS_MACRO ;

#undef SWITH_2_STEREO
#undef STEREO_RATE_W
#undef STEREO_RATE_H
#undef STEREO_OFFSET_W
#undef STEREO_OFFSET_H
#undef SWITH_2_SINGLE

        engine.setGLViewPort(0, 0, fbWidth, fbHeight);
        AutoStereoRgn.set(hw->getBounds());
    } else if (displayMode==DISPLAYMODE_LEFT_ONLY) {
        bool stereo_mode;
#define SWITH_2_SINGLE \
        while (stereo_mode) { \
            engine.setGLViewPort(0, 0, fbWidth, fbHeight); \
            stereo_mode = false; \
            break; \
        }

#define STEREO_RATE_W 0.5
#define STEREO_RATE_H 1
#define STEREO_OFFSET_W 0
#define STEREO_OFFSET_H 0

#define SWITH_2_STEREO \
        while (!stereo_mode) { \
            bneedChangeFiltering = true; \
            engine.setGLViewPort(0, 0, fbWidth/2, fbHeight); \
            stereo_mode = true; \
            break; \
        }

        stereo_mode = true;
        bneedChangeFiltering = true;
        engine.setGLViewPort(0, 0, fbWidth/2, fbHeight);
        AutoStereoRgn.set(Rect(0, 0, fbWidth/2, fbHeight));
        //Scissor for LEFT_ONLY
        xScissorOffset = xReproduceRateOffset/2;
        yScissorOffset = deltaHeight+ yReproduceRateOffset;
        if (adjustwidth==0 || adjustHeight==0) { //TV not support reproduce rate
            scissorWidth = realWidth/2;
            scissorHeight = realHeight;
        } else {
            scissorWidth = adjustwidth/ 2;
            scissorHeight = adjustHeight;
        }
        engine.setScissor(xScissorOffset, yScissorOffset, scissorWidth, scissorHeight);
        DRAW_ALL_LAYERS_MACRO ;
        engine.disableScissor();

#undef SWITH_2_STEREO
#undef STEREO_RATE_W
#undef STEREO_RATE_H
#undef STEREO_OFFSET_W
#undef STEREO_OFFSET_H
#undef SWITH_2_SINGLE

        engine.setGLViewPort(0, 0, fbWidth, fbHeight);
        AutoStereoRgn.set(hw->getBounds());
    } else if (displayMode == DISPLAYMODE_RIGHT_ONLY) {
        bool stereo_mode;
#define SWITH_2_SINGLE \
        while (stereo_mode) { \
            engine.setGLViewPort(0, 0, fbWidth, fbHeight); \
            stereo_mode = false; \
            break; \
        }

#define STEREO_RATE_W 0.5
#define STEREO_RATE_H 1
#define STEREO_OFFSET_W (hw->bounds().width()/2)
#define STEREO_OFFSET_H 0

#define SWITH_2_STEREO \
        while (!stereo_mode) { \
            bneedChangeFiltering = true; \
            engine.setGLViewPort(fbWidth/2, 0, fbWidth/2, fbHeight); \
            stereo_mode = true; \
            break; \
        }

        stereo_mode = true;
        bneedChangeFiltering = true;
        engine.setGLViewPort(realWidth/2, 0, fbWidth/2, fbHeight);
        AutoStereoRgn.set(Rect(fbWidth/2, 0, fbWidth, fbHeight));
        //Scissor test for RIGHT_ONLY
        xScissorOffset = realWidth/2 + xReproduceRateOffset/2;
        yScissorOffset =  deltaHeight+ yReproduceRateOffset;
         if (adjustwidth==0 || adjustHeight==0) {
            scissorWidth = realWidth / 2;
            scissorHeight = realHeight;
        } else {
            scissorWidth = adjustwidth /2;
            scissorHeight = adjustHeight;
        }
        engine.setScissor(xScissorOffset, yScissorOffset, scissorWidth, scissorHeight);
        DRAW_ALL_LAYERS_MACRO ;
        engine.disableScissor();

#undef SWITH_2_STEREO
#undef STEREO_RATE_W
#undef STEREO_RATE_H
#undef STEREO_OFFSET_W
#undef STEREO_OFFSET_H
#undef SWITH_2_SINGLE

        engine.setGLViewPort(0, 0, fbWidth, fbHeight);
        AutoStereoRgn.set(hw->getBounds());
    } else {
#define SWITH_2_SINGLE \
        do{}while(0)

#define STEREO_RATE_W 1
#define STEREO_RATE_H 1
#define STEREO_OFFSET_W 0
#define STEREO_OFFSET_H 0

#define SWITH_2_STEREO \
        do{}while(0)

        engine.setGLViewPort(0, 0, fbWidth, fbHeight);
        xScissorOffset =  xReproduceRateOffset;
        yScissorOffset =  deltaHeight + yReproduceRateOffset;
        if (adjustwidth== 0||adjustHeight== 0) {
            scissorWidth = realWidth;
            scissorHeight = realHeight;
        } else {
            scissorWidth = adjustwidth;
            scissorHeight = adjustHeight;
        }
        engine.setScissor(xScissorOffset, yScissorOffset, scissorWidth, scissorHeight);
        DRAW_ALL_LAYERS_MACRO ;
#undef SWITH_2_STEREO
#undef STEREO_RATE_W
#undef STEREO_RATE_H
#undef STEREO_OFFSET_W
#undef STEREO_OFFSET_H
#undef SWITH_2_SINGLE
    }
}
#endif
// MStar Android Patch End

void SurfaceFlinger::drawWormhole(const sp<const DisplayDevice>& hw, const Region& region) const {
    const int32_t height = hw->getHeight();
    RenderEngine& engine(getRenderEngine());
    // MStar Android Patch Begin
    if (hw->getDisplayType() == DisplayDevice::DISPLAY_PRIMARY) {
        const int fbHeight = hw->getHeight();
        const int fbWidth = hw->getWidth();
        int realHeight = fbHeight;
        int realWidth = fbWidth;
#ifdef BUILD_FOR_STB
        realHeight = hw->getRealDisplayHeight();
        realWidth = hw->getRealDisplayWidth();
#else
        float hScale = 1.0f;
        float vScale = 1.0f;
        getScaleParamByCurrentResolutionMode(&hScale, &vScale);
        realHeight = fbHeight * hScale;
        realWidth = fbWidth * vScale;
#endif
        int xScissorOffset = 0;
        int yScissorOffset = 0;
        int scissorWidth = 0;
        int scissorHeight = 0;
        int deltaHeight = fbHeight - realHeight;
        // defined used for STB reproducerate
        int adjustloffset = 0;
        int adjusttopoffset = 0;
        int adjustwidth = 0;
        int adjustHeight = 0;
        int xReproduceRateOffset = 0;
        int yReproduceRateOffset = 0;
#ifdef BUILD_FOR_STB
        hw->getAdjustValue(&adjustwidth, &adjustHeight, &adjustloffset, &adjusttopoffset);
        yReproduceRateOffset = realHeight - adjustHeight - adjusttopoffset;
        xReproduceRateOffset = adjustloffset;
#endif
    //should not do scissor for STB because of reproducerate
#ifndef BUILD_FOR_STB
        xScissorOffset = xReproduceRateOffset;
        yScissorOffset = deltaHeight + yReproduceRateOffset;
        if (adjustwidth == 0 || adjustHeight == 0) {
            scissorWidth = realWidth;
            scissorHeight = realHeight;
        } else {
            scissorWidth = adjustwidth;
            scissorHeight = adjustHeight;
        }
        engine.setScissor(xScissorOffset, yScissorOffset, scissorWidth, scissorHeight);
#endif
        // MStar Android Patch End
        engine.fillRegionWithColor(region, height, 0, 0, 0, 0);
        // MStar Android Patch Begin
#ifndef BUILD_FOR_STB
        engine.disableScissor();
#endif
    } else {
        engine.fillRegionWithColor(region, height, 0, 0, 0, 0);
    }
    // MStar Android Patch End
}

void SurfaceFlinger::addClientLayer(const sp<Client>& client,
        const sp<IBinder>& handle,
        const sp<IGraphicBufferProducer>& gbc,
        const sp<Layer>& lbc)
{
    // attach this layer to the client
    client->attachLayer(handle, lbc);

    // add this layer to the current state list
    Mutex::Autolock _l(mStateLock);
    mCurrentState.layersSortedByZ.add(lbc);
    mGraphicBufferProducerList.add(gbc->asBinder());
}

status_t SurfaceFlinger::removeLayer(const sp<Layer>& layer) {
    Mutex::Autolock _l(mStateLock);
    ssize_t index = mCurrentState.layersSortedByZ.remove(layer);
    if (index >= 0) {
        mLayersPendingRemoval.push(layer);
        mLayersRemoved = true;
        setTransactionFlags(eTransactionNeeded);
        return NO_ERROR;
    }
    return status_t(index);
}

uint32_t SurfaceFlinger::peekTransactionFlags(uint32_t flags) {
    return android_atomic_release_load(&mTransactionFlags);
}

uint32_t SurfaceFlinger::getTransactionFlags(uint32_t flags) {
    return android_atomic_and(~flags, &mTransactionFlags) & flags;
}

uint32_t SurfaceFlinger::setTransactionFlags(uint32_t flags) {
    uint32_t old = android_atomic_or(flags, &mTransactionFlags);
    if ((old & flags)==0) { // wake the server up
        signalTransaction();
    }
    return old;
}

void SurfaceFlinger::setTransactionState(
        const Vector<ComposerState>& state,
        const Vector<DisplayState>& displays,
        uint32_t flags)
{
    ATRACE_CALL();
    Mutex::Autolock _l(mStateLock);
    uint32_t transactionFlags = 0;

    if (flags & eAnimation) {
        // For window updates that are part of an animation we must wait for
        // previous animation "frames" to be handled.
        while (mAnimTransactionPending) {
            status_t err = mTransactionCV.waitRelative(mStateLock, s2ns(5));
            if (CC_UNLIKELY(err != NO_ERROR)) {
                // just in case something goes wrong in SF, return to the
                // caller after a few seconds.
                ALOGW_IF(err == TIMED_OUT, "setTransactionState timed out "
                        "waiting for previous animation frame");
                mAnimTransactionPending = false;
                break;
            }
        }
    }

    size_t count = displays.size();
    for (size_t i=0 ; i<count ; i++) {
        const DisplayState& s(displays[i]);
        transactionFlags |= setDisplayStateLocked(s);
    }

    count = state.size();
    for (size_t i=0 ; i<count ; i++) {
        const ComposerState& s(state[i]);
        // Here we need to check that the interface we're given is indeed
        // one of our own. A malicious client could give us a NULL
        // IInterface, or one of its own or even one of our own but a
        // different type. All these situations would cause us to crash.
        //
        // NOTE: it would be better to use RTTI as we could directly check
        // that we have a Client*. however, RTTI is disabled in Android.
        if (s.client != NULL) {
            sp<IBinder> binder = s.client->asBinder();
            if (binder != NULL) {
                String16 desc(binder->getInterfaceDescriptor());
                if (desc == ISurfaceComposerClient::descriptor) {
                    sp<Client> client( static_cast<Client *>(s.client.get()) );
                    transactionFlags |= setClientStateLocked(client, s.state);
                }
            }
        }
    }

    if (transactionFlags) {
        // this triggers the transaction
        setTransactionFlags(transactionFlags);

        // if this is a synchronous transaction, wait for it to take effect
        // before returning.
        if (flags & eSynchronous) {
            mTransactionPending = true;
        }
        if (flags & eAnimation) {
            mAnimTransactionPending = true;
        }
        while (mTransactionPending) {
            status_t err = mTransactionCV.waitRelative(mStateLock, s2ns(5));
            if (CC_UNLIKELY(err != NO_ERROR)) {
                // just in case something goes wrong in SF, return to the
                // called after a few seconds.
                ALOGW_IF(err == TIMED_OUT, "setTransactionState timed out!");
                mTransactionPending = false;
                break;
            }
        }
    }
}

uint32_t SurfaceFlinger::setDisplayStateLocked(const DisplayState& s)
{
    ssize_t dpyIdx = mCurrentState.displays.indexOfKey(s.token);
    if (dpyIdx < 0)
        return 0;

    uint32_t flags = 0;
    DisplayDeviceState& disp(mCurrentState.displays.editValueAt(dpyIdx));
    if (disp.isValid()) {
        const uint32_t what = s.what;
        if (what & DisplayState::eSurfaceChanged) {
            if (disp.surface->asBinder() != s.surface->asBinder()) {
                disp.surface = s.surface;
                flags |= eDisplayTransactionNeeded;
            }
        }
        if (what & DisplayState::eLayerStackChanged) {
            if (disp.layerStack != s.layerStack) {
                disp.layerStack = s.layerStack;
                flags |= eDisplayTransactionNeeded;
            }
        }
        if (what & DisplayState::eDisplayProjectionChanged) {
            if (disp.orientation != s.orientation) {
                disp.orientation = s.orientation;
                flags |= eDisplayTransactionNeeded;
            }
            if (disp.frame != s.frame) {
                disp.frame = s.frame;
                flags |= eDisplayTransactionNeeded;
            }
            if (disp.viewport != s.viewport) {
                disp.viewport = s.viewport;
                flags |= eDisplayTransactionNeeded;
            }
        }
    }
    return flags;
}

uint32_t SurfaceFlinger::setClientStateLocked(
        const sp<Client>& client,
        const layer_state_t& s)
{
    uint32_t flags = 0;
    sp<Layer> layer(client->getLayerUser(s.surface));
    if (layer != 0) {
        const uint32_t what = s.what;
        if (what & layer_state_t::ePositionChanged) {
            if (layer->setPosition(s.x, s.y))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eLayerChanged) {
            // NOTE: index needs to be calculated before we update the state
            ssize_t idx = mCurrentState.layersSortedByZ.indexOf(layer);
            if (layer->setLayer(s.z)) {
                mCurrentState.layersSortedByZ.removeAt(idx);
                mCurrentState.layersSortedByZ.add(layer);
                // we need traversal (state changed)
                // AND transaction (list changed)
                flags |= eTransactionNeeded|eTraversalNeeded;
            }
        }
        if (what & layer_state_t::eSizeChanged) {
            if (layer->setSize(s.w, s.h)) {
                flags |= eTraversalNeeded;
            }
        }
        if (what & layer_state_t::eAlphaChanged) {
            if (layer->setAlpha(uint8_t(255.0f*s.alpha+0.5f)))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eMatrixChanged) {
            if (layer->setMatrix(s.matrix))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eTransparentRegionChanged) {
            if (layer->setTransparentRegionHint(s.transparentRegion))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eVisibilityChanged) {
            if (layer->setFlags(s.flags, s.mask))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eCropChanged) {
            if (layer->setCrop(s.crop))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eLayerStackChanged) {
            // NOTE: index needs to be calculated before we update the state
            ssize_t idx = mCurrentState.layersSortedByZ.indexOf(layer);
            if (layer->setLayerStack(s.layerStack)) {
                mCurrentState.layersSortedByZ.removeAt(idx);
                mCurrentState.layersSortedByZ.add(layer);
                // we need traversal (state changed)
                // AND transaction (list changed)
                flags |= eTransactionNeeded|eTraversalNeeded;
            }
        }
    }
    return flags;
}

status_t SurfaceFlinger::createLayer(
        const String8& name,
        const sp<Client>& client,
        uint32_t w, uint32_t h, PixelFormat format, uint32_t flags,
        sp<IBinder>* handle, sp<IGraphicBufferProducer>* gbp)
{
    //ALOGD("createLayer for (%d x %d), name=%s", w, h, name.string());
    if (int32_t(w|h) < 0) {
        ALOGE("createLayer() failed, w or h is negative (w=%d, h=%d)",
                int(w), int(h));
        return BAD_VALUE;
    }

    status_t result = NO_ERROR;

    sp<Layer> layer;

    switch (flags & ISurfaceComposerClient::eFXSurfaceMask) {
        case ISurfaceComposerClient::eFXSurfaceNormal:
            result = createNormalLayer(client,
                    name, w, h, flags, format,
                    handle, gbp, &layer);
            break;
        case ISurfaceComposerClient::eFXSurfaceDim:
            result = createDimLayer(client,
                    name, w, h, flags,
                    handle, gbp, &layer);
            break;
        default:
            result = BAD_VALUE;
            break;
    }

    if (result == NO_ERROR) {
        addClientLayer(client, *handle, *gbp, layer);
        setTransactionFlags(eTransactionNeeded);
    }
    return result;
}

status_t SurfaceFlinger::createNormalLayer(const sp<Client>& client,
        const String8& name, uint32_t w, uint32_t h, uint32_t flags, PixelFormat& format,
        sp<IBinder>* handle, sp<IGraphicBufferProducer>* gbp, sp<Layer>* outLayer)
{
    // initialize the surfaces
    switch (format) {
    case PIXEL_FORMAT_TRANSPARENT:
    case PIXEL_FORMAT_TRANSLUCENT:
        format = PIXEL_FORMAT_RGBA_8888;
        break;
    case PIXEL_FORMAT_OPAQUE:
#ifdef NO_RGBX_8888
        format = PIXEL_FORMAT_RGB_565;
#else
        format = PIXEL_FORMAT_RGBX_8888;
#endif
        break;
    }

#ifdef NO_RGBX_8888
    if (format == PIXEL_FORMAT_RGBX_8888)
        format = PIXEL_FORMAT_RGBA_8888;
#endif

    *outLayer = new Layer(this, client, name, w, h, flags);
    status_t err = (*outLayer)->setBuffers(w, h, format, flags);
    if (err == NO_ERROR) {
        *handle = (*outLayer)->getHandle();
        *gbp = (*outLayer)->getBufferQueue();
    }

    ALOGE_IF(err, "createNormalLayer() failed (%s)", strerror(-err));
    return err;
}

status_t SurfaceFlinger::createDimLayer(const sp<Client>& client,
        const String8& name, uint32_t w, uint32_t h, uint32_t flags,
        sp<IBinder>* handle, sp<IGraphicBufferProducer>* gbp, sp<Layer>* outLayer)
{
    *outLayer = new LayerDim(this, client, name, w, h, flags);
    *handle = (*outLayer)->getHandle();
    *gbp = (*outLayer)->getBufferQueue();
    return NO_ERROR;
}

status_t SurfaceFlinger::onLayerRemoved(const sp<Client>& client, const sp<IBinder>& handle)
{
    // called by the window manager when it wants to remove a Layer
    status_t err = NO_ERROR;
    sp<Layer> l(client->getLayerUser(handle));
    if (l != NULL) {
        err = removeLayer(l);
        ALOGE_IF(err<0 && err != NAME_NOT_FOUND,
                "error removing layer=%p (%s)", l.get(), strerror(-err));
    }
    return err;
}

status_t SurfaceFlinger::onLayerDestroyed(const wp<Layer>& layer)
{
    // called by ~LayerCleaner() when all references to the IBinder (handle)
    // are gone
    status_t err = NO_ERROR;
    sp<Layer> l(layer.promote());
    if (l != NULL) {
        err = removeLayer(l);
        ALOGE_IF(err<0 && err != NAME_NOT_FOUND,
                "error removing layer=%p (%s)", l.get(), strerror(-err));
    }
    return err;
}

// ---------------------------------------------------------------------------

void SurfaceFlinger::onInitializeDisplays() {
    // reset screen orientation and use primary layer stack
    Vector<ComposerState> state;
    Vector<DisplayState> displays;
    DisplayState d;
    d.what = DisplayState::eDisplayProjectionChanged |
             DisplayState::eLayerStackChanged;
    d.token = mBuiltinDisplays[DisplayDevice::DISPLAY_PRIMARY];
    d.layerStack = 0;
    d.orientation = DisplayState::eOrientationDefault;
    d.frame.makeInvalid();
    d.viewport.makeInvalid();
    displays.add(d);
    setTransactionState(state, displays, 0);
    onScreenAcquired(getDefaultDisplayDevice());

    const nsecs_t period =
            getHwComposer().getRefreshPeriod(HWC_DISPLAY_PRIMARY);
    mAnimFrameTracker.setDisplayRefreshPeriod(period);
}

void SurfaceFlinger::initializeDisplays() {
    class MessageScreenInitialized : public MessageBase {
        SurfaceFlinger* flinger;
    public:
        MessageScreenInitialized(SurfaceFlinger* flinger) : flinger(flinger) { }
        virtual bool handler() {
            flinger->onInitializeDisplays();
            return true;
        }
    };
    sp<MessageBase> msg = new MessageScreenInitialized(this);
    postMessageAsync(msg);  // we may be called from main thread, use async message
}


void SurfaceFlinger::onScreenAcquired(const sp<const DisplayDevice>& hw) {
    ALOGD("Screen acquired, type=%d flinger=%p", hw->getDisplayType(), this);
    if (hw->isScreenAcquired()) {
        // this is expected, e.g. when power manager wakes up during boot
        ALOGD(" screen was previously acquired");
        return;
    }

    hw->acquireScreen();
    int32_t type = hw->getDisplayType();
    if (type < DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES) {
        // built-in display, tell the HWC
        getHwComposer().acquire(type);

        if (type == DisplayDevice::DISPLAY_PRIMARY) {
            // FIXME: eventthread only knows about the main display right now
            mEventThread->onScreenAcquired();

            resyncToHardwareVsync(true);
        }
    }
    mVisibleRegionsDirty = true;
    repaintEverything();
}

void SurfaceFlinger::onScreenReleased(const sp<const DisplayDevice>& hw) {
    ALOGD("Screen released, type=%d flinger=%p", hw->getDisplayType(), this);
    if (!hw->isScreenAcquired()) {
        ALOGD(" screen was previously released");
        return;
    }

    hw->releaseScreen();
    int32_t type = hw->getDisplayType();
    if (type < DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES) {
        if (type == DisplayDevice::DISPLAY_PRIMARY) {
            disableHardwareVsync(true); // also cancels any in-progress resync

            // FIXME: eventthread only knows about the main display right now
            mEventThread->onScreenReleased();
        }

        // built-in display, tell the HWC
        getHwComposer().release(type);
    }
    mVisibleRegionsDirty = true;
    // from this point on, SF will stop drawing on this display
}

void SurfaceFlinger::unblank(const sp<IBinder>& display) {
    class MessageScreenAcquired : public MessageBase {
        SurfaceFlinger& mFlinger;
        sp<IBinder> mDisplay;
    public:
        MessageScreenAcquired(SurfaceFlinger& flinger,
                const sp<IBinder>& disp) : mFlinger(flinger), mDisplay(disp) { }
        virtual bool handler() {
            const sp<DisplayDevice> hw(mFlinger.getDisplayDevice(mDisplay));
            if (hw == NULL) {
                ALOGE("Attempt to unblank null display %p", mDisplay.get());
            } else if (hw->getDisplayType() >= DisplayDevice::DISPLAY_VIRTUAL) {
                ALOGW("Attempt to unblank virtual display");
            } else {
                mFlinger.onScreenAcquired(hw);
            }
            return true;
        }
    };

    // MStar Android Patch Begin
#ifdef ENABLE_STR
    char property[PROPERTY_VALUE_MAX] = {0};
    if (mStrBootAnimExit) {
        if (!mStandbyRecording) {
            property_set("service.bootanim.exit", "1");
        }
        mStrBootAnimExit = false;
    } else {
        sp<MessageBase> msg = new MessageScreenAcquired(*this, display);
        postMessageSync(msg);
    }
    ALOGD("SurfaceFlinger::unblank end pthread_self()=%lu, getpid()=%d", pthread_self(), getpid());
#else
    sp<MessageBase> msg = new MessageScreenAcquired(*this, display);
    postMessageSync(msg);
#endif
    // MStar Android Patch End
}

void SurfaceFlinger::blank(const sp<IBinder>& display) {
    // MStar Android Patch Begin
#ifdef ENABLE_STR
    ALOGD("SurfaceFlinger::blank begin pthread_self()=%lu, getpid()=%d", pthread_self(), getpid());
#endif
    // MStar Android Patch End

    class MessageScreenReleased : public MessageBase {
        SurfaceFlinger& mFlinger;
        sp<IBinder> mDisplay;
    public:
        MessageScreenReleased(SurfaceFlinger& flinger,
                const sp<IBinder>& disp) : mFlinger(flinger), mDisplay(disp) { }
        virtual bool handler() {
            const sp<DisplayDevice> hw(mFlinger.getDisplayDevice(mDisplay));
            if (hw == NULL) {
                ALOGE("Attempt to blank null display %p", mDisplay.get());
            } else if (hw->getDisplayType() >= DisplayDevice::DISPLAY_VIRTUAL) {
                ALOGW("Attempt to blank virtual display");
            } else {
                mFlinger.onScreenReleased(hw);
            }
            return true;
        }
    };

    // MStar Android Patch Begin
#ifdef ENABLE_STR
    char property[PROPERTY_VALUE_MAX] = {0};
    if (property_get("mstar.str.suspending", property, "0") > 0) {
        if (atoi(property) != 0) {
            signalSuspend();
            mStrBootAnimExit = true;
        }
    } else {
        sp<MessageBase> msg = new MessageScreenReleased(*this, display);
        postMessageSync(msg);
    }
    ALOGD("SurfaceFlinger::blank end pthread_self()=%lu, getpid()=%d", pthread_self(), getpid());
#else
    sp<MessageBase> msg = new MessageScreenReleased(*this, display);
    postMessageSync(msg);
#endif
    // MStar Android Patch End
}

// ---------------------------------------------------------------------------

status_t SurfaceFlinger::dump(int fd, const Vector<String16>& args)
{
    String8 result;

    IPCThreadState* ipc = IPCThreadState::self();
    const int pid = ipc->getCallingPid();
    const int uid = ipc->getCallingUid();
    if ((uid != AID_SHELL) &&
            !PermissionCache::checkPermission(sDump, pid, uid)) {
        result.appendFormat("Permission Denial: "
                "can't dump SurfaceFlinger from pid=%d, uid=%d\n", pid, uid);
    } else {
        // Try to get the main lock, but don't insist if we can't
        // (this would indicate SF is stuck, but we want to be able to
        // print something in dumpsys).
        int retry = 3;
        while (mStateLock.tryLock()<0 && --retry>=0) {
            usleep(1000000);
        }
        const bool locked(retry >= 0);
        // MStar Android Patch Begin
        String8 target_dir;
        // MStar Android Patch End
        if (!locked) {
            result.append(
                    "SurfaceFlinger appears to be unresponsive, "
                    "dumping anyways (no locks held)\n");
        }

        bool dumpAll = true;
        size_t index = 0;
        size_t numArgs = args.size();
        if (numArgs) {
            if ((index < numArgs) &&
                    (args[index] == String16("--list"))) {
                index++;
                listLayersLocked(args, index, result);
                dumpAll = false;
            }

            if ((index < numArgs) &&
                    (args[index] == String16("--latency"))) {
                index++;
                dumpStatsLocked(args, index, result);
                dumpAll = false;
            }

            if ((index < numArgs) &&
                    (args[index] == String16("--latency-clear"))) {
                index++;
                clearStatsLocked(args, index, result);
                dumpAll = false;
            }
            // MStar Android Patch Begin
            if ((index < numArgs) &&
                (args[index] == String16("--dumpcontent"))) {
                index++;
                if (index < args.size()) {
                    target_dir = String8(args[index]);
                    index++;
                    ALOGE("      enable graphic2 buffer dump to:%s",target_dir.string());
                }
                dumpAll = true;
            }
            if ((index < numArgs) &&
                (args[index] == String16("--overscan"))) {
                index++;
                int overscan[4] = {0};
                for (int i = 0;i < 4 && index < args.size();i++) {
                    overscan[i] = atoi(String8(args[index]).string());
                    ALOGD("overscan[%d] = %d ",i,overscan[i]);
                    index++;
                }
                mLeftOverscan = overscan[0];
                mTopOverscan = overscan[1];
                mRightOverscan = overscan[2];
                mBottomOverscan = overscan[3];
                repaintEverything();
            }
            // MStar Android Patch End
        }

        if (dumpAll) {
            // MStar Android Patch Begin
            if (!target_dir.isEmpty()) {
                graphic_buffer_dump_helper::enable_dump_graphic_buffer(true, target_dir.string());
            }
            // MStar Android Patch End

            dumpAllLocked(args, index, result);

            // MStar Android Patch Begin
            if (!target_dir.isEmpty()) {
                graphic_buffer_dump_helper::enable_dump_graphic_buffer(false, NULL);
            }
            // MStar Android Patch End
        }

        if (locked) {
            mStateLock.unlock();
        }
    }
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

void SurfaceFlinger::listLayersLocked(const Vector<String16>& args, size_t& index,
        String8& result) const
{
    const LayerVector& currentLayers = mCurrentState.layersSortedByZ;
    const size_t count = currentLayers.size();
    for (size_t i=0 ; i<count ; i++) {
        const sp<Layer>& layer(currentLayers[i]);
        result.appendFormat("%s\n", layer->getName().string());
    }
}

void SurfaceFlinger::dumpStatsLocked(const Vector<String16>& args, size_t& index,
        String8& result) const
{
    String8 name;
    if (index < args.size()) {
        name = String8(args[index]);
        index++;
    }

    const nsecs_t period =
            getHwComposer().getRefreshPeriod(HWC_DISPLAY_PRIMARY);
    result.appendFormat("%lld\n", period);

    if (name.isEmpty()) {
        mAnimFrameTracker.dump(result);
    } else {
        const LayerVector& currentLayers = mCurrentState.layersSortedByZ;
        const size_t count = currentLayers.size();
        for (size_t i=0 ; i<count ; i++) {
            const sp<Layer>& layer(currentLayers[i]);
            if (name == layer->getName()) {
                layer->dumpStats(result);
            }
        }
    }
}

void SurfaceFlinger::clearStatsLocked(const Vector<String16>& args, size_t& index,
        String8& result)
{
    String8 name;
    if (index < args.size()) {
        name = String8(args[index]);
        index++;
    }

    const LayerVector& currentLayers = mCurrentState.layersSortedByZ;
    const size_t count = currentLayers.size();
    for (size_t i=0 ; i<count ; i++) {
        const sp<Layer>& layer(currentLayers[i]);
        if (name.isEmpty() || (name == layer->getName())) {
            layer->clearStats();
        }
    }

    mAnimFrameTracker.clear();
}

// This should only be called from the main thread.  Otherwise it would need
// the lock and should use mCurrentState rather than mDrawingState.
void SurfaceFlinger::logFrameStats() {
    const LayerVector& drawingLayers = mDrawingState.layersSortedByZ;
    const size_t count = drawingLayers.size();
    for (size_t i=0 ; i<count ; i++) {
        const sp<Layer>& layer(drawingLayers[i]);
        layer->logFrameStats();
    }

    mAnimFrameTracker.logAndResetStats(String8("<win-anim>"));
}

/*static*/ void SurfaceFlinger::appendSfConfigString(String8& result)
{
    static const char* config =
            " [sf"
#ifdef NO_RGBX_8888
            " NO_RGBX_8888"
#endif
#ifdef HAS_CONTEXT_PRIORITY
            " HAS_CONTEXT_PRIORITY"
#endif
#ifdef NEVER_DEFAULT_TO_ASYNC_MODE
            " NEVER_DEFAULT_TO_ASYNC_MODE"
#endif
#ifdef TARGET_DISABLE_TRIPLE_BUFFERING
            " TARGET_DISABLE_TRIPLE_BUFFERING"
#endif
            "]";
    result.append(config);
}

void SurfaceFlinger::dumpAllLocked(const Vector<String16>& args, size_t& index,
        String8& result) const
{
    bool colorize = false;
    if (index < args.size()
            && (args[index] == String16("--color"))) {
        colorize = true;
        index++;
    }

    Colorizer colorizer(colorize);

    // figure out if we're stuck somewhere
    const nsecs_t now = systemTime();
    const nsecs_t inSwapBuffers(mDebugInSwapBuffers);
    const nsecs_t inTransaction(mDebugInTransaction);
    nsecs_t inSwapBuffersDuration = (inSwapBuffers) ? now-inSwapBuffers : 0;
    nsecs_t inTransactionDuration = (inTransaction) ? now-inTransaction : 0;

    /*
     * Dump library configuration.
     */

    colorizer.bold(result);
    result.append("Build configuration:");
    colorizer.reset(result);
    appendSfConfigString(result);
    appendUiConfigString(result);
    appendGuiConfigString(result);
    result.append("\n");

    colorizer.bold(result);
    result.append("Sync configuration: ");
    colorizer.reset(result);
    result.append(SyncFeatures::getInstance().toString());
    result.append("\n");

    /*
     * Dump the visible layer list
     */
    const LayerVector& currentLayers = mCurrentState.layersSortedByZ;
    const size_t count = currentLayers.size();
    colorizer.bold(result);
    result.appendFormat("Visible layers (count = %d)\n", count);
    colorizer.reset(result);
    for (size_t i=0 ; i<count ; i++) {
        const sp<Layer>& layer(currentLayers[i]);
        layer->dump(result, colorizer);
    }

    /*
     * Dump Display state
     */

    colorizer.bold(result);
    result.appendFormat("Displays (%d entries)\n", mDisplays.size());
    colorizer.reset(result);
    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        const sp<const DisplayDevice>& hw(mDisplays[dpy]);
        hw->dump(result);
    }

    /*
     * Dump SurfaceFlinger global state
     */

    colorizer.bold(result);
    result.append("SurfaceFlinger global state:\n");
    colorizer.reset(result);

    HWComposer& hwc(getHwComposer());
    sp<const DisplayDevice> hw(getDefaultDisplayDevice());

    colorizer.bold(result);
    result.appendFormat("EGL implementation : %s\n",
            eglQueryStringImplementationANDROID(mEGLDisplay, EGL_VERSION));
    colorizer.reset(result);
    result.appendFormat("%s\n",
            eglQueryStringImplementationANDROID(mEGLDisplay, EGL_EXTENSIONS));

    mRenderEngine->dump(result);

    hw->undefinedRegion.dump(result, "undefinedRegion");
    result.appendFormat("  orientation=%d, canDraw=%d\n",
            hw->getOrientation(), hw->canDraw());
    result.appendFormat(
            "  last eglSwapBuffers() time: %f us\n"
            "  last transaction time     : %f us\n"
            "  transaction-flags         : %08x\n"
            "  refresh-rate              : %f fps\n"
            "  x-dpi                     : %f\n"
            "  y-dpi                     : %f\n"
            "  EGL_NATIVE_VISUAL_ID      : %d\n"
            "  gpu_to_cpu_unsupported    : %d\n"
            ,
            mLastSwapBufferTime/1000.0,
            mLastTransactionTime/1000.0,
            mTransactionFlags,
            1e9 / hwc.getRefreshPeriod(HWC_DISPLAY_PRIMARY),
            hwc.getDpiX(HWC_DISPLAY_PRIMARY),
            hwc.getDpiY(HWC_DISPLAY_PRIMARY),
            mEGLNativeVisualId,
            !mGpuToCpuSupported);

    result.appendFormat("  eglSwapBuffers time: %f us\n",
            inSwapBuffersDuration/1000.0);

    result.appendFormat("  transaction time: %f us\n",
            inTransactionDuration/1000.0);

    /*
     * VSYNC state
     */
    mEventThread->dump(result);

    /*
     * Dump HWComposer state
     */
    colorizer.bold(result);
    result.append("h/w composer state:\n");
    colorizer.reset(result);
    result.appendFormat("  h/w composer %s and %s\n",
            hwc.initCheck()==NO_ERROR ? "present" : "not present",
                    (mDebugDisableHWC || mDebugRegion || mDaltonize) ? "disabled" : "enabled");
    hwc.dump(result);

    /*
     * Dump gralloc state
     */
    const GraphicBufferAllocator& alloc(GraphicBufferAllocator::get());
    alloc.dump(result);
}

const Vector< sp<Layer> >&
SurfaceFlinger::getLayerSortedByZForHwcDisplay(int id) {
    // Note: mStateLock is held here
    wp<IBinder> dpy;
    for (size_t i=0 ; i<mDisplays.size() ; i++) {
        if (mDisplays.valueAt(i)->getHwcDisplayId() == id) {
            dpy = mDisplays.keyAt(i);
            break;
        }
    }
    if (dpy == NULL) {
        ALOGE("getLayerSortedByZForHwcDisplay: invalid hwc display id %d", id);
        // Just use the primary display so we have something to return
        dpy = getBuiltInDisplay(DisplayDevice::DISPLAY_PRIMARY);
    }
    return getDisplayDevice(dpy)->getVisibleLayersSortedByZ();
}

bool SurfaceFlinger::startDdmConnection()
{
    void* libddmconnection_dso =
            dlopen("libsurfaceflinger_ddmconnection.so", RTLD_NOW);
    if (!libddmconnection_dso) {
        return false;
    }
    void (*DdmConnection_start)(const char* name);
    DdmConnection_start =
            (typeof DdmConnection_start)dlsym(libddmconnection_dso, "DdmConnection_start");
    if (!DdmConnection_start) {
        dlclose(libddmconnection_dso);
        return false;
    }
    (*DdmConnection_start)(getServiceName());
    return true;
}

status_t SurfaceFlinger::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    switch (code) {
        case CREATE_CONNECTION:
        case CREATE_DISPLAY:
        case SET_TRANSACTION_STATE:
        case BOOT_FINISHED:
        case BLANK:
        case UNBLANK:
        // MStar Android Patch Begin
        case SET_AUTO_STEREO_MODE:
        case SET_BYPASS_TRANSFORM_MODE:
        case SET_PANEL_MODE:
        case GET_PANEL_MODE:
        // MStar Android Patch End
        {
            // codes that require permission check
            IPCThreadState* ipc = IPCThreadState::self();
            const int pid = ipc->getCallingPid();
            const int uid = ipc->getCallingUid();
            if ((uid != AID_GRAPHICS) &&
                    !PermissionCache::checkPermission(sAccessSurfaceFlinger, pid, uid)) {
                ALOGE("Permission Denial: "
                        "can't access SurfaceFlinger pid=%d, uid=%d", pid, uid);
                return PERMISSION_DENIED;
            }
            break;
        }
        case CAPTURE_SCREEN:
        {
            // codes that require permission check
            IPCThreadState* ipc = IPCThreadState::self();
            const int pid = ipc->getCallingPid();
            const int uid = ipc->getCallingUid();
            if ((uid != AID_GRAPHICS) &&
                    !PermissionCache::checkPermission(sReadFramebuffer, pid, uid)) {
                ALOGE("Permission Denial: "
                        "can't read framebuffer pid=%d, uid=%d", pid, uid);
                return PERMISSION_DENIED;
            }
            break;
        }
    }

    status_t err = BnSurfaceComposer::onTransact(code, data, reply, flags);
    if (err == UNKNOWN_TRANSACTION || err == PERMISSION_DENIED) {
        CHECK_INTERFACE(ISurfaceComposer, data, reply);
        if (CC_UNLIKELY(!PermissionCache::checkCallingPermission(sHardwareTest))) {
            IPCThreadState* ipc = IPCThreadState::self();
            const int pid = ipc->getCallingPid();
            const int uid = ipc->getCallingUid();
            ALOGE("Permission Denial: "
                    "can't access SurfaceFlinger pid=%d, uid=%d", pid, uid);
            return PERMISSION_DENIED;
        }
        int n;
        switch (code) {
            case 1000: // SHOW_CPU, NOT SUPPORTED ANYMORE
            case 1001: // SHOW_FPS, NOT SUPPORTED ANYMORE
                return NO_ERROR;
            case 1002:  // SHOW_UPDATES
                n = data.readInt32();
                mDebugRegion = n ? n : (mDebugRegion ? 0 : 1);
                invalidateHwcGeometry();
                repaintEverything();
                return NO_ERROR;
            case 1004:{ // repaint everything
                repaintEverything();
                return NO_ERROR;
            }
            case 1005:{ // force transaction
                setTransactionFlags(
                        eTransactionNeeded|
                        eDisplayTransactionNeeded|
                        eTraversalNeeded);
                return NO_ERROR;
            }
            case 1006:{ // send empty update
                signalRefresh();
                return NO_ERROR;
            }
            case 1008:  // toggle use of hw composer
                n = data.readInt32();
                mDebugDisableHWC = n ? 1 : 0;
                invalidateHwcGeometry();
                repaintEverything();
                return NO_ERROR;
            case 1009:  // toggle use of transform hint
                n = data.readInt32();
                mDebugDisableTransformHint = n ? 1 : 0;
                invalidateHwcGeometry();
                repaintEverything();
                return NO_ERROR;
            case 1010:  // interrogate.
                reply->writeInt32(0);
                reply->writeInt32(0);
                reply->writeInt32(mDebugRegion);
                reply->writeInt32(0);
                reply->writeInt32(mDebugDisableHWC);
                return NO_ERROR;
            case 1013: {
                Mutex::Autolock _l(mStateLock);
                sp<const DisplayDevice> hw(getDefaultDisplayDevice());
                reply->writeInt32(hw->getPageFlipCount());
                return NO_ERROR;
            }
            case 1014: {
                // daltonize
                n = data.readInt32();
                switch (n % 10) {
                    case 1: mDaltonizer.setType(Daltonizer::protanomaly);   break;
                    case 2: mDaltonizer.setType(Daltonizer::deuteranomaly); break;
                    case 3: mDaltonizer.setType(Daltonizer::tritanomaly);   break;
                }
                if (n >= 10) {
                    mDaltonizer.setMode(Daltonizer::correction);
                } else {
                    mDaltonizer.setMode(Daltonizer::simulation);
                }
                mDaltonize = n > 0;
                invalidateHwcGeometry();
                repaintEverything();
            }
            return NO_ERROR;
        }
    }
    return err;
}

void SurfaceFlinger::repaintEverything() {
    android_atomic_or(1, &mRepaintEverything);
    // MStar Android Patch Begin
    mVisibleRegionsDirty = true;
    // MStar Android Patch End
    signalTransaction();
}

// ---------------------------------------------------------------------------
// Capture screen into an IGraphiBufferProducer
// ---------------------------------------------------------------------------

/* The code below is here to handle b/8734824
 *
 * We create a IGraphicBufferProducer wrapper that forwards all calls
 * to the calling binder thread, where they are executed. This allows
 * the calling thread to be reused (on the other side) and not
 * depend on having "enough" binder threads to handle the requests.
 *
 */

class GraphicProducerWrapper : public BBinder, public MessageHandler {
    sp<IGraphicBufferProducer> impl;
    sp<Looper> looper;
    status_t result;
    bool exitPending;
    bool exitRequested;
    mutable Barrier barrier;
    volatile int32_t memoryBarrier;
    uint32_t code;
    Parcel const* data;
    Parcel* reply;

    enum {
        MSG_API_CALL,
        MSG_EXIT
    };

    /*
     * this is called by our "fake" BpGraphicBufferProducer. We package the
     * data and reply Parcel and forward them to the calling thread.
     */
    virtual status_t transact(uint32_t code,
            const Parcel& data, Parcel* reply, uint32_t flags) {
        this->code = code;
        this->data = &data;
        this->reply = reply;
        android_atomic_acquire_store(0, &memoryBarrier);
        if (exitPending) {
            // if we've exited, we run the message synchronously right here
            handleMessage(Message(MSG_API_CALL));
        } else {
            barrier.close();
            looper->sendMessage(this, Message(MSG_API_CALL));
            barrier.wait();
        }
        // MStar Android Patch Begin
        return result;
        // MStar Android Patch End

    }

    /*
     * here we run on the binder calling thread. All we've got to do is
     * call the real BpGraphicBufferProducer.
     */
    virtual void handleMessage(const Message& message) {
        android_atomic_release_load(&memoryBarrier);
        if (message.what == MSG_API_CALL) {
            // MStar Android Patch Begin
            result = impl->asBinder()->transact(code, data[0], reply);
            // MStar Android Patch End
            barrier.open();
        } else if (message.what == MSG_EXIT) {
            exitRequested = true;
        }
    }

public:
    GraphicProducerWrapper(const sp<IGraphicBufferProducer>& impl) :
        impl(impl), looper(new Looper(true)), result(NO_ERROR),
        exitPending(false), exitRequested(false) {
    }

    status_t waitForResponse() {
        do {
            looper->pollOnce(-1);
        } while (!exitRequested);
        return result;
    }

    void exit(status_t result) {
        this->result = result;
        exitPending = true;
        looper->sendMessage(this, Message(MSG_EXIT));
    }
};


status_t SurfaceFlinger::captureScreen(const sp<IBinder>& display,
        const sp<IGraphicBufferProducer>& producer,
        uint32_t reqWidth, uint32_t reqHeight,
        uint32_t minLayerZ, uint32_t maxLayerZ) {

    if (CC_UNLIKELY(display == 0))
        return BAD_VALUE;

    if (CC_UNLIKELY(producer == 0))
        return BAD_VALUE;

    // if we have secure windows on this display, never allow the screen capture
    // unless the producer interface is local (i.e.: we can take a screenshot for
    // ourselves).
    if (!producer->asBinder()->localBinder()) {
        Mutex::Autolock _l(mStateLock);
        sp<const DisplayDevice> hw(getDisplayDevice(display));
        if (hw->getSecureLayerVisible()) {
            ALOGW("FB is protected: PERMISSION_DENIED");
            return PERMISSION_DENIED;
        }
    }

    class MessageCaptureScreen : public MessageBase {
        SurfaceFlinger* flinger;
        sp<IBinder> display;
        sp<IGraphicBufferProducer> producer;
        uint32_t reqWidth, reqHeight;
        uint32_t minLayerZ,maxLayerZ;
        status_t result;
    public:
        MessageCaptureScreen(SurfaceFlinger* flinger,
                const sp<IBinder>& display,
                const sp<IGraphicBufferProducer>& producer,
                uint32_t reqWidth, uint32_t reqHeight,
                uint32_t minLayerZ, uint32_t maxLayerZ)
            : flinger(flinger), display(display), producer(producer),
              reqWidth(reqWidth), reqHeight(reqHeight),
              minLayerZ(minLayerZ), maxLayerZ(maxLayerZ),
              result(PERMISSION_DENIED)
        {
        }
        status_t getResult() const {
            return result;
        }
        virtual bool handler() {
            Mutex::Autolock _l(flinger->mStateLock);
            sp<const DisplayDevice> hw(flinger->getDisplayDevice(display));
            result = flinger->captureScreenImplLocked(hw,
                    producer, reqWidth, reqHeight, minLayerZ, maxLayerZ);
            static_cast<GraphicProducerWrapper*>(producer->asBinder().get())->exit(result);
            return true;
        }
    };

    // make sure to process transactions before screenshots -- a transaction
    // might already be pending but scheduled for VSYNC; this guarantees we
    // will handle it before the screenshot. When VSYNC finally arrives
    // the scheduled transaction will be a no-op. If no transactions are
    // scheduled at this time, this will end-up being a no-op as well.
    mEventQueue.invalidateTransactionNow();

    // this creates a "fake" BBinder which will serve as a "fake" remote
    // binder to receive the marshaled calls and forward them to the
    // real remote (a BpGraphicBufferProducer)
    sp<GraphicProducerWrapper> wrapper = new GraphicProducerWrapper(producer);

    // the asInterface() call below creates our "fake" BpGraphicBufferProducer
    // which does the marshaling work forwards to our "fake remote" above.
    sp<MessageBase> msg = new MessageCaptureScreen(this,
            display, IGraphicBufferProducer::asInterface( wrapper ),
            reqWidth, reqHeight, minLayerZ, maxLayerZ);

    status_t res = postMessageAsync(msg);
    if (res == NO_ERROR) {
        res = wrapper->waitForResponse();
    }
    return res;
}

// MStar Android Patch Begin
void SurfaceFlinger::renderScreenImplLocked(
        const sp<const DisplayDevice>& hw,
        uint32_t reqWidth, uint32_t reqHeight,
        uint32_t minLayerZ, uint32_t maxLayerZ,
        bool yswap, uint32_t supportWidth, uint32_t supportHeight)
{
// MStar Android Patch End

    ATRACE_CALL();
    RenderEngine& engine(getRenderEngine());

    // get screen geometry
    // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
    const uint32_t hw_w = hw->getOsdWidthForAndroid();
    const uint32_t hw_h = hw->getOsdHeightForAndroid();
#else
    const uint32_t hw_w = hw->getWidth();
    const uint32_t hw_h = hw->getHeight();
#endif
    // MStar Android Patch End
    const bool filtering = reqWidth != hw_w || reqWidth != hw_h;

    // make sure to clear all GL error flags
    engine.checkErrors();

    // set-up our viewport
    engine.setViewportAndProjection(reqWidth, reqHeight, hw_w, hw_h, yswap);
    engine.disableTexturing();

    // redraw the screen entirely...
    engine.clearWithColor(0, 0, 0, 1);

    const LayerVector& layers( mDrawingState.layersSortedByZ );
    const size_t count = layers.size();
    for (size_t i=0 ; i<count ; ++i) {
        const sp<Layer>& layer(layers[i]);
        const Layer::State& state(layer->getDrawingState());
        if (state.layerStack == hw->getLayerStack()) {
            if (state.z >= minLayerZ && state.z <= maxLayerZ) {
                if (layer->isVisible()) {
                    if (filtering) layer->setFiltering(true);
                    // MStar Android Patch Begin
                    if (layer->isOverlay()) {
                        if (mCaptureData!=NULL) {
                            uint32_t cameraCaptureTexture = engine.initCaptureData(mCaptureData,supportWidth,supportHeight);
                            layer->drawCaptureData(hw,Texture(Texture::TEXTURE_2D, cameraCaptureTexture));
                            engine.releaseCaptureData(cameraCaptureTexture);
                            mCaptureData.clear();
                            mCaptureData = NULL;
                        } else {
                            Region rgn;
                            layer->clearWithOpenGL(hw, rgn);
                        }
                    } else {
                        layer->draw(hw);
                    }
                    // MStar Android Patch End
                    if (filtering) layer->setFiltering(false);
                }
            }
        }
    }

    // compositionComplete is needed for older driver
    hw->compositionComplete();
    hw->setViewportAndProjection();
}

// MStar Android Patch Begin
static EN_TRAVELING_RESOLUTION getTravelingRes(int *width, int *height) {
    if (*width == 720 && *height == 480) {
        return E_TRAVELING_RES_720_480;
    } else if (*width == 720 && *height == 576) {
        return E_TRAVELING_RES_720_576;
    } else if (*width == 1280 && *height == 720) {
        return E_TRAVELING_RES_1280_720;
    }else {
        *width = 1920;
        *height = 1080;
        return E_TRAVELING_RES_1920_1080;
    }
}

status_t SurfaceFlinger::captureScreenImplLocked(
        const sp<const DisplayDevice>& hw,
        const sp<IGraphicBufferProducer>& producer,
        uint32_t reqWidth, uint32_t reqHeight,
        uint32_t minLayerZ, uint32_t maxLayerZ)
{
    ATRACE_CALL();
    // MStar Android Patch Begin
    char value[PROPERTY_VALUE_MAX];
    bool mEnable4k2k = false;
    int supportWidth = SCREENCAP_WIDTH;
    int supportHeight = SCREENCAP_HEIGHT;
    // get screen geometry
#ifdef ENABLE_HWCOMPOSER_13
    const uint32_t hw_w = hw->getOsdWidthForAndroid();
    const uint32_t hw_h = hw->getOsdHeightForAndroid();
#else
    const uint32_t hw_w = hw->getOsdWidth();
    const uint32_t hw_h = hw->getOsdHeight();
#endif
    // MStar Android Patch End

    if ((reqWidth > hw_w) || (reqHeight > hw_h)) {
        ALOGE("size mismatch (%d, %d) > (%d, %d)",
                reqWidth, reqHeight, hw_w, hw_h);
        return BAD_VALUE;
    }

    reqWidth  = (!reqWidth)  ? hw_w : reqWidth;
    reqHeight = (!reqHeight) ? hw_h : reqHeight;

    // MStar Android Patch Begin
    property_get("mstar.resolution.4k2kEnableUI", value, "0");
    if (atoi(value) == 1) {
        mEnable4k2k = true;
    }
#ifdef BUILD_FOR_STB
    supportWidth = hw->getRealDisplayWidth();
    supportHeight = hw->getRealDisplayHeight();
    if (mHwc) {
        mHwc->getCurOPTiming(&supportWidth, &supportHeight);
    }
#else
     if (mHwc) {
        mHwc->getCurOPTiming(&supportWidth, &supportHeight);
    }
#endif
    ALOGD("captureScreen: supportWidth = %d, supportHeight = %d, maxLayerZ = %d", supportWidth, supportHeight, maxLayerZ);
    if (mCaptureData != NULL) {
        mCaptureData.clear();
        mCaptureData = NULL;
    }
    if (maxLayerZ == -1UL) {
#if defined(MSTAR_MADISON) || defined(MSTAR_CLIPPERS)
        sp<Camera> mCamera = Camera::connect(CAMERA_ID_XCDIP0, String16("SurfaceFlinger"), -1);
#elif defined(MSTAR_MONACO)
        sp<Camera> mCamera = Camera::connect(CAMERA_ID_XCDIP1, String16("SurfaceFlinger"), -1);
#else
        sp<Camera> mCamera;
        if (mEnable4k2k) {
            mCamera = Camera::connect(CAMERA_ID_XCDIP1, String16("SurfaceFlinger"), -1);
        } else {
            mCamera = Camera::connect(CAMERA_ID_XCDIP2, String16("SurfaceFlinger"), -1);
        }
#endif
        if (mCamera == NULL) {
            ALOGD("captureScreen: camera connect fail");
            //return UNKNOWN_ERROR;
            goto CAPTURE_BYCAMERA_DONE;
        }

        mCamera->setListener(new ScreenCaptureListener(this));
        CameraParameters mCameraParameters(mCamera->getParameters());
        mCameraParameters.set("traveling-res", getTravelingRes(&supportWidth, &supportHeight));
        mCameraParameters.set("traveling-mode", E_TRAVELING_ALL_VIDEO);
        mCameraParameters.set("traveling-mem-format", E_TRAVELING_MEM_FORMAT_ABGR8888);
        mCameraParameters.set("traveling-frame-rate", 0);
        mCamera->setParameters(mCameraParameters.flatten());
        mCamera->setPreviewCallbackFlags(CAMERA_FRAME_CALLBACK_FLAG_ENABLE_MASK|CAMERA_FRAME_CALLBACK_FLAG_ONE_SHOT_MASK );
        status_t ret = mCamera->startPreview();
        if (ret != OK) {
            ALOGD("captureScreen: camera startprview fail\n");
            mCamera->setListener(NULL);
            mCamera->disconnect();
            return UNKNOWN_ERROR;
        }

        {
            Mutex::Autolock _l(mFrameAvailableLock);
            if (mCaptureData == NULL) {
                status_t err = mFrameAvailableCV.waitRelative(mFrameAvailableLock, 1000*1000000);
                if (NO_ERROR != err) {
                    ALOGD("captureScreen: wait camera raw data timeout(1000ms)\n");
                }
            }
            if (mCaptureData != NULL) {
                ALOGD("captureScreen: get mCaptureData success !!!!!!!!!!!!!!!!!");
            }
        }

        mCamera->setListener(NULL);
        mCamera->stopPreview();
        mCamera->disconnect();
    }
CAPTURE_BYCAMERA_DONE:
    // MStar Android Patch End

    // create a surface (because we're a producer, and we need to
    // dequeue/queue a buffer)
    sp<Surface> sur = new Surface(producer, false);
    ANativeWindow* window = sur.get();
    // MStar Android Patch Begin
    if (window == NULL) {
        ALOGE("NO_MEMORY");
        return NO_MEMORY;
    }
    // MStar Android Patch End
    status_t result = NO_ERROR;
    if (native_window_api_connect(window, NATIVE_WINDOW_API_EGL) == NO_ERROR) {
        uint32_t usage = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN |
                        GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;

        int err = 0;
        err = native_window_set_buffers_dimensions(window, reqWidth, reqHeight);
        err |= native_window_set_scaling_mode(window, NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
        err |= native_window_set_buffers_format(window, HAL_PIXEL_FORMAT_RGBA_8888);
        err |= native_window_set_usage(window, usage);

        if (err == NO_ERROR) {
            ANativeWindowBuffer* buffer;
            /* TODO: Once we have the sync framework everywhere this can use
             * server-side waits on the fence that dequeueBuffer returns.
             */
            result = native_window_dequeue_buffer_and_wait(window,  &buffer);
            // MStar Android Patch Begin
            if ((result == NO_ERROR)&&(buffer!=NULL)) {
            // MStar Android Patch End
                // create an EGLImage from the buffer so we can later
                // turn it into a texture
                EGLImageKHR image = eglCreateImageKHR(mEGLDisplay, EGL_NO_CONTEXT,
                        EGL_NATIVE_BUFFER_ANDROID, buffer, NULL);
                if (image != EGL_NO_IMAGE_KHR) {
                    // this binds the given EGLImage as a framebuffer for the
                    // duration of this scope.
                    RenderEngine::BindImageAsFramebuffer imageBond(getRenderEngine(), image);
                    if (imageBond.getStatus() == NO_ERROR) {
                        // this will in fact render into our dequeued buffer
                        // via an FBO, which means we didn't have to create
                        // an EGLSurface and therefore we're not
                        // dependent on the context's EGLConfig.
                        // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
                        DisplayDevice* temphw = (DisplayDevice*)hw.get();
                        temphw->setOverscanAnd3DEnabled(false);
                        renderScreenImplLocked(hw, reqWidth, reqHeight,
                                minLayerZ, maxLayerZ, true, supportWidth
                                , supportHeight);
                        temphw->setOverscanAnd3DEnabled(true);
#else
                        DisplayDevice* temphw = (DisplayDevice*)hw.get();
                        if (m4k2kAnd2k1kCoexistEnable && mChangeTo2k1kMode) {
                            temphw->changeGlobalTransform(true);
                        }
#ifdef BUILD_FOR_STB
                        Transform originalTransform, identityTransform;
                        if (m4k2kAnd2k1kCoexistEnable) {
                            identityTransform.set(2.0f,0,0,2.0f);
                        }
                        originalTransform = temphw->getTransform();
                        temphw->setTransform(identityTransform);
#endif
                        renderScreenImplLocked(hw, reqWidth, reqHeight,
                                minLayerZ, maxLayerZ, true, supportWidth
                                , supportHeight);
#ifdef BUILD_FOR_STB
                        temphw->setTransform(originalTransform);
#endif
                        if (m4k2kAnd2k1kCoexistEnable && mChangeTo2k1kMode) {
                            temphw->changeGlobalTransform(false);
                        }
#endif
                        // MStar Android Patch End

                        // Create a sync point and wait on it, so we know the buffer is
                        // ready before we pass it along.  We can't trivially call glFlush(),
                        // so we use a wait flag instead.
                        // TODO: pass a sync fd to queueBuffer() and let the consumer wait.
                        EGLSyncKHR sync = eglCreateSyncKHR(mEGLDisplay, EGL_SYNC_FENCE_KHR, NULL);
                        if (sync != EGL_NO_SYNC_KHR) {
                            EGLint result = eglClientWaitSyncKHR(mEGLDisplay, sync,
                                    EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, 2000000000 /*2 sec*/);
                            EGLint eglErr = eglGetError();
                            eglDestroySyncKHR(mEGLDisplay, sync);
                            if (result == EGL_TIMEOUT_EXPIRED_KHR) {
                                ALOGW("captureScreen: fence wait timed out");
                            } else {
                                ALOGW_IF(eglErr != EGL_SUCCESS,
                                        "captureScreen: error waiting on EGL fence: %#x", eglErr);
                            }
                        } else {
                            ALOGW("captureScreen: error creating EGL fence: %#x", eglGetError());
                            // not fatal
                        }

                        if (DEBUG_SCREENSHOTS) {
                            uint32_t* pixels = new uint32_t[reqWidth*reqHeight];
                            getRenderEngine().readPixels(0, 0, reqWidth, reqHeight, pixels);
                            checkScreenshot(reqWidth, reqHeight, reqWidth, pixels,
                                    hw, minLayerZ, maxLayerZ);
                            delete [] pixels;
                        }

                    } else {
                        ALOGE("got GL_FRAMEBUFFER_COMPLETE_OES error while taking screenshot");
                        result = INVALID_OPERATION;
                    }
                    // destroy our image
                    eglDestroyImageKHR(mEGLDisplay, image);
                } else {
                    result = BAD_VALUE;
                }
                window->queueBuffer(window, buffer, -1);
            }
        } else {
            result = BAD_VALUE;
        }
        native_window_api_disconnect(window, NATIVE_WINDOW_API_EGL);
    }

    return result;
}

void SurfaceFlinger::checkScreenshot(size_t w, size_t s, size_t h, void const* vaddr,
        const sp<const DisplayDevice>& hw, uint32_t minLayerZ, uint32_t maxLayerZ) {
    if (DEBUG_SCREENSHOTS) {
        for (size_t y=0 ; y<h ; y++) {
            uint32_t const * p = (uint32_t const *)vaddr + y*s;
            for (size_t x=0 ; x<w ; x++) {
                if (p[x] != 0xFF000000) return;
            }
        }
        ALOGE("*** we just took a black screenshot ***\n"
                "requested minz=%d, maxz=%d, layerStack=%d",
                minLayerZ, maxLayerZ, hw->getLayerStack());
        const LayerVector& layers( mDrawingState.layersSortedByZ );
        const size_t count = layers.size();
        for (size_t i=0 ; i<count ; ++i) {
            const sp<Layer>& layer(layers[i]);
            const Layer::State& state(layer->getDrawingState());
            const bool visible = (state.layerStack == hw->getLayerStack())
                                && (state.z >= minLayerZ && state.z <= maxLayerZ)
                                && (layer->isVisible());
            ALOGE("%c index=%d, name=%s, layerStack=%d, z=%d, visible=%d, flags=%x, alpha=%x",
                    visible ? '+' : '-',
                            i, layer->getName().string(), state.layerStack, state.z,
                            layer->isVisible(), state.flags, state.alpha);
        }
    }
}

// MStar Android Patch Begin
status_t SurfaceFlinger::setAutoStereoMode(int32_t identity, int32_t autoStereo) {
    Mutex::Autolock _l(mStateLock);
    const LayerVector& layers(mCurrentState.layersSortedByZ);
    size_t count = layers.size();

    int mIdentity;

    for (size_t j = 0; j<count; j++) {
        const sp<Layer>& layer(layers[j]);
        mIdentity = (int)(layer->getIdentity());

        if (identity == mIdentity) {
            ALOGD("SurfaceFlinger::setAutoStereoMode,autoStereo=%d", autoStereo);
            layer->setAutoStereo(autoStereo);
            break;
        }
    }
    return NO_ERROR;
}

status_t SurfaceFlinger::setBypassTransformMode(int32_t identity, int32_t bypassTransform) {
    return NO_ERROR;
}

status_t SurfaceFlinger::setPanelMode(int32_t panelMode) {
    return NO_ERROR;
}

status_t SurfaceFlinger::getPanelMode(int32_t *panelMode) {
    return NO_ERROR;
}

status_t SurfaceFlinger::forcePostSecFb(int secFbId) {
    return NO_ERROR;
}

status_t SurfaceFlinger::getSecFbBuffer(sp<GraphicBuffer>* buf, int idx) {
    return NO_ERROR;
}

status_t SurfaceFlinger::setGopStretchWin(int32_t gopNo, int32_t dest_Width, int32_t dest_Height) {
#ifdef ENABLE_HWCOMPOSER_13
    return BAD_VALUE;
#else
    return mHwc->setGopStretchWin(gopNo, dest_Width, dest_Height);
#endif
}

status_t SurfaceFlinger::setSurfaceResolutionMode(int32_t width, int32_t height, int32_t hstart, int32_t interleave, int32_t orientation, int32_t value) {
#ifdef ENABLE_HWCOMPOSER_13
    {
        Mutex::Autolock _l(mOverscanLock);
        mTopOverscan    = (value>>24)&0xff;
        mBottomOverscan = (value>>16)&0xff;
        mLeftOverscan   = (value>>8)&0xff;
        mRightOverscan  = (value)&0xff;
    }
    //ALOGI("Overscan binder thread: left:%d top:%d right:%d bottom:%d", mLeftOverscan, mTopOverscan, mRightOverscan, mBottomOverscan);
    // need dirty region invalid geometry?
    repaintEverything();
#else
#ifdef BUILD_FOR_STB
    //only process reproduce rate
    if (orientation > 0) {
        ALOGI("SurfaceFlinger::setSurfaceResolutionMode was invoked! orientation=%d,value=%d",orientation,value);
        const sp<DisplayDevice>& hw(mDisplays[DisplayDevice::DISPLAY_PRIMARY]);
        mHwc->setReproduceRate(value);
        hw->setDisplayWindow();
    }
#endif
#endif
    return NO_ERROR;
}
// MStar Android Patch End

// ---------------------------------------------------------------------------

SurfaceFlinger::LayerVector::LayerVector() {
}

SurfaceFlinger::LayerVector::LayerVector(const LayerVector& rhs)
    : SortedVector<sp<Layer> >(rhs) {
}

int SurfaceFlinger::LayerVector::do_compare(const void* lhs,
    const void* rhs) const
{
    // sort layers per layer-stack, then by z-order and finally by sequence
    const sp<Layer>& l(*reinterpret_cast<const sp<Layer>*>(lhs));
    const sp<Layer>& r(*reinterpret_cast<const sp<Layer>*>(rhs));

    uint32_t ls = l->getCurrentState().layerStack;
    uint32_t rs = r->getCurrentState().layerStack;
    if (ls != rs)
        return ls - rs;

    uint32_t lz = l->getCurrentState().z;
    uint32_t rz = r->getCurrentState().z;
    if (lz != rz)
        return lz - rz;

    return l->sequence - r->sequence;
}

// ---------------------------------------------------------------------------

SurfaceFlinger::DisplayDeviceState::DisplayDeviceState()
    : type(DisplayDevice::DISPLAY_ID_INVALID) {
}

SurfaceFlinger::DisplayDeviceState::DisplayDeviceState(DisplayDevice::DisplayType type)
    : type(type), layerStack(DisplayDevice::NO_LAYER_STACK), orientation(0) {
    viewport.makeInvalid();
    frame.makeInvalid();
}

// ---------------------------------------------------------------------------

// MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
int SurfaceFlinger::OnVSyncEvent() {
    //maybe HWCOMPOSER13 no need it
    return -1;
}

SurfaceFlinger::VSyncMessageMonitor::VSyncMessageMonitor() {
    //maybe HWCOMPOSER13 no need it
}

void SurfaceFlinger::VSyncMessageMonitor::Init(sp < SurfaceFlinger > surFlinger, const sp < EventThread > & eventThread) {
    //maybe HWCOMPOSER13 no need it
}

int SurfaceFlinger::VSyncMessageMonitor::cb_eventReceiver(int fd, int events, void * data) {
    //maybe HWCOMPOSER13 no need it
    return -1;
}

int SurfaceFlinger::VSyncMessageMonitor::eventReceiver(int fd, int events) {
    //maybe HWCOMPOSER13 no need it
    return -1;
}

bool SurfaceFlinger::VSyncMessageMonitor::threadLoop() {
    //maybe HWCOMPOSER13 no need it
    return true;
}

status_t SurfaceFlinger::VSyncMessageMonitor::readyToRun() {
    //maybe HWCOMPOSER13 no need it
    return NO_INIT;
}
#else
int SurfaceFlinger::OnVSyncEvent() {
    static int preDisplayMode = DISPLAYMODE_NORMAL;
    static int pretimingwidth = 0;
    static int pretimingheight = 0;
    char value[PROPERTY_VALUE_MAX] = {0};
    int displayMode = DISPLAYMODE_NORMAL;
    bool needRepaint = false;
    int  curTimingWidth = 0;
    int  curTimingHeight = 0;
    if (property_get(MSTAR_DESK_DISPLAY_MODE, value, NULL) > 0) {
        displayMode = atoi(value);
    }
    if (displayMode < 0 || displayMode >= DISPLAYMODE_MAX) {
        displayMode = DISPLAYMODE_NORMAL;
        ALOGI("The property mstar.desk-display-mode is incorrect that it will reset to default value");
    }
    if (displayMode != preDisplayMode) {
        ALOGI("Johnny SurfaceFlinger::OnVSyncEvent() fire displayMode=%d,preDisplayMode=%d",displayMode,preDisplayMode);
        preDisplayMode = displayMode;
        needRepaint = true;
    }
    //timming change
    if (mHwc) {
        mHwc->getCurOPTiming(&curTimingWidth, &curTimingHeight);
        if ((curTimingWidth>0)&&(curTimingHeight>0)) {
            if ((pretimingwidth!=curTimingWidth)||(pretimingheight!=curTimingHeight)) {
               needRepaint = true;
               pretimingwidth = curTimingWidth;
               pretimingheight = curTimingHeight;
#ifdef BUILD_FOR_STB
               //Triger hwcomposer when HDMI timming changed
               const sp<DisplayDevice>& hw(mDisplays[DisplayDevice::DISPLAY_PRIMARY]);
               hw->reSetPanelSize(curTimingWidth, curTimingHeight);
#endif
               ALOGD("timing changed need to repaint -----------!!!!");
            }
        }
     }
// resolution change
#ifdef BUILD_FOR_STB
    memset(value, 0, PROPERTY_VALUE_MAX);
    if (property_get("mstar.resolutionState", value, NULL) > 0) {
        const sp<DisplayDevice>& hw(mDisplays[DisplayDevice::DISPLAY_PRIMARY]);
        if (hw->getCurResState() != value) {
            int panelWidth, panelHeight;
            bool resolutionChanged = false;
            hw->getStbResolution(&panelWidth, &panelHeight, &resolutionChanged);
            ALOGI("STB resolution change need repaint panelWidth=%d,panelHeight=%d!",panelWidth,panelHeight);
            //hw->reSetPanelSize(panelWidth,panelHeight);
            hw->setRealResolute(panelWidth, panelHeight);
            hw->setDisplayWindow();
            hw->setCurResState(value);
#ifdef ENABLE_HWCURSOR
            changeHwCursorResolution();
#endif
            needRepaint  = true;
        }
    }
#endif
    if (needRepaint) {
        invalidateHwcGeometry();
        repaintEverything();
    }
    return 0;
}

SurfaceFlinger::VSyncMessageMonitor::VSyncMessageMonitor() {
}

void SurfaceFlinger::VSyncMessageMonitor::Init(sp < SurfaceFlinger > surFlinger, const sp < EventThread > & eventThread) {
    mLooper = new Looper(true);
    mFlinger = surFlinger;
    mVSyncEvent = eventThread->createEventConnection();
    mVSyncEvent->setVsyncRate(1);
    mmVSyncEventTube = mVSyncEvent->getDataChannel();
    mLooper->addFd(mmVSyncEventTube->getFd(), 0, ALOOPER_EVENT_INPUT,
                          cb_eventReceiver, this);
    run("VSyncMessageMonitor");
}

int SurfaceFlinger::VSyncMessageMonitor::cb_eventReceiver(int fd, int events, void * data) {
    VSyncMessageMonitor* monitor = reinterpret_cast<VSyncMessageMonitor *>(data);
    return monitor->eventReceiver(fd, events);
}

int SurfaceFlinger::VSyncMessageMonitor::eventReceiver(int fd, int events) {
    ssize_t n;
    DisplayEventReceiver::Event buffer[8];
    while ((n = DisplayEventReceiver::getEvents(mmVSyncEventTube, buffer, 8)) > 0) {
        for (int i=0 ; i<n ; i++) {
            if (buffer[i].header.type == DisplayEventReceiver::DISPLAY_EVENT_VSYNC) {
                mFlinger->OnVSyncEvent();
                break;
            }
        }
    }
    return 1;
}

bool SurfaceFlinger::VSyncMessageMonitor::threadLoop() {
    do {
        IPCThreadState::self()->flushCommands();
        int32_t ret = mLooper->pollOnce(-1);
        switch (ret) {
            case ALOOPER_POLL_WAKE:
            case ALOOPER_POLL_CALLBACK:
                continue;
            case ALOOPER_POLL_ERROR:
                ALOGE("ALOOPER_POLL_ERROR");
            case ALOOPER_POLL_TIMEOUT:
                // timeout (should not happen)
                continue;
            default:
                // should not happen
                ALOGE("Looper::pollOnce() returned unknown status %d", ret);
                continue;
        }
    } while (true);
    return true;
}

status_t SurfaceFlinger::VSyncMessageMonitor::readyToRun() {
    if (mLooper != NULL) {
        return NO_ERROR;
    }
    return NO_INIT;
}
#endif

#ifdef ENABLE_HWCURSOR
int SurfaceFlinger::setHwCursorShow() {
    if (mHwc) {
        return mHwc->hwCursorShow();
    }
    return 0;
}

status_t SurfaceFlinger::setHwCursorHide() {
    if (mHwc) {
        return mHwc->hwCursorHide();
    }
    return 0;
}

status_t SurfaceFlinger::setHwCursorMatrix(float dsdx, float dtdx, float dsdy, float dtdy) {
    if (mHwc) {
        return mHwc->hwCursorSetMatrix(dsdx, dtdx, dsdy, dtdy);
    }
    return 0;
}

status_t SurfaceFlinger::setHwCursorPosition(float positionX, float positionY, float hotSpotX, float hotSpotY, int iconWidth, int iconHeight) {
    if (mHwc) {
        return mHwc->hwCursorSetPosition(positionX, positionY, hotSpotX, hotSpotY, iconWidth, iconHeight);
    }
    return 0;
}

status_t SurfaceFlinger::setHwCursorAlpha(float alpha) {
    if (mHwc) {
        return mHwc->hwCursorSetAlpha(alpha);
    }
    return 0;
}

status_t SurfaceFlinger::changeHwCursorIcon() {
    if (mHwc) {
        //copy Icon data
        void* data = mHwcursorIcon->pointer();
        void* vaddr = mHwc->hwCursorGetIconVaddr();
        unsigned int sharedBufferSize = HWCURSOR_WIDTH * HWCURSOR_HEIGHT * 4;
        if (vaddr != NULL && data != NULL) {
            memcpy(vaddr, data, sharedBufferSize);
        }
        return mHwc->hwCursorChangeIcon();
    }
    return 0;
}

status_t SurfaceFlinger::doHwCursorTransaction() {
    if (mHwc) {
        return mHwc->hwCursorDoTransaction();
    }
    return 0;
}

sp<IMemory>  SurfaceFlinger::getHwCursorIconBuf() {
    return mHwcursorIcon;
}

status_t SurfaceFlinger::getHwCursorWidth(int32_t * cursorWidth) {
    if (mHwc) {
        *cursorWidth = mHwc->hwCursorGetWidth();
    }
    return 0;
}

status_t SurfaceFlinger::getHwCursorHeight(int32_t * cursorHeight) {
    if (mHwc) {
        *cursorHeight = mHwc->hwCursorGetHeight();
    }
    return 0;
}

status_t SurfaceFlinger::getHwCursorStride(int32_t * cursorStride) {
    if (mHwc) {
        *cursorStride = mHwc->hwCursorGetStride();
    }
    return 0;
}

status_t SurfaceFlinger::changeHwCursorResolution() {
    if (mHwc) {
        ALOGD("Enter hwCursorChangeResolution");
        return mHwc->hwCursorChangeResolution();
    }
    return 0;
}

status_t SurfaceFlinger::loadHwCursorModule() {
    if (mHwc) {
        return mHwc->loadHwCursorModule();
    }
    return 0;
}
#endif

void SurfaceFlinger::getScaleParamByCurrentResolutionMode(float *hScale, float *vScale) const {
    *hScale = 1.0f;
    *vScale = 1.0f;
    if (m4k2kAnd2k1kCoexistEnable) {
        if (mChangeTo2k1kMode) {
            *hScale = 0.5f;
            *vScale = 0.5f;
        } else {
            *hScale  =1.0f;
            *vScale  = 1.0f;
        }
    }
}
// MStar Android Patch End

}; // namespace android


#if defined(__gl_h_)
#error "don't include gl/gl.h in this file"
#endif

#if defined(__gl2_h_)
#error "don't include gl2/gl2.h in this file"
#endif
