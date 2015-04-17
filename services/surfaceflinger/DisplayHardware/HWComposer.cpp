/*
 * Copyright (C) 2010 The Android Open Source Project
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <math.h>

#include <utils/CallStack.h>
#include <utils/Errors.h>
#include <utils/misc.h>
#include <utils/String8.h>
#include <utils/Thread.h>
#include <utils/Trace.h>
#include <utils/Vector.h>

#include <ui/GraphicBuffer.h>

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>

#include <android/configuration.h>

#include <cutils/log.h>
#include <cutils/properties.h>

#include "HWComposer.h"

#include "../Layer.h"           // needed only for debugging
#include "../SurfaceFlinger.h"

namespace android {

#define MIN_HWC_HEADER_VERSION HWC_HEADER_VERSION

static uint32_t hwcApiVersion(const hwc_composer_device_1_t* hwc) {
    uint32_t hwcVersion = hwc->common.version;
    return hwcVersion & HARDWARE_API_VERSION_2_MAJ_MIN_MASK;
}

static uint32_t hwcHeaderVersion(const hwc_composer_device_1_t* hwc) {
    uint32_t hwcVersion = hwc->common.version;
    return hwcVersion & HARDWARE_API_VERSION_2_HEADER_MASK;
}

static bool hwcHasApiVersion(const hwc_composer_device_1_t* hwc,
        uint32_t version) {
    return hwcApiVersion(hwc) >= (version & HARDWARE_API_VERSION_2_MAJ_MIN_MASK);
}

// ---------------------------------------------------------------------------

struct HWComposer::cb_context {
    struct callbacks : public hwc_procs_t {
        // these are here to facilitate the transition when adding
        // new callbacks (an implementation can check for NULL before
        // calling a new callback).
        void (*zero[4])(void);
    };
    callbacks procs;
    HWComposer* hwc;
};

// ---------------------------------------------------------------------------

HWComposer::HWComposer(
        const sp<SurfaceFlinger>& flinger,
        EventHandler& handler)
    : mFlinger(flinger),
      mFbDev(0), mHwc(0), mNumDisplays(1),
      mCBContext(new cb_context),
      mEventHandler(handler),
      mDebugForceFakeVSync(false)
{
    for (size_t i =0 ; i<MAX_HWC_DISPLAYS ; i++) {
        mLists[i] = 0;
    }

    for (size_t i=0 ; i<HWC_NUM_PHYSICAL_DISPLAY_TYPES ; i++) {
        mLastHwVSync[i] = 0;
        mVSyncCounts[i] = 0;
    }

    char value[PROPERTY_VALUE_MAX];
    property_get("debug.sf.no_hw_vsync", value, "0");
    mDebugForceFakeVSync = atoi(value);

    bool needVSyncThread = true;

    // MStar Android Patch Begin
    char property[PROPERTY_VALUE_MAX];
    mSwitchTimingEnable = false;
    if (property_get("mstar.4k2k.2k1k.coexist", property, "false") > 0) {
        if (strcmp(property, "true") == 0) {
            if (property_get("mstar.switch.timing.enable", property, "true") > 0) {
                if (strcmp(property, "true") == 0) {
                    mSwitchTimingEnable = true;
                }
            }
        }
    }
    // MStar Android Patch End

    // Note: some devices may insist that the FB HAL be opened before HWC.
    int fberr = loadFbHalModule();
    loadHwcModule();

    if (mFbDev && mHwc && hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1)) {
        // close FB HAL if we don't needed it.
        // FIXME: this is temporary until we're not forced to open FB HAL
        // before HWC.
        framebuffer_close(mFbDev);
        mFbDev = NULL;
    }

    // If we have no HWC, or a pre-1.1 HWC, an FB dev is mandatory.
    if ((!mHwc || !hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1))
            && !mFbDev) {
        ALOGE("ERROR: failed to open framebuffer (%s), aborting",
                strerror(-fberr));
        abort();
    }

    // these display IDs are always reserved
    for (size_t i=0 ; i<NUM_BUILTIN_DISPLAYS ; i++) {
        mAllocatedDisplayIDs.markBit(i);
    }

    if (mHwc) {
        ALOGI("Using %s version %u.%u", HWC_HARDWARE_COMPOSER,
              (hwcApiVersion(mHwc) >> 24) & 0xff,
              (hwcApiVersion(mHwc) >> 16) & 0xff);
        if (mHwc->registerProcs) {
            mCBContext->hwc = this;
            mCBContext->procs.invalidate = &hook_invalidate;
            mCBContext->procs.vsync = &hook_vsync;
            if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1))
                mCBContext->procs.hotplug = &hook_hotplug;
            else
                mCBContext->procs.hotplug = NULL;
            memset(mCBContext->procs.zero, 0, sizeof(mCBContext->procs.zero));
            mHwc->registerProcs(mHwc, &mCBContext->procs);
        }

        // don't need a vsync thread if we have a hardware composer
        // MStar Android Patch Begin
        needVSyncThread = false;
        // MStar Android Patch End
        // always turn vsync off when we start
        eventControl(HWC_DISPLAY_PRIMARY, HWC_EVENT_VSYNC, 0);

        // the number of displays we actually have depends on the
        // hw composer version
        if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_3)) {
            // 1.3 adds support for virtual displays
            mNumDisplays = MAX_HWC_DISPLAYS;
        } else if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1)) {
            // 1.1 adds support for multiple displays
            mNumDisplays = NUM_BUILTIN_DISPLAYS;
        } else {
            mNumDisplays = 1;
        }
    }

    if (mFbDev) {
        ALOG_ASSERT(!(mHwc && hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1)),
                "should only have fbdev if no hwc or hwc is 1.0");

        DisplayData& disp(mDisplayData[HWC_DISPLAY_PRIMARY]);
        disp.connected = true;
        disp.width = mFbDev->width;
        disp.height = mFbDev->height;
        disp.format = mFbDev->format;
        disp.xdpi = mFbDev->xdpi;
        disp.ydpi = mFbDev->ydpi;
        // MStar Android Patch Begin
        disp.osdWidth= mFbDev->osdWidth;
        disp.osdHeight = mFbDev->osdHeight;
        // MStar Android Patch End
        if (disp.refresh == 0) {
            disp.refresh = nsecs_t(1e9 / mFbDev->fps);
            ALOGW("getting VSYNC period from fb HAL: %lld", disp.refresh);
        }
        if (disp.refresh == 0) {
            disp.refresh = nsecs_t(1e9 / 60.0);
            ALOGW("getting VSYNC period from thin air: %lld",
                    mDisplayData[HWC_DISPLAY_PRIMARY].refresh);
        }
    } else if (mHwc) {
        // here we're guaranteed to have at least HWC 1.1
        for (size_t i =0 ; i<NUM_BUILTIN_DISPLAYS ; i++) {
            queryDisplayProperties(i);
        }
    }

    if (needVSyncThread) {
        // we don't have VSYNC support, we need to fake it
        mVSyncThread = new VSyncThread(*this);
    }
    // MStar Android Patch Begin
#ifdef ENABLE_HWCURSOR
    // load hwcursor dev when SpriteController trigger
    mHwCursorDev = NULL;
#endif
    // MStar Android Patch End
}

HWComposer::~HWComposer() {
    if (mHwc) {
        eventControl(HWC_DISPLAY_PRIMARY, HWC_EVENT_VSYNC, 0);
    }
    if (mVSyncThread != NULL) {
        mVSyncThread->requestExitAndWait();
    }
    if (mHwc) {
        hwc_close_1(mHwc);
    }
    if (mFbDev) {
        framebuffer_close(mFbDev);
    }
    // MStar Android Patch Begin
#ifdef ENABLE_HWCURSOR
    if (mHwCursorDev) {
        hwcursor_close(mHwCursorDev);
    }
#endif
    // MStar Android Patch End
    delete mCBContext;
}

// Load and prepare the hardware composer module.  Sets mHwc.
void HWComposer::loadHwcModule()
{
    hw_module_t const* module;

    if (hw_get_module(HWC_HARDWARE_MODULE_ID, &module) != 0) {
        ALOGE("%s module not found", HWC_HARDWARE_MODULE_ID);
        return;
    }

    int err = hwc_open_1(module, &mHwc);
    if (err) {
        ALOGE("%s device failed to initialize (%s)",
              HWC_HARDWARE_COMPOSER, strerror(-err));
        return;
    }

    if (!hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_0) ||
            hwcHeaderVersion(mHwc) < MIN_HWC_HEADER_VERSION ||
            hwcHeaderVersion(mHwc) > HWC_HEADER_VERSION) {
        ALOGE("%s device version %#x unsupported, will not be used",
              HWC_HARDWARE_COMPOSER, mHwc->common.version);
        hwc_close_1(mHwc);
        mHwc = NULL;
        return;
    }
}

// Load and prepare the FB HAL, which uses the gralloc module.  Sets mFbDev.
int HWComposer::loadFbHalModule()
{
    hw_module_t const* module;

    int err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
    if (err != 0) {
        ALOGE("%s module not found", GRALLOC_HARDWARE_MODULE_ID);
        return err;
    }

    return framebuffer_open(module, &mFbDev);
}

status_t HWComposer::initCheck() const {
    return mHwc ? NO_ERROR : NO_INIT;
}

void HWComposer::hook_invalidate(const struct hwc_procs* procs) {
    cb_context* ctx = reinterpret_cast<cb_context*>(
            const_cast<hwc_procs_t*>(procs));
    ctx->hwc->invalidate();
}

void HWComposer::hook_vsync(const struct hwc_procs* procs, int disp,
        int64_t timestamp) {
    cb_context* ctx = reinterpret_cast<cb_context*>(
            const_cast<hwc_procs_t*>(procs));
    ctx->hwc->vsync(disp, timestamp);
}

void HWComposer::hook_hotplug(const struct hwc_procs* procs, int disp,
        int connected) {
    cb_context* ctx = reinterpret_cast<cb_context*>(
            const_cast<hwc_procs_t*>(procs));
    ctx->hwc->hotplug(disp, connected);
}

void HWComposer::invalidate() {
    mFlinger->repaintEverything();
}

void HWComposer::vsync(int disp, int64_t timestamp) {
    if (uint32_t(disp) < HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
        {
            Mutex::Autolock _l(mLock);

            // There have been reports of HWCs that signal several vsync events
            // with the same timestamp when turning the display off and on. This
            // is a bug in the HWC implementation, but filter the extra events
            // out here so they don't cause havoc downstream.
            if (timestamp == mLastHwVSync[disp]) {
                ALOGW("Ignoring duplicate VSYNC event from HWC (t=%lld)",
                        timestamp);
                return;
            }

            mLastHwVSync[disp] = timestamp;
        }

        char tag[16];
        snprintf(tag, sizeof(tag), "HW_VSYNC_%1u", disp);
        ATRACE_INT(tag, ++mVSyncCounts[disp] & 1);

        mEventHandler.onVSyncReceived(disp, timestamp);
    }
}

void HWComposer::hotplug(int disp, int connected) {
    if (disp == HWC_DISPLAY_PRIMARY || disp >= VIRTUAL_DISPLAY_ID_BASE) {
        ALOGE("hotplug event received for invalid display: disp=%d connected=%d",
                disp, connected);
        return;
    }
    queryDisplayProperties(disp);
    mEventHandler.onHotplugReceived(disp, bool(connected));
}

static float getDefaultDensity(uint32_t height) {
    if (height >= 1080) return ACONFIGURATION_DENSITY_XHIGH;
    else                return ACONFIGURATION_DENSITY_TV;
}

static const uint32_t DISPLAY_ATTRIBUTES[] = {
    HWC_DISPLAY_VSYNC_PERIOD,
    HWC_DISPLAY_WIDTH,
    HWC_DISPLAY_HEIGHT,
    HWC_DISPLAY_DPI_X,
    HWC_DISPLAY_DPI_Y,
    // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
    HWC_DISPLAY_OSDWIDTH,
    HWC_DISPLAY_OSDHEIGHT,
#endif
    // MStar Android Patch End
    HWC_DISPLAY_NO_ATTRIBUTE,
};
#define NUM_DISPLAY_ATTRIBUTES (sizeof(DISPLAY_ATTRIBUTES) / sizeof(DISPLAY_ATTRIBUTES)[0])

status_t HWComposer::queryDisplayProperties(int disp) {

    LOG_ALWAYS_FATAL_IF(!mHwc || !hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1));

    // use zero as default value for unspecified attributes
    int32_t values[NUM_DISPLAY_ATTRIBUTES - 1];
    memset(values, 0, sizeof(values));

    uint32_t config;
    size_t numConfigs = 1;
    status_t err = mHwc->getDisplayConfigs(mHwc, disp, &config, &numConfigs);
    if (err != NO_ERROR) {
        // this can happen if an unpluggable display is not connected
        mDisplayData[disp].connected = false;
        return err;
    }

    err = mHwc->getDisplayAttributes(mHwc, disp, config, DISPLAY_ATTRIBUTES, values);
    if (err != NO_ERROR) {
        // we can't get this display's info. turn it off.
        mDisplayData[disp].connected = false;
        return err;
    }

    int32_t w = 0, h = 0;
    for (size_t i = 0; i < NUM_DISPLAY_ATTRIBUTES - 1; i++) {
        switch (DISPLAY_ATTRIBUTES[i]) {
        case HWC_DISPLAY_VSYNC_PERIOD:
            mDisplayData[disp].refresh = nsecs_t(values[i]);
            break;
        case HWC_DISPLAY_WIDTH:
            mDisplayData[disp].width = values[i];
            break;
        case HWC_DISPLAY_HEIGHT:
            mDisplayData[disp].height = values[i];
            break;
        case HWC_DISPLAY_DPI_X:
            mDisplayData[disp].xdpi = values[i] / 1000.0f;
            break;
        case HWC_DISPLAY_DPI_Y:
            mDisplayData[disp].ydpi = values[i] / 1000.0f;
            break;
        // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
        case HWC_DISPLAY_OSDWIDTH:
            mDisplayData[disp].osdWidth = values[i];
            break;
        case HWC_DISPLAY_OSDHEIGHT:
            mDisplayData[disp].osdHeight = values[i];
            break;
#endif
        // MStar Android Patch End
        default:
            ALOG_ASSERT(false, "unknown display attribute[%d] %#x",
                    i, DISPLAY_ATTRIBUTES[i]);
            break;
        }
    }

    // FIXME: what should we set the format to?
    mDisplayData[disp].format = HAL_PIXEL_FORMAT_RGBA_8888;
    mDisplayData[disp].connected = true;
    if (mDisplayData[disp].xdpi == 0.0f || mDisplayData[disp].ydpi == 0.0f) {
        float dpi = getDefaultDensity(h);
        mDisplayData[disp].xdpi = dpi;
        mDisplayData[disp].ydpi = dpi;
    }
    return NO_ERROR;
}

status_t HWComposer::setVirtualDisplayProperties(int32_t id,
        uint32_t w, uint32_t h, uint32_t format) {
    if (id < VIRTUAL_DISPLAY_ID_BASE || id >= int32_t(mNumDisplays) ||
            !mAllocatedDisplayIDs.hasBit(id)) {
        return BAD_INDEX;
    }
    mDisplayData[id].width = w;
    mDisplayData[id].height = h;
    mDisplayData[id].format = format;
    mDisplayData[id].xdpi = mDisplayData[id].ydpi = getDefaultDensity(h);
    return NO_ERROR;
}

int32_t HWComposer::allocateDisplayId() {
    if (mAllocatedDisplayIDs.count() >= mNumDisplays) {
        return NO_MEMORY;
    }
    int32_t id = mAllocatedDisplayIDs.firstUnmarkedBit();
    mAllocatedDisplayIDs.markBit(id);
    mDisplayData[id].connected = true;
    return id;
}

status_t HWComposer::freeDisplayId(int32_t id) {
    if (id < NUM_BUILTIN_DISPLAYS) {
        // cannot free the reserved IDs
        return BAD_VALUE;
    }
    if (uint32_t(id)>31 || !mAllocatedDisplayIDs.hasBit(id)) {
        return BAD_INDEX;
    }
    mAllocatedDisplayIDs.clearBit(id);
    mDisplayData[id].connected = false;
    return NO_ERROR;
}

nsecs_t HWComposer::getRefreshPeriod(int disp) const {
    // MStar Android Patch Begin
    if (disp == HWC_DISPLAY_PRIMARY) {
        nsecs_t refresh = mHwc->getRefreshPeriod(mHwc);
        if (refresh < 0) {
            return mDisplayData[disp].refresh;
        }
        return refresh;
    }
    // MStar Android Patch End
    return mDisplayData[disp].refresh;
}

nsecs_t HWComposer::getRefreshTimestamp(int disp) const {
    // this returns the last refresh timestamp.
    // if the last one is not available, we estimate it based on
    // the refresh period and whatever closest timestamp we have.
    Mutex::Autolock _l(mLock);
    nsecs_t now = systemTime(CLOCK_MONOTONIC);
    return now - ((now - mLastHwVSync[disp]) %  mDisplayData[disp].refresh);
}

sp<Fence> HWComposer::getDisplayFence(int disp) const {
    return mDisplayData[disp].lastDisplayFence;
}

uint32_t HWComposer::getWidth(int disp) const {
    return mDisplayData[disp].width;
}

uint32_t HWComposer::getHeight(int disp) const {
    return mDisplayData[disp].height;
}

// MStar Android Patch Begin
int HWComposer::getOsdWidth(int disp) const {
    return mDisplayData[disp].osdWidth;
}

int HWComposer::getOsdHeight(int disp) const {
    return mDisplayData[disp].osdHeight;
}
// MStar Android Patch End

uint32_t HWComposer::getFormat(int disp) const {
    return mDisplayData[disp].format;
}

float HWComposer::getDpiX(int disp) const {
    return mDisplayData[disp].xdpi;
}

float HWComposer::getDpiY(int disp) const {
    return mDisplayData[disp].ydpi;
}

bool HWComposer::isConnected(int disp) const {
    return mDisplayData[disp].connected;
}

void HWComposer::eventControl(int disp, int event, int enabled) {
    if (uint32_t(disp)>31 || !mAllocatedDisplayIDs.hasBit(disp)) {
        ALOGD("eventControl ignoring event %d on unallocated disp %d (en=%d)",
              event, disp, enabled);
        return;
    }
    if (event != EVENT_VSYNC) {
        ALOGW("eventControl got unexpected event %d (disp=%d en=%d)",
              event, disp, enabled);
        return;
    }
    status_t err = NO_ERROR;
    if (mHwc && !mDebugForceFakeVSync) {
        // NOTE: we use our own internal lock here because we have to call
        // into the HWC with the lock held, and we want to make sure
        // that even if HWC blocks (which it shouldn't), it won't
        // affect other threads.
        Mutex::Autolock _l(mEventControlLock);
        const int32_t eventBit = 1UL << event;
        const int32_t newValue = enabled ? eventBit : 0;
        const int32_t oldValue = mDisplayData[disp].events & eventBit;
        if (newValue != oldValue) {
            ATRACE_CALL();
            err = mHwc->eventControl(mHwc, disp, event, enabled);
            if (!err) {
                int32_t& events(mDisplayData[disp].events);
                events = (events & ~eventBit) | newValue;
            }
        }
        // error here should not happen -- not sure what we should
        // do if it does.
        ALOGE_IF(err, "eventControl(%d, %d) failed %s",
                event, enabled, strerror(-err));
    }

    if (err == NO_ERROR && mVSyncThread != NULL) {
        mVSyncThread->setEnabled(enabled);
    }
}

status_t HWComposer::createWorkList(int32_t id, size_t numLayers) {
    if (uint32_t(id)>31 || !mAllocatedDisplayIDs.hasBit(id)) {
        return BAD_INDEX;
    }

    if (mHwc) {
        DisplayData& disp(mDisplayData[id]);
        if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1)) {
            // we need space for the HWC_FRAMEBUFFER_TARGET
            numLayers++;
        }
        if (disp.capacity < numLayers || disp.list == NULL) {
            size_t size = sizeof(hwc_display_contents_1_t)
                    + numLayers * sizeof(hwc_layer_1_t);
            free(disp.list);
            disp.list = (hwc_display_contents_1_t*)malloc(size);
            disp.capacity = numLayers;
        }
        if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1)) {
            disp.framebufferTarget = &disp.list->hwLayers[numLayers - 1];
            memset(disp.framebufferTarget, 0, sizeof(hwc_layer_1_t));
            const hwc_rect_t r = { 0, 0, (int) disp.width, (int) disp.height };
            disp.framebufferTarget->compositionType = HWC_FRAMEBUFFER_TARGET;
            disp.framebufferTarget->hints = 0;
            disp.framebufferTarget->flags = 0;
            disp.framebufferTarget->handle = disp.fbTargetHandle;
            disp.framebufferTarget->transform = 0;
            disp.framebufferTarget->blending = HWC_BLENDING_PREMULT;
            if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_3)) {
                disp.framebufferTarget->sourceCropf.left = 0;
                disp.framebufferTarget->sourceCropf.top = 0;
                disp.framebufferTarget->sourceCropf.right = disp.width;
                disp.framebufferTarget->sourceCropf.bottom = disp.height;
            } else {
                disp.framebufferTarget->sourceCrop = r;
            }
            // MStar Android Patch Begin
            // for 4k2k switch case ,we will delay set the displayFrame
            if (!(mFlinger->get4k2kAnd2k1kSwitch()) && id == DisplayDevice::DISPLAY_PRIMARY) {
                disp.framebufferTarget->displayFrame = r;
            }
            // MStar Android Patch End
            disp.framebufferTarget->visibleRegionScreen.numRects = 1;
            disp.framebufferTarget->visibleRegionScreen.rects =
                &disp.framebufferTarget->displayFrame;
            disp.framebufferTarget->acquireFenceFd = -1;
            disp.framebufferTarget->releaseFenceFd = -1;
            disp.framebufferTarget->planeAlpha = 0xFF;
        }
        disp.list->retireFenceFd = -1;
        disp.list->flags = HWC_GEOMETRY_CHANGED;
        disp.list->numHwLayers = numLayers;
    }
    return NO_ERROR;
}

status_t HWComposer::setFramebufferTarget(int32_t id,
        const sp<Fence>& acquireFence, const sp<GraphicBuffer>& buf) {
    if (uint32_t(id)>31 || !mAllocatedDisplayIDs.hasBit(id)) {
        return BAD_INDEX;
    }
    DisplayData& disp(mDisplayData[id]);
    if (!disp.framebufferTarget) {
        // this should never happen, but apparently eglCreateWindowSurface()
        // triggers a Surface::queueBuffer()  on some
        // devices (!?) -- log and ignore.
        ALOGE("HWComposer: framebufferTarget is null");
        return NO_ERROR;
    }

    int acquireFenceFd = -1;
    if (acquireFence->isValid()) {
        acquireFenceFd = acquireFence->dup();
    }

    // ALOGD("fbPost: handle=%p, fence=%d", buf->handle, acquireFenceFd);
    disp.fbTargetHandle = buf->handle;
    disp.framebufferTarget->handle = disp.fbTargetHandle;
    disp.framebufferTarget->acquireFenceFd = acquireFenceFd;
    return NO_ERROR;
}

status_t HWComposer::prepare() {
    for (size_t i=0 ; i<mNumDisplays ; i++) {
        DisplayData& disp(mDisplayData[i]);
        if (disp.framebufferTarget) {
            // make sure to reset the type to HWC_FRAMEBUFFER_TARGET
            // DO NOT reset the handle field to NULL, because it's possible
            // that we have nothing to redraw (eg: eglSwapBuffers() not called)
            // in which case, we should continue to use the same buffer.
            LOG_FATAL_IF(disp.list == NULL);
            disp.framebufferTarget->compositionType = HWC_FRAMEBUFFER_TARGET;
        }
        if (!disp.connected && disp.list != NULL) {
            ALOGW("WARNING: disp %d: connected, non-null list, layers=%d",
                  i, disp.list->numHwLayers);
        }
        mLists[i] = disp.list;
        // MStar Android Patch Begin
        if (i < DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES && mLists[i]) {
            const Vector< sp<Layer> >& visibleLayersSortedByZ =
                    mFlinger->getLayerSortedByZForHwcDisplay(i);
            sp<const DisplayDevice> displayDevice(mFlinger->getBuiltInDisplayDevice(i));
            //hwc 1.3 need to ignore HWC_FRAMEBUFFER_TARGET
#ifdef ENABLE_HWCOMPOSER_13
            for (size_t j=0; j<disp.list->numHwLayers-1; j++) {
#else
            for (size_t j=0; j<disp.list->numHwLayers; j++) {
#endif
                const sp<Layer>& layer(visibleLayersSortedByZ[j]);
                mLists[i]->hwLayers[j].name = (char*) malloc(strlen(layer->getName().string()) + 1);
                strcpy(mLists[i]->hwLayers[j].name,layer->getName().string());

                const Rect& region = layer->getActiveBuffer() != NULL?
                    layer->getActiveBuffer()->getBounds():Rect(-1,-1);
                const hwc_rect_t r = { 0, 0, region.getWidth(), region.getHeight() };
                mLists[i]->hwLayers[j].activeBufferRegion = r;

                const hwc_rect_t osdr = { 0, 0, displayDevice->getFrame().getWidth(), displayDevice->getFrame().getHeight() };
                mLists[i]->hwLayers[j].osdRegion = osdr;
            }
        }
        // MStar Android Patch End
        if (mLists[i]) {
            if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_3)) {
                mLists[i]->outbuf = disp.outbufHandle;
                mLists[i]->outbufAcquireFenceFd = -1;
            } else if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1)) {
                // garbage data to catch improper use
                mLists[i]->dpy = (hwc_display_t)0xDEADBEEF;
                mLists[i]->sur = (hwc_surface_t)0xDEADBEEF;
            } else {
                mLists[i]->dpy = EGL_NO_DISPLAY;
                mLists[i]->sur = EGL_NO_SURFACE;
            }
        }
    }
    // MStar Android Patch Begin
#ifdef BUILD_MSTARTV_MI
    if (mSwitchTimingEnable && mHwc->switchOSDTiming) {
        mHwc->switchOSDTiming(mHwc,mNumDisplays, mLists);
    }
#endif
    // MStar Android Patch End

    int err = mHwc->prepare(mHwc, mNumDisplays, mLists);
    ALOGE_IF(err, "HWComposer: prepare failed (%s)", strerror(-err));

    if (err == NO_ERROR) {
        // here we're just making sure that "skip" layers are set
        // to HWC_FRAMEBUFFER and we're also counting how many layers
        // we have of each type.
        //
        // If there are no window layers, we treat the display has having FB
        // composition, because SurfaceFlinger will use GLES to draw the
        // wormhole region.
        for (size_t i=0 ; i<mNumDisplays ; i++) {
            DisplayData& disp(mDisplayData[i]);
            disp.hasFbComp = false;
            disp.hasOvComp = false;
            if (disp.list) {
                for (size_t i=0 ; i<disp.list->numHwLayers ; i++) {
                    hwc_layer_1_t& l = disp.list->hwLayers[i];

                    //ALOGD("prepare: %d, type=%d, handle=%p",
                    //        i, l.compositionType, l.handle);

                    if (l.flags & HWC_SKIP_LAYER) {
                        l.compositionType = HWC_FRAMEBUFFER;
                    }
                    if (l.compositionType == HWC_FRAMEBUFFER) {
                        disp.hasFbComp = true;
                    }
                    if (l.compositionType == HWC_OVERLAY) {
                        disp.hasOvComp = true;
                    }
                }
                // MStar Android Patch Begin
                if (i < DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES && mLists[i]) {
                    //name has alloced some buffer,so we must free it after used.
#ifdef ENABLE_HWCOMPOSER_13
                    for (size_t j=0 ; j<disp.list->numHwLayers-1; j++) {
#else
                    for (size_t j=0 ; j<disp.list->numHwLayers; j++) {
#endif
                        free(mLists[i]->hwLayers[j].name);
                        mLists[i]->hwLayers[j].name = NULL;
                    }
                }
                // MStar Android Patch End
                if (disp.list->numHwLayers == (disp.framebufferTarget ? 1 : 0)) {
                    disp.hasFbComp = true;
                }
            } else {
                disp.hasFbComp = true;
            }
        }
    }
    return (status_t)err;
}

bool HWComposer::hasHwcComposition(int32_t id) const {
    if (!mHwc || uint32_t(id)>31 || !mAllocatedDisplayIDs.hasBit(id))
        return false;
    return mDisplayData[id].hasOvComp;
}

bool HWComposer::hasGlesComposition(int32_t id) const {
    if (!mHwc || uint32_t(id)>31 || !mAllocatedDisplayIDs.hasBit(id))
        return true;
    return mDisplayData[id].hasFbComp;
}

sp<Fence> HWComposer::getAndResetReleaseFence(int32_t id) {
    if (uint32_t(id)>31 || !mAllocatedDisplayIDs.hasBit(id))
        return Fence::NO_FENCE;

    int fd = INVALID_OPERATION;
    if (mHwc && hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1)) {
        const DisplayData& disp(mDisplayData[id]);
        if (disp.framebufferTarget) {
            fd = disp.framebufferTarget->releaseFenceFd;
            disp.framebufferTarget->acquireFenceFd = -1;
            disp.framebufferTarget->releaseFenceFd = -1;
        }
    }
    return fd >= 0 ? new Fence(fd) : Fence::NO_FENCE;
}

status_t HWComposer::commit() {
    int err = NO_ERROR;
    if (mHwc) {
        if (!hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1)) {
            // On version 1.0, the OpenGL ES target surface is communicated
            // by the (dpy, sur) fields and we are guaranteed to have only
            // a single display.
            mLists[0]->dpy = eglGetCurrentDisplay();
            mLists[0]->sur = eglGetCurrentSurface(EGL_DRAW);
        }

        for (size_t i=VIRTUAL_DISPLAY_ID_BASE; i<mNumDisplays; i++) {
            DisplayData& disp(mDisplayData[i]);
            if (disp.outbufHandle) {
                mLists[i]->outbuf = disp.outbufHandle;
                mLists[i]->outbufAcquireFenceFd =
                        disp.outbufAcquireFence->dup();
            }
        }

        // MStar Android Patch Begin
        sp<const DisplayDevice> displayDevice(mFlinger->getDefaultDisplayDevice());
        if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1) || !displayDevice->skipSwapBuffer) {
            err = mHwc->set(mHwc, mNumDisplays, mLists);
        }
        // MStar Android Patch End

        for (size_t i=0 ; i<mNumDisplays ; i++) {
            DisplayData& disp(mDisplayData[i]);
            disp.lastDisplayFence = disp.lastRetireFence;
            disp.lastRetireFence = Fence::NO_FENCE;
            if (disp.list) {
                if (disp.list->retireFenceFd != -1) {
                    disp.lastRetireFence = new Fence(disp.list->retireFenceFd);
                    disp.list->retireFenceFd = -1;
                }
                disp.list->flags &= ~HWC_GEOMETRY_CHANGED;
            }
        }
    }
    return (status_t)err;
}

status_t HWComposer::release(int disp) {
    LOG_FATAL_IF(disp >= VIRTUAL_DISPLAY_ID_BASE);
    if (mHwc) {
        eventControl(disp, HWC_EVENT_VSYNC, 0);
        return (status_t)mHwc->blank(mHwc, disp, 1);
    }
    return NO_ERROR;
}

status_t HWComposer::acquire(int disp) {
    LOG_FATAL_IF(disp >= VIRTUAL_DISPLAY_ID_BASE);
    if (mHwc) {
        return (status_t)mHwc->blank(mHwc, disp, 0);
    }
    return NO_ERROR;
}

void HWComposer::disconnectDisplay(int disp) {
    LOG_ALWAYS_FATAL_IF(disp < 0 || disp == HWC_DISPLAY_PRIMARY);
    DisplayData& dd(mDisplayData[disp]);
    free(dd.list);
    dd.list = NULL;
    dd.framebufferTarget = NULL;    // points into dd.list
    dd.fbTargetHandle = NULL;
    dd.outbufHandle = NULL;
    dd.lastRetireFence = Fence::NO_FENCE;
    dd.lastDisplayFence = Fence::NO_FENCE;
    dd.outbufAcquireFence = Fence::NO_FENCE;
}

int HWComposer::getVisualID() const {
    if (mHwc && hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1)) {
        // FIXME: temporary hack until HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED
        // is supported by the implementation. we can only be in this case
        // if we have HWC 1.1
        return HAL_PIXEL_FORMAT_RGBA_8888;
        //return HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    } else {
        return mFbDev->format;
    }
}

bool HWComposer::supportsFramebufferTarget() const {
    return (mHwc && hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1));
}

int HWComposer::fbPost(int32_t id,
        const sp<Fence>& acquireFence, const sp<GraphicBuffer>& buffer) {
    // MStar Android Patch Begin
    if (mFlinger->get4k2kAnd2k1kSwitch()) {
        if (mFlinger->isChangeTo2k1kMode()) {
 #ifndef ENABLE_HWCOMPOSER_13
            mFbDev->setOSDContentRegion(mFbDev,0,0,OSD_2K1K_WIDTH,OSD_2K1K_HEIGHT);
 #endif
            //ALOGD("fbdev_setOSDContentRegion to %d x %d",OSD_2K1K_WIDTH,OSD_2K1K_HEIGHT);
        } else {
 #ifndef ENABLE_HWCOMPOSER_13
            mFbDev->setOSDContentRegion(mFbDev,0,0,mFbDev->osdWidth,mFbDev->osdHeight);
 #endif
            //ALOGD("fbdev_setOSDContentRegion to %d x %d",mFbDev->osdWidth,mFbDev->osdHeight);
        }
    }
    // MStar Android Patch End
    if (mHwc && hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1)) {
        return setFramebufferTarget(id, acquireFence, buffer);
    } else {
        acquireFence->waitForever("HWComposer::fbPost");
        // MStar Android Patch Begin
        int result =  mFbDev->post(mFbDev, buffer->handle);
#ifdef BUILD_MSTARTV_MI
        if (mFlinger->get4k2kAnd2k1kSwitch()) {
            if (mFlinger->isChangeTo2k1kMode()) {
                CurOsdWidth = OSD_2K1K_WIDTH;
                CurOsdHeight = OSD_2K1K_HEIGHT;
            } else {
                CurOsdWidth = mFbDev->osdWidth;
                CurOsdHeight = mFbDev->osdHeight;
            }
            mHwc->setOSDRegionSize(mHwc,CurOsdWidth,CurOsdHeight);
        }
#endif
        return result;
        // MStar Android Patch End
    }
}

// MStar Android Patch Begin
int HWComposer::setGopStretchWin(int32_t gopNo, int32_t dest_Width, int32_t dest_Height) {
#ifdef ENABLE_HWCOMPOSER_13
    return -1;
#else
if (mFbDev->setGopStretchWin) {
    int32_t srcWidth = mFbDev->osdWidth;
    int32_t srcHeight = mFbDev->osdHeight;
    return mFbDev->setGopStretchWin(mFbDev, gopNo, srcWidth, srcHeight, dest_Width, dest_Height);
}
    return -1;
#endif
}

int HWComposer::setDisplayMode(int displayMode) {
#ifdef ENABLE_HWCOMPOSER_13
    return -1;
#else
    if (mFbDev->setDisplayMode) {
        return mFbDev->setDisplayMode(mFbDev,displayMode);
    }
    return -1;
#endif
}

int HWComposer::getCurOPTiming(int *width, int *height) {
#ifdef ENABLE_HWCOMPOSER_13
    if (mHwc->getCurOpTiming) {
        return mHwc->getCurOpTiming(mHwc,width,height);
    }
    return -1;
#else
    if (mFbDev->getCurOPTiming) {
        return mFbDev->getCurOPTiming(mFbDev,width,height);
    }
    return -1;
#endif
}

int HWComposer::getUrsaVsersion() {
#ifdef ENABLE_HWCOMPOSER_13
    if (mHwc->getUrsaVsersion) {
        return mHwc->getUrsaVsersion(mHwc);
    }
    return -1;
#else
    if (mFbDev->getUrsaVsersion) {
        return mFbDev->getUrsaVsersion(mFbDev);
    }
    return -1;
#endif
}

int HWComposer::isStbTarget() {
#ifdef ENABLE_HWCOMPOSER_13
    if (mHwc->isStbTarget) {
        return mHwc->isStbTarget(mHwc);
    }
    return 0;
#else
    return 0;
#endif
}

int HWComposer::isOsdNeedResize() {
#ifdef ENABLE_HWCOMPOSER_13
    if (mHwc->isOsdNeedResize) {
        return mHwc->isOsdNeedResize(mHwc);
    }
    return 0;
#else
    return 0;
#endif
}

#ifdef BUILD_FOR_STB
void HWComposer::reSetPanelSize(int panelwidth, int panelheight) const {
    if (mHwc->reSetPanelSize) {
        mHwc->reSetPanelSize(mHwc, panelwidth, panelheight);
    }
}

status_t HWComposer::reSetGopSize(int width, int height, int hstart, int interleave, int orientation, int value) {
    if (mFbDev->reSetGopSize) {
        return mFbDev->reSetGopSize(mFbDev, width, height, hstart, interleave, orientation, value);
    } else {
        return INVALID_OPERATION;
    }
}

status_t HWComposer::getAdjustValue(int *adjustwidth, int *adjustheight,int* adjustloffset,int* adjusttopoffset) {
    if (mFbDev->getAdjustValue) {
        return mFbDev->getAdjustValue(mFbDev, adjustwidth, adjustheight,adjustloffset,adjusttopoffset);
    } else {
        return INVALID_OPERATION;
    }
}

status_t HWComposer::setReproduceRate(int value) {
    if (mFbDev->setReproduceRate) {
        return mFbDev->setReproduceRate(mFbDev, value);
    } else {
        return INVALID_OPERATION;
    }
}
#endif
// MStar Android Patch End

int HWComposer::fbCompositionComplete() {
    if (mHwc && hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1))
        return NO_ERROR;

    if (mFbDev->compositionComplete) {
        return mFbDev->compositionComplete(mFbDev);
    } else {
        return INVALID_OPERATION;
    }
}

void HWComposer::fbDump(String8& result) {
    if (mFbDev && mFbDev->common.version >= 1 && mFbDev->dump) {
        const size_t SIZE = 4096;
        char buffer[SIZE];
        mFbDev->dump(mFbDev, buffer, SIZE);
        result.append(buffer);
    }
}

status_t HWComposer::setOutputBuffer(int32_t id, const sp<Fence>& acquireFence,
        const sp<GraphicBuffer>& buf) {
    if (uint32_t(id)>31 || !mAllocatedDisplayIDs.hasBit(id))
        return BAD_INDEX;
    if (id < VIRTUAL_DISPLAY_ID_BASE)
        return INVALID_OPERATION;

    DisplayData& disp(mDisplayData[id]);
    disp.outbufHandle = buf->handle;
    disp.outbufAcquireFence = acquireFence;
    return NO_ERROR;
}

sp<Fence> HWComposer::getLastRetireFence(int32_t id) {
    if (uint32_t(id)>31 || !mAllocatedDisplayIDs.hasBit(id))
        return Fence::NO_FENCE;
    return mDisplayData[id].lastRetireFence;
}

/*
 * Helper template to implement a concrete HWCLayer
 * This holds the pointer to the concrete hwc layer type
 * and implements the "iterable" side of HWCLayer.
 */
template<typename CONCRETE, typename HWCTYPE>
class Iterable : public HWComposer::HWCLayer {
protected:
    HWCTYPE* const mLayerList;
    HWCTYPE* mCurrentLayer;
    Iterable(HWCTYPE* layer) : mLayerList(layer), mCurrentLayer(layer) { }
    inline HWCTYPE const * getLayer() const { return mCurrentLayer; }
    inline HWCTYPE* getLayer() { return mCurrentLayer; }
    virtual ~Iterable() { }
private:
    // returns a copy of ourselves
    virtual HWComposer::HWCLayer* dup() {
        return new CONCRETE( static_cast<const CONCRETE&>(*this) );
    }
    virtual status_t setLayer(size_t index) {
        mCurrentLayer = &mLayerList[index];
        return NO_ERROR;
    }
};

/*
 * Concrete implementation of HWCLayer for HWC_DEVICE_API_VERSION_1_0.
 * This implements the HWCLayer side of HWCIterableLayer.
 */
class HWCLayerVersion1 : public Iterable<HWCLayerVersion1, hwc_layer_1_t> {
    struct hwc_composer_device_1* mHwc;
public:
    HWCLayerVersion1(struct hwc_composer_device_1* hwc, hwc_layer_1_t* layer)
        : Iterable<HWCLayerVersion1, hwc_layer_1_t>(layer), mHwc(hwc) { }

    virtual int32_t getCompositionType() const {
        return getLayer()->compositionType;
    }
    virtual uint32_t getHints() const {
        return getLayer()->hints;
    }
    virtual sp<Fence> getAndResetReleaseFence() {
        int fd = getLayer()->releaseFenceFd;
        getLayer()->releaseFenceFd = -1;
        return fd >= 0 ? new Fence(fd) : Fence::NO_FENCE;
    }
    virtual void setAcquireFenceFd(int fenceFd) {
        getLayer()->acquireFenceFd = fenceFd;
    }
    virtual void setPerFrameDefaultState() {
        //getLayer()->compositionType = HWC_FRAMEBUFFER;
    }
    virtual void setPlaneAlpha(uint8_t alpha) {
        if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_2)) {
            getLayer()->planeAlpha = alpha;
        } else {
            if (alpha < 0xFF) {
                getLayer()->flags |= HWC_SKIP_LAYER;
            }
        }
    }
    virtual void setDefaultState() {
        hwc_layer_1_t* const l = getLayer();
        l->compositionType = HWC_FRAMEBUFFER;
        l->hints = 0;
        l->flags = HWC_SKIP_LAYER;
        l->handle = 0;
        l->transform = 0;
        l->blending = HWC_BLENDING_NONE;
        l->visibleRegionScreen.numRects = 0;
        l->visibleRegionScreen.rects = NULL;
        l->acquireFenceFd = -1;
        l->releaseFenceFd = -1;
        l->planeAlpha = 0xFF;
        // MStar Android Patch Begin
        l->VendorUsageBits = 0x0;
        // MStar Android Patch End
    }
    virtual void setSkip(bool skip) {
        if (skip) {
            getLayer()->flags |= HWC_SKIP_LAYER;
        } else {
            getLayer()->flags &= ~HWC_SKIP_LAYER;
        }
    }
    virtual void setBlending(uint32_t blending) {
        getLayer()->blending = blending;
    }
    virtual void setTransform(uint32_t transform) {
        getLayer()->transform = transform;
    }
    virtual void setFrame(const Rect& frame) {
        getLayer()->displayFrame = reinterpret_cast<hwc_rect_t const&>(frame);
    }
    virtual void setCrop(const FloatRect& crop) {
        if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_3)) {
            getLayer()->sourceCropf = reinterpret_cast<hwc_frect_t const&>(crop);
        } else {
            /*
             * Since h/w composer didn't support a flot crop rect before version 1.3,
             * using integer coordinates instead produces a different output from the GL code in
             * Layer::drawWithOpenGL(). The difference can be large if the buffer crop to
             * window size ratio is large and a window crop is defined
             * (i.e.: if we scale the buffer a lot and we also crop it with a window crop).
             */
            hwc_rect_t& r = getLayer()->sourceCrop;
            r.left  = int(ceilf(crop.left));
            r.top   = int(ceilf(crop.top));
            r.right = int(floorf(crop.right));
            r.bottom= int(floorf(crop.bottom));
        }
    }
    virtual void setVisibleRegionScreen(const Region& reg) {
        // Region::getSharedBuffer creates a reference to the underlying
        // SharedBuffer of this Region, this reference is freed
        // in onDisplayed()
        hwc_region_t& visibleRegion = getLayer()->visibleRegionScreen;
        SharedBuffer const* sb = reg.getSharedBuffer(&visibleRegion.numRects);
        visibleRegion.rects = reinterpret_cast<hwc_rect_t const *>(sb->data());
    }
    virtual void setBuffer(const sp<GraphicBuffer>& buffer) {
        if (buffer == 0 || buffer->handle == 0) {
            getLayer()->compositionType = HWC_FRAMEBUFFER;
            getLayer()->flags |= HWC_SKIP_LAYER;
            getLayer()->handle = 0;
        } else {
            getLayer()->handle = buffer->handle;
        }
    }
    virtual void onDisplayed() {
        hwc_region_t& visibleRegion = getLayer()->visibleRegionScreen;
        SharedBuffer const* sb = SharedBuffer::bufferFromData(visibleRegion.rects);
        if (sb) {
            sb->release();
            // not technically needed but safer
            visibleRegion.numRects = 0;
            visibleRegion.rects = NULL;
        }

        getLayer()->acquireFenceFd = -1;
    }

    // MStar Android Patch Begin
    virtual uint32_t getVendorUsageBits() {
        return getLayer()->VendorUsageBits;
    }
    // MStar Android Patch End
};

/*
 * returns an iterator initialized at a given index in the layer list
 */
HWComposer::LayerListIterator HWComposer::getLayerIterator(int32_t id, size_t index) {
    if (uint32_t(id)>31 || !mAllocatedDisplayIDs.hasBit(id)) {
        return LayerListIterator();
    }
    const DisplayData& disp(mDisplayData[id]);
    if (!mHwc || !disp.list || index > disp.list->numHwLayers) {
        return LayerListIterator();
    }
    return LayerListIterator(new HWCLayerVersion1(mHwc, disp.list->hwLayers), index);
}

/*
 * returns an iterator on the beginning of the layer list
 */
HWComposer::LayerListIterator HWComposer::begin(int32_t id) {
    return getLayerIterator(id, 0);
}

/*
 * returns an iterator on the end of the layer list
 */
HWComposer::LayerListIterator HWComposer::end(int32_t id) {
    size_t numLayers = 0;
    if (uint32_t(id) <= 31 && mAllocatedDisplayIDs.hasBit(id)) {
        const DisplayData& disp(mDisplayData[id]);
        if (mHwc && disp.list) {
            numLayers = disp.list->numHwLayers;
            if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_1)) {
                // with HWC 1.1, the last layer is always the HWC_FRAMEBUFFER_TARGET,
                // which we ignore when iterating through the layer list.
                ALOGE_IF(!numLayers, "mDisplayData[%d].list->numHwLayers is 0", id);
                if (numLayers) {
                    numLayers--;
                }
            }
        }
    }
    return getLayerIterator(id, numLayers);
}

void HWComposer::dump(String8& result) const {
    if (mHwc) {
        result.appendFormat("Hardware Composer state (version %8x):\n", hwcApiVersion(mHwc));
        result.appendFormat("  mDebugForceFakeVSync=%d\n", mDebugForceFakeVSync);
        for (size_t i=0 ; i<mNumDisplays ; i++) {
            const DisplayData& disp(mDisplayData[i]);
            if (!disp.connected)
                continue;

            const Vector< sp<Layer> >& visibleLayersSortedByZ =
                    mFlinger->getLayerSortedByZForHwcDisplay(i);

            result.appendFormat(
                    "  Display[%d] : %ux%u, xdpi=%f, ydpi=%f, refresh=%lld\n",
                    i, disp.width, disp.height, disp.xdpi, disp.ydpi, disp.refresh);

            if (disp.list) {
                result.appendFormat(
                        "  numHwLayers=%u, flags=%08x\n",
                        disp.list->numHwLayers, disp.list->flags);

                result.append(
                        "    type    |  handle  |   hints  |   flags  | tr | blend |  format  |          source crop            |           frame           name \n"
                        "------------+----------+----------+----------+----+-------+----------+---------------------------------+--------------------------------\n");
                //      " __________ | ________ | ________ | ________ | __ | _____ | ________ | [_____._,_____._,_____._,_____._] | [_____,_____,_____,_____]
                for (size_t i=0 ; i<disp.list->numHwLayers ; i++) {
                    const hwc_layer_1_t&l = disp.list->hwLayers[i];
                    int32_t format = -1;
                    String8 name("unknown");
                    // MStar Android Patch Begin
                    int w = 0, h = 0, stride = 0;
                    // MStar Android Patch End

                    if (i < visibleLayersSortedByZ.size()) {
                        const sp<Layer>& layer(visibleLayersSortedByZ[i]);
                        const sp<GraphicBuffer>& buffer(
                                layer->getActiveBuffer());
                        if (buffer != NULL) {
                            format = buffer->getPixelFormat();
                            // MStar Android Patch Begin
                            w = buffer->getWidth();
                            h = buffer->getHeight();
                            stride = buffer->getStride();
                            // MStar Android Patch End
                        }
                        name = layer->getName();
                    }

                    int type = l.compositionType;
                    if (type == HWC_FRAMEBUFFER_TARGET) {
                        name = "HWC_FRAMEBUFFER_TARGET";
                        format = disp.format;
                    }

                    static char const* compositionTypeName[] = {
                            "GLES",
                            "HWC",
                            "BACKGROUND",
                            "FB TARGET",
                            "UNKNOWN"};
                    if (type >= NELEM(compositionTypeName))
                        type = NELEM(compositionTypeName) - 1;

                    // MStar Android Patch Begin
                    if (i < visibleLayersSortedByZ.size()) {
                        const sp<Layer>& layer(visibleLayersSortedByZ[i]);
                        if (layer != NULL) {
                            const sp<GraphicBuffer>& buffer(layer->getActiveBuffer());
                            if (buffer != NULL) {
                                char prefix[256];
                                snprintf(prefix, 256, "HWC_Z%d_Typ_%s_Disp_L%dT%dR%dB%d_w%dXh%dS%d",
                                       i, compositionTypeName[type], l.displayFrame.left, l.displayFrame.top, l.displayFrame.right, l.displayFrame.bottom,
                                       w,h, stride);
                                graphic_buffer_dump_helper::set_prefix(prefix);
                                graphic_buffer_dump_helper::dump_graphic_buffer_if_needed(buffer);
                                graphic_buffer_dump_helper::set_prefix( "unknow_");
                            }
                        }
                    }
                    // MStar Android Patch End

                    if (hwcHasApiVersion(mHwc, HWC_DEVICE_API_VERSION_1_3)) {
                        result.appendFormat(
                                " %10s | %08x | %08x | %08x | %02x | %05x | %08x | [%7.1f,%7.1f,%7.1f,%7.1f] | [%5d,%5d,%5d,%5d] %s\n",
                                        compositionTypeName[type],
                                        intptr_t(l.handle), l.hints, l.flags, l.transform, l.blending, format,
                                        l.sourceCropf.left, l.sourceCropf.top, l.sourceCropf.right, l.sourceCropf.bottom,
                                        l.displayFrame.left, l.displayFrame.top, l.displayFrame.right, l.displayFrame.bottom,
                                        name.string());
                    } else {
                        result.appendFormat(
                                " %10s | %08x | %08x | %08x | %02x | %05x | %08x | [%7d,%7d,%7d,%7d] | [%5d,%5d,%5d,%5d] %s\n",
                                        compositionTypeName[type],
                                        intptr_t(l.handle), l.hints, l.flags, l.transform, l.blending, format,
                                        l.sourceCrop.left, l.sourceCrop.top, l.sourceCrop.right, l.sourceCrop.bottom,
                                        l.displayFrame.left, l.displayFrame.top, l.displayFrame.right, l.displayFrame.bottom,
                                        name.string());
                    }
                }
            }
        }
    }

    if (mHwc && mHwc->dump) {
        const size_t SIZE = 4096;
        char buffer[SIZE];
        mHwc->dump(mHwc, buffer, SIZE);
        result.append(buffer);
    }
}

// ---------------------------------------------------------------------------

HWComposer::VSyncThread::VSyncThread(HWComposer& hwc)
    : mHwc(hwc), mEnabled(false),
      mNextFakeVSync(0),
      mRefreshPeriod(hwc.getRefreshPeriod(HWC_DISPLAY_PRIMARY))
{
}

void HWComposer::VSyncThread::setEnabled(bool enabled) {
    Mutex::Autolock _l(mLock);
    if (mEnabled != enabled) {
        mEnabled = enabled;
        mCondition.signal();
    }
}

void HWComposer::VSyncThread::onFirstRef() {
    run("VSyncThread", PRIORITY_URGENT_DISPLAY + PRIORITY_MORE_FAVORABLE);
}

bool HWComposer::VSyncThread::threadLoop() {
    { // scope for lock
        Mutex::Autolock _l(mLock);
        while (!mEnabled) {
            mCondition.wait(mLock);
        }
    }

    const nsecs_t period = mRefreshPeriod;
    const nsecs_t now = systemTime(CLOCK_MONOTONIC);
    nsecs_t next_vsync = mNextFakeVSync;
    nsecs_t sleep = next_vsync - now;
    if (sleep < 0) {
        // we missed, find where the next vsync should be
        sleep = (period - ((now - next_vsync) % period));
        next_vsync = now + sleep;
    }
    mNextFakeVSync = next_vsync + period;

    struct timespec spec;
    spec.tv_sec  = next_vsync / 1000000000;
    spec.tv_nsec = next_vsync % 1000000000;

    int err;
    do {
        err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
    } while (err<0 && errno == EINTR);

    if (err == 0) {
        mHwc.mEventHandler.onVSyncReceived(0, next_vsync);
    }

    return true;
}

HWComposer::DisplayData::DisplayData()
:   width(0), height(0), format(0),
    xdpi(0.0f), ydpi(0.0f),
    refresh(0),
    connected(false),
    hasFbComp(false), hasOvComp(false),
    capacity(0), list(NULL),
    // MStar Android Patch Begin
    osdWidth(0), osdHeight(0),
    // MStar Android Patch End
    framebufferTarget(NULL), fbTargetHandle(0),
    lastRetireFence(Fence::NO_FENCE), lastDisplayFence(Fence::NO_FENCE),
    outbufHandle(NULL), outbufAcquireFence(Fence::NO_FENCE),
    events(0)
{}

HWComposer::DisplayData::~DisplayData() {
    free(list);
}

// MStar Android Patch Begin
#ifdef ENABLE_HWCURSOR
int HWComposer::loadHwCursorModule() {
    hw_module_t const* module;
    struct hwcursor_fix_info hwCursorFinfo;
    if (mHwCursorDev != NULL) {
        ALOGI("mHwCursorDev has been loaded!");
        return 0;
    }
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0) {
        if (hwcursor_open(module, &mHwCursorDev)) {
            ALOGE("hwcursor_open failed");
            return -1;
        }
        if (mHwCursorDev->hwCursorGetfinfo) {
            mHwCursorDev->hwCursorGetfinfo(mHwCursorDev, &hwCursorFinfo);
            mHwCursorVaddr = hwCursorFinfo.hwCursorVaddr;
            mHwCursorWidth = hwCursorFinfo.hwCursorWidth;
            mHwCursorHeight = hwCursorFinfo.hwCursorHeight;
            mHwCursorStride = hwCursorFinfo.hwCursorStride;
            mUseHwCursor = true;
        } else {
            mHwCursorVaddr = 0;
            mHwCursorWidth = -1;
            mHwCursorHeight = -1;
            mHwCursorStride = -1;
            mUseHwCursor = false;
        }
    }
    showWarning = false;
    return 0;
}

int HWComposer::hwCursorShow() {
    if (mHwCursorDev && mHwCursorDev->hwCursorShow) {
        return mHwCursorDev->hwCursorShow(mHwCursorDev);
    }
    return 0;
}

int HWComposer::hwCursorHide() {
    if (mHwCursorDev && mHwCursorDev->hwCursorHide) {
        return mHwCursorDev->hwCursorHide(mHwCursorDev);
    }
    return 0;
}

int HWComposer::hwCursorSetMatrix(float dsdx, float dtdx, float dsdy, float dtdy) {
    if (mHwCursorDev && mHwCursorDev->hwCursorSetMatrix) {
        return mHwCursorDev->hwCursorSetMatrix(mHwCursorDev, dsdx, dtdx, dsdy, dtdy);
    }
    return 0;
}

int HWComposer::hwCursorSetPosition(float positionX, float positionY, float hotSpotX, float hotSpotY, int iconWidth, int iconHeight) {
    if (mHwCursorDev && mHwCursorDev->hwCursorSetPosition) {
        mHwCursorDev->hwCursorSetPosition(mHwCursorDev, positionX, positionY, hotSpotX, hotSpotY, iconWidth, iconHeight);
    }
    return 0;
}

int HWComposer::hwCursorSetAlpha(float alpha) {
    if (mHwCursorDev && mHwCursorDev->hwCursorSetAlpha) {
        mHwCursorDev->hwCursorSetAlpha(mHwCursorDev, alpha);
    }
    return 0;
}

int HWComposer::hwCursorChangeIcon() {
    if (mHwCursorDev && mHwCursorDev->hwCursorChangeIcon) {
        //copy icon from Ashm
        return mHwCursorDev->hwCursorChangeIcon(mHwCursorDev);
    }
    return 0;
}

int HWComposer::hwCursorDoTransaction() {
    if (mHwCursorDev && mHwCursorDev->hwCursorDoTransaction) {
        return mHwCursorDev->hwCursorDoTransaction(mHwCursorDev);
    }
    return 0;
}

int HWComposer::hwCursorChangeResolution() {
    if (mHwCursorDev && mHwCursorDev->hwCursorResolutionChanged) {
        return mHwCursorDev->hwCursorResolutionChanged(mHwCursorDev);
    }
    return 0;
}

int HWComposer::hwCursorsetOverscan(int left, int top, int right,int bottom) {
#ifdef ENABLE_HWCOMPOSER_13
    if (mHwCursorDev && mHwCursorDev->hwCursorOverscanChange) {
        return mHwCursorDev->hwCursorOverscanChange(mHwCursorDev,left,top,right,bottom);
    }
    return 0;
#else
    return 0;
#endif
}

#endif

int HWComposer::require4k2kOutputTiming(int enable){
    if (mSwitchTimingEnable && mHwc->require4k2kOutputTiming) {
        return mHwc->require4k2kOutputTiming(mHwc,enable);
    }
    return 0;
}

status_t HWComposer::updateFramebufferTargetFrameSize(int32_t id) {
    DisplayData& disp(mDisplayData[id]);
    if (!disp.framebufferTarget) {
        // means hwcomposer 1.0 version
        return NO_ERROR;
    }

    if (mFlinger->get4k2kAnd2k1kSwitch()) {
        if (mFlinger->isChangeTo2k1kMode()) {
            const hwc_rect_t r = { 0, 0, (int) disp.width/2, (int) disp.height/2};
            disp.framebufferTarget->displayFrame = r;
        } else {
            const hwc_rect_t r = { 0, 0, (int) disp.width, (int) disp.height};
            disp.framebufferTarget->displayFrame = r;
        }
    }

    return NO_ERROR;
}

status_t HWComposer::updateFramebufferTargetFrameSize(int32_t id, Rect &rect) {
    DisplayData& disp(mDisplayData[id]);
    if (!disp.framebufferTarget) {
        // means hwcomposer 1.0 version
        return NO_ERROR;
    }

    const hwc_rect_t r = { 0, 0, (int) rect.getWidth(), (int) rect.getHeight()};
    disp.framebufferTarget->displayFrame = r;

    return NO_ERROR;
}

void HWComposer::recalc3DTransform(Transform** first, Transform** second, Rect& frame) {
    hwc_3d_param_t param[2];
    if (mHwc->recalc3DTransform) {
        const hwc_rect_t r = { frame.left, frame.top, frame.right, frame.bottom};
        mHwc->recalc3DTransform(mHwc,param,r);
    }
    if (param[0].isVisiable == 1) {
        *first = new Transform();
        (*first)->set( param[0].scaleX, 0, 0, param[0].scaleY);
        (*first)->set(param[0].TanslateX, param[0].TanslateY);
    } else {
        (*first) = NULL;
    }
    if (param[1].isVisiable == 1) {
        (*second) = new Transform();
        (*second)->set( param[1].scaleX, 0, 0, param[1].scaleY);
        (*second)->set(param[1].TanslateX, param[1].TanslateY);
    } else {
        (*second) = NULL;
    }
}

// MStar Android Patch End

// ---------------------------------------------------------------------------
}; // namespace android