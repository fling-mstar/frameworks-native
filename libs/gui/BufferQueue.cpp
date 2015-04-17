/*
 * Copyright (C) 2012 The Android Open Source Project
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

#define LOG_TAG "BufferQueue"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS
//#define LOG_NDEBUG 0

#define GL_GLEXT_PROTOTYPES
#define EGL_EGLEXT_PROTOTYPES

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <gui/BufferQueue.h>
#include <gui/IConsumerListener.h>
#include <gui/ISurfaceComposer.h>
#include <private/gui/ComposerService.h>

#include <utils/Log.h>
#include <utils/Trace.h>
#include <utils/CallStack.h>

// MStar Android Patch Begin
#ifdef BUILD_MSTARTV
#include <MsTypes.h>
#include "MsFrmFormat.h"
#include "vsync_bridge.h"
#include "detile_yuv.h"
#endif
#include <cutils/properties.h>
// MStar Android Patch End

// Macros for including the BufferQueue name in log messages
#define ST_LOGV(x, ...) ALOGV("[%s] "x, mConsumerName.string(), ##__VA_ARGS__)
#define ST_LOGD(x, ...) ALOGD("[%s] "x, mConsumerName.string(), ##__VA_ARGS__)
#define ST_LOGI(x, ...) ALOGI("[%s] "x, mConsumerName.string(), ##__VA_ARGS__)
#define ST_LOGW(x, ...) ALOGW("[%s] "x, mConsumerName.string(), ##__VA_ARGS__)
#define ST_LOGE(x, ...) ALOGE("[%s] "x, mConsumerName.string(), ##__VA_ARGS__)

#define ATRACE_BUFFER_INDEX(index)                                            \
    if (ATRACE_ENABLED()) {                                                   \
        char ___traceBuf[1024];                                               \
        snprintf(___traceBuf, 1024, "%s: %d", mConsumerName.string(),         \
                (index));                                                     \
        android::ScopedTrace ___bufTracer(ATRACE_TAG, ___traceBuf);           \
    }

// MStar Android Patch Begin
#define WIDTH_4K2K      3840
#define HEIGHT_4K2K     2160
#define WIDTH_2K1K      1920
#define HEIGHT_2K1K     1080
// MStar Android Patch End

namespace android {

// Get an ID that's unique within this process.
static int32_t createProcessUniqueId() {
    static volatile int32_t globalCounter = 0;
    return android_atomic_inc(&globalCounter);
}

static const char* scalingModeName(int scalingMode) {
    switch (scalingMode) {
        case NATIVE_WINDOW_SCALING_MODE_FREEZE: return "FREEZE";
        case NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW: return "SCALE_TO_WINDOW";
        case NATIVE_WINDOW_SCALING_MODE_SCALE_CROP: return "SCALE_CROP";
        default: return "Unknown";
    }
}

BufferQueue::BufferQueue(const sp<IGraphicBufferAlloc>& allocator) :
    mDefaultWidth(1),
    mDefaultHeight(1),
    mMaxAcquiredBufferCount(1),
    mDefaultMaxBufferCount(2),
    mOverrideMaxBufferCount(0),
    mConsumerControlledByApp(false),
    mDequeueBufferCannotBlock(false),
    mUseAsyncBuffer(true),
    mConnectedApi(NO_CONNECTED_API),
    mAbandoned(false),
    mFrameCounter(0),
    mBufferHasBeenQueued(false),
    mDefaultBufferFormat(PIXEL_FORMAT_RGBA_8888),
    mConsumerUsageBits(0),
    mTransformHint(0),
    // MStar Android Patch Begin
    mDetileCount(0)
    // MStar Android Patch End
{
    // Choose a name using the PID and a process-unique ID.
    mConsumerName = String8::format("unnamed-%d-%d", getpid(), createProcessUniqueId());

    ST_LOGV("BufferQueue");
    if (allocator == NULL) {
        sp<ISurfaceComposer> composer(ComposerService::getComposerService());
        mGraphicBufferAlloc = composer->createGraphicBufferAlloc();
        if (mGraphicBufferAlloc == 0) {
            ST_LOGE("createGraphicBufferAlloc() failed in BufferQueue()");
        }
    } else {
        mGraphicBufferAlloc = allocator;
    }
}

BufferQueue::~BufferQueue() {
    ST_LOGV("~BufferQueue");
}

status_t BufferQueue::setDefaultMaxBufferCountLocked(int count) {
    const int minBufferCount = mUseAsyncBuffer ? 2 : 1;
    if (count < minBufferCount || count > NUM_BUFFER_SLOTS)
        return BAD_VALUE;

    mDefaultMaxBufferCount = count;
    mDequeueCondition.broadcast();

    return NO_ERROR;
}

void BufferQueue::setConsumerName(const String8& name) {
    Mutex::Autolock lock(mMutex);

    // MStar Android Patch Begin
    char value[PROPERTY_VALUE_MAX];
    if (property_get("ms.vsync_bridge.swdetile", value, NULL)
        && (!strcmp(value, "1") || !strcasecmp(value, "true"))) {
        ST_LOGD("MStar force SW detile");
        return;
    }
    // MStar Android Patch End

    mConsumerName = name;
}

status_t BufferQueue::setDefaultBufferFormat(uint32_t defaultFormat) {
    Mutex::Autolock lock(mMutex);
    mDefaultBufferFormat = defaultFormat;
    return NO_ERROR;
}

status_t BufferQueue::setConsumerUsageBits(uint32_t usage) {
    Mutex::Autolock lock(mMutex);
    mConsumerUsageBits = usage;
    return NO_ERROR;
}

status_t BufferQueue::setTransformHint(uint32_t hint) {
    ST_LOGV("setTransformHint: %02x", hint);
    Mutex::Autolock lock(mMutex);
    mTransformHint = hint;
    return NO_ERROR;
}

status_t BufferQueue::setBufferCount(int bufferCount) {
    ST_LOGV("setBufferCount: count=%d", bufferCount);

    sp<IConsumerListener> listener;
    {
        Mutex::Autolock lock(mMutex);

        if (mAbandoned) {
            ST_LOGE("setBufferCount: BufferQueue has been abandoned!");
            return NO_INIT;
        }
        if (bufferCount > NUM_BUFFER_SLOTS) {
            ST_LOGE("setBufferCount: bufferCount too large (max %d)",
                    NUM_BUFFER_SLOTS);
            return BAD_VALUE;
        }

        // Error out if the user has dequeued buffers
        for (int i=0 ; i<NUM_BUFFER_SLOTS; i++) {
            if (mSlots[i].mBufferState == BufferSlot::DEQUEUED) {
                ST_LOGE("setBufferCount: client owns some buffers");
                return -EINVAL;
            }
        }

        if (bufferCount == 0) {
            mOverrideMaxBufferCount = 0;
            mDequeueCondition.broadcast();
            return NO_ERROR;
        }

        // fine to assume async to false before we're setting the buffer count
        const int minBufferSlots = getMinMaxBufferCountLocked(false);
        if (bufferCount < minBufferSlots) {
            ST_LOGE("setBufferCount: requested buffer count (%d) is less than "
                    "minimum (%d)", bufferCount, minBufferSlots);
            return BAD_VALUE;
        }

        // here we're guaranteed that the client doesn't have dequeued buffers
        // and will release all of its buffer references.  We don't clear the
        // queue, however, so currently queued buffers still get displayed.
        freeAllBuffersLocked();
        mOverrideMaxBufferCount = bufferCount;
        mDequeueCondition.broadcast();
        listener = mConsumerListener;
    } // scope for lock

    if (listener != NULL) {
        listener->onBuffersReleased();
    }

    return NO_ERROR;
}

int BufferQueue::query(int what, int* outValue)
{
    ATRACE_CALL();
    Mutex::Autolock lock(mMutex);

    if (mAbandoned) {
        ST_LOGE("query: BufferQueue has been abandoned!");
        return NO_INIT;
    }

    int value;
    switch (what) {
    case NATIVE_WINDOW_WIDTH:
        value = mDefaultWidth;
        break;
    case NATIVE_WINDOW_HEIGHT:
        value = mDefaultHeight;
        break;
    case NATIVE_WINDOW_FORMAT:
        value = mDefaultBufferFormat;
        break;
    case NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS:
        value = getMinUndequeuedBufferCount(false);
        break;
    case NATIVE_WINDOW_CONSUMER_RUNNING_BEHIND:
        value = (mQueue.size() >= 2);
        break;
    case NATIVE_WINDOW_CONSUMER_USAGE_BITS:
        value = mConsumerUsageBits;
        break;
    // MStar Android Patch Begin
    case NATIVE_WINDOW_HARDWARE_RENDER:
        value = (mConsumerName == "SurfaceView");
        break;
    // MStar Android Patch End
    default:
        return BAD_VALUE;
    }
    outValue[0] = value;
    return NO_ERROR;
}

status_t BufferQueue::requestBuffer(int slot, sp<GraphicBuffer>* buf) {
    ATRACE_CALL();
    ST_LOGV("requestBuffer: slot=%d", slot);
    Mutex::Autolock lock(mMutex);
    if (mAbandoned) {
        ST_LOGE("requestBuffer: BufferQueue has been abandoned!");
        return NO_INIT;
    }
    if (slot < 0 || slot >= NUM_BUFFER_SLOTS) {
        ST_LOGE("requestBuffer: slot index out of range [0, %d]: %d",
                NUM_BUFFER_SLOTS, slot);
        return BAD_VALUE;
    } else if (mSlots[slot].mBufferState != BufferSlot::DEQUEUED) {
        ST_LOGE("requestBuffer: slot %d is not owned by the client (state=%d)",
                slot, mSlots[slot].mBufferState);
        return BAD_VALUE;
    }
    mSlots[slot].mRequestBufferCalled = true;
    *buf = mSlots[slot].mGraphicBuffer;
    return NO_ERROR;
}

// MStar Android Patch Begin
status_t BufferQueue::requestBuffer4K2K(sp<GraphicBuffer>* buf) {
    status_t error;
    sp<GraphicBuffer> graphicBuffer(
            mGraphicBufferAlloc->createGraphicBuffer(
                    WIDTH_4K2K, HEIGHT_4K2K, 1, GRALLOC_USAGE_PRIVATE_4K2K, &error));
    if (graphicBuffer == 0) {
        ST_LOGE("BufferQueue::requestBuffer4K2K failed");
        return error;
    }
    *buf = graphicBuffer;
    return OK;
}
// MStar Android Patch End

status_t BufferQueue::dequeueBuffer(int *outBuf, sp<Fence>* outFence, bool async,
        uint32_t w, uint32_t h, uint32_t format, uint32_t usage) {
    ATRACE_CALL();
    ST_LOGV("dequeueBuffer: w=%d h=%d fmt=%#x usage=%#x", w, h, format, usage);

    if ((w && !h) || (!w && h)) {
        ST_LOGE("dequeueBuffer: invalid size: w=%u, h=%u", w, h);
        return BAD_VALUE;
    }

    status_t returnFlags(OK);
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSyncKHR eglFence = EGL_NO_SYNC_KHR;

    { // Scope for the lock
        Mutex::Autolock lock(mMutex);

        if (format == 0) {
            format = mDefaultBufferFormat;
        }
        // turn on usage bits the consumer requested
        usage |= mConsumerUsageBits;

        int found = -1;
        // MStar Android Patch Begin
        #ifdef RENDER_IN_QUEUE_BUFFER
        int found2 = -1;
        #endif
        // MStar Android Patch End
        bool tryAgain = true;
        while (tryAgain) {
            if (mAbandoned) {
                ST_LOGE("dequeueBuffer: BufferQueue has been abandoned!");
                return NO_INIT;
            }

            const int maxBufferCount = getMaxBufferCountLocked(async);
            if (async && mOverrideMaxBufferCount) {
                // FIXME: some drivers are manually setting the buffer-count (which they
                // shouldn't), so we do this extra test here to handle that case.
                // This is TEMPORARY, until we get this fixed.
                if (mOverrideMaxBufferCount < maxBufferCount) {
                    ST_LOGE("dequeueBuffer: async mode is invalid with buffercount override");
                    return BAD_VALUE;
                }
            }

            // Free up any buffers that are in slots beyond the max buffer
            // count.
            for (int i = maxBufferCount; i < NUM_BUFFER_SLOTS; i++) {
                assert(mSlots[i].mBufferState == BufferSlot::FREE);
                if (mSlots[i].mGraphicBuffer != NULL) {
                    freeBufferLocked(i);
                    returnFlags |= IGraphicBufferProducer::RELEASE_ALL_BUFFERS;
                }
            }

            // look for a free buffer to give to the client
            found = INVALID_BUFFER_SLOT;
            // MStar Android Patch Begin
            #ifdef RENDER_IN_QUEUE_BUFFER
            found2 = INVALID_BUFFER_SLOT;
            #endif
            // MStar Android Patch End
            int dequeuedCount = 0;
            int acquiredCount = 0;
            for (int i = 0; i < maxBufferCount; i++) {
                const int state = mSlots[i].mBufferState;
                switch (state) {
                    case BufferSlot::DEQUEUED:
                        dequeuedCount++;
                        break;
                    case BufferSlot::ACQUIRED:
                        acquiredCount++;
                        break;
                    case BufferSlot::FREE:
                        /* We return the oldest of the free buffers to avoid
                         * stalling the producer if possible.  This is because
                         * the consumer may still have pending reads of the
                         * buffers in flight.
                         */
                        if ((found < 0) ||
                                mSlots[i].mFrameNumber < mSlots[found].mFrameNumber) {
                            found = i;
                        }
                        break;
                }

                // MStar Android Patch Begin
                #ifdef RENDER_IN_QUEUE_BUFFER
                if ((usage & GRALLOC_USAGE_PRIVATE_2) && (mConsumerName == "SurfaceView")) {
                    if (state == BufferSlot::QUEUED || state == BufferSlot::ACQUIRED) {
                        bool isOlder = mSlots[i].mFrameNumber <
                                mSlots[found2].mFrameNumber;
                        if (found2 < 0 || isOlder) {
                            found2 = i;
                        }
                    }
                }
                #endif
                // MStar Android Patch End
            }

            // MStar Android Patch Begin
            #ifdef RENDER_IN_QUEUE_BUFFER
            if ((usage & GRALLOC_USAGE_PRIVATE_2) && (mConsumerName == "SurfaceView") && found2 != -1) {
                if (found == -1 || mSlots[found2].mFrameNumber < mSlots[found].mFrameNumber) {
                    if (mSlots[found2].mBufferState == BufferSlot::QUEUED) {
                        Fifo::iterator front(mQueue.begin());
                        mQueue.erase(front);
                        mSlots[found2].mBufferState = BufferSlot::FREE;
                    }
                    found = found2;
                }
            }
            #endif
            // MStar Android Patch End

            // clients are not allowed to dequeue more than one buffer
            // if they didn't set a buffer count.
            if (!mOverrideMaxBufferCount && dequeuedCount) {
                ST_LOGE("dequeueBuffer: can't dequeue multiple buffers without "
                        "setting the buffer count");
                return -EINVAL;
            }

            // See whether a buffer has been queued since the last
            // setBufferCount so we know whether to perform the min undequeued
            // buffers check below.
            if (mBufferHasBeenQueued) {
                // make sure the client is not trying to dequeue more buffers
                // than allowed.
                const int newUndequeuedCount = maxBufferCount - (dequeuedCount+1);
                const int minUndequeuedCount = getMinUndequeuedBufferCount(async);
                if (newUndequeuedCount < minUndequeuedCount) {
                    ST_LOGE("dequeueBuffer: min undequeued buffer count (%d) "
                            "exceeded (dequeued=%d undequeudCount=%d)",
                            minUndequeuedCount, dequeuedCount,
                            newUndequeuedCount);
                    return -EBUSY;
                }
            }

            // If no buffer is found, wait for a buffer to be released or for
            // the max buffer count to change.
            tryAgain = found == INVALID_BUFFER_SLOT;
            if (tryAgain) {
                // return an error if we're in "cannot block" mode (producer and consumer
                // are controlled by the application) -- however, the consumer is allowed
                // to acquire briefly an extra buffer (which could cause us to have to wait here)
                // and that's okay because we know the wait will be brief (it happens
                // if we dequeue a buffer while the consumer has acquired one but not released
                // the old one yet -- for e.g.: see GLConsumer::updateTexImage()).
                if (mDequeueBufferCannotBlock && (acquiredCount <= mMaxAcquiredBufferCount)) {
                    ST_LOGE("dequeueBuffer: would block! returning an error instead.");
                    return WOULD_BLOCK;
                }
                mDequeueCondition.wait(mMutex);
            }
        }


        if (found == INVALID_BUFFER_SLOT) {
            // This should not happen.
            ST_LOGE("dequeueBuffer: no available buffer slots");
            return -EBUSY;
        }

        const int buf = found;
        *outBuf = found;

        ATRACE_BUFFER_INDEX(buf);

        const bool useDefaultSize = !w && !h;
        if (useDefaultSize) {
            // use the default size
            w = mDefaultWidth;
            h = mDefaultHeight;
        }

        mSlots[buf].mBufferState = BufferSlot::DEQUEUED;

        const sp<GraphicBuffer>& buffer(mSlots[buf].mGraphicBuffer);
        if ((buffer == NULL) ||
            (uint32_t(buffer->width)  != w) ||
            (uint32_t(buffer->height) != h) ||
            (uint32_t(buffer->format) != format) ||
            ((uint32_t(buffer->usage) & usage) != usage))
        {
            mSlots[buf].mAcquireCalled = false;
            mSlots[buf].mGraphicBuffer = NULL;
            mSlots[buf].mRequestBufferCalled = false;
            mSlots[buf].mEglFence = EGL_NO_SYNC_KHR;
            mSlots[buf].mFence = Fence::NO_FENCE;
            mSlots[buf].mEglDisplay = EGL_NO_DISPLAY;

            returnFlags |= IGraphicBufferProducer::BUFFER_NEEDS_REALLOCATION;
        }


        if (CC_UNLIKELY(mSlots[buf].mFence == NULL)) {
            ST_LOGE("dequeueBuffer: about to return a NULL fence from mSlot. "
                    "buf=%d, w=%d, h=%d, format=%d",
                    buf, buffer->width, buffer->height, buffer->format);
        }

        dpy = mSlots[buf].mEglDisplay;
        eglFence = mSlots[buf].mEglFence;
        *outFence = mSlots[buf].mFence;
        mSlots[buf].mEglFence = EGL_NO_SYNC_KHR;
        mSlots[buf].mFence = Fence::NO_FENCE;
    }  // end lock scope

    if (returnFlags & IGraphicBufferProducer::BUFFER_NEEDS_REALLOCATION) {
        status_t error;
        // MStar Android Patch Begin
#ifdef ENABLE_HWCOMPOSER_13
        sp<GraphicBuffer> graphicBuffer;
        graphicBuffer = mGraphicBufferAlloc->createGraphicBuffer(w, h, format, usage, &error);
        if (graphicBuffer == 0) {
            ST_LOGW("dequeueBuffer: out of dedicated memory, fallback to system memory");
            usage &= ~GRALLOC_USAGE_ION_MASK;
            graphicBuffer = mGraphicBufferAlloc->createGraphicBuffer(w, h, format, usage, &error);
            if (graphicBuffer == 0) {
                ST_LOGE("dequeueBuffer: SurfaceComposer::createGraphicBuffer failed");
                return error;
            }
        }

#else
#ifdef ENABLE_GOP_HW_COMPOSER
        {
            char property[PROPERTY_VALUE_MAX]={0};
            if (property_get("mstar.layers_in_fb2", property, NULL)>0) {
                const char *layerName = mConsumerName.string();
                if (!strncmp(property,layerName,strlen(property))) {
                    if ((w==WIDTH_2K1K)&&(h==HEIGHT_2K1K)) {
                        usage |= GRALLOC_USAGE_GOP_HW_COMPOSER;
                    }
                }
            }
        }
#endif
        sp<GraphicBuffer> graphicBuffer(
                mGraphicBufferAlloc->createGraphicBuffer(w, h, format, usage, &error));
        if (graphicBuffer == 0) {
            ST_LOGE("dequeueBuffer: SurfaceComposer::createGraphicBuffer failed");
            return error;
        }
#endif
        // MStar Android Patch End
        { // Scope for the lock
            Mutex::Autolock lock(mMutex);

            if (mAbandoned) {
                ST_LOGE("dequeueBuffer: BufferQueue has been abandoned!");
                return NO_INIT;
            }

            mSlots[*outBuf].mFrameNumber = ~0;
            mSlots[*outBuf].mGraphicBuffer = graphicBuffer;
        }
    }

    if (eglFence != EGL_NO_SYNC_KHR) {
        EGLint result = eglClientWaitSyncKHR(dpy, eglFence, 0, 1000000000);
        // If something goes wrong, log the error, but return the buffer without
        // synchronizing access to it.  It's too late at this point to abort the
        // dequeue operation.
        if (result == EGL_FALSE) {
            ST_LOGE("dequeueBuffer: error waiting for fence: %#x", eglGetError());
        } else if (result == EGL_TIMEOUT_EXPIRED_KHR) {
            ST_LOGE("dequeueBuffer: timeout waiting for fence");
        }
        eglDestroySyncKHR(dpy, eglFence);
    }

    ST_LOGV("dequeueBuffer: returning slot=%d/%llu buf=%p flags=%#x", *outBuf,
            mSlots[*outBuf].mFrameNumber,
            mSlots[*outBuf].mGraphicBuffer->handle, returnFlags);

    // MStar Android Patch Begin
    #ifdef BUILD_MSTARTV
    if ((usage & GRALLOC_USAGE_PRIVATE_2) && (mConsumerName == "SurfaceView")) {
        void *vaddr=NULL;
        status_t err = mSlots[*outBuf].mGraphicBuffer->lock(GRALLOC_USAGE_SW_READ_MASK, (void**)(&vaddr));
        MS_DispFrameFormat dff;
        memcpy(&dff, vaddr, sizeof(MS_DispFrameFormat));
        mSlots[*outBuf].mGraphicBuffer->unlock();

        if (vaddr && dff.CodecType) {
            vsync_bridge_wait_frame_done(&dff);
        }
    }
    #endif
    // MStar Android Patch End

    return returnFlags;
}

status_t BufferQueue::queueBuffer(int buf,
        const QueueBufferInput& input, QueueBufferOutput* output) {
    ATRACE_CALL();
    ATRACE_BUFFER_INDEX(buf);

    Rect crop;
    uint32_t transform;
    int scalingMode;
    int64_t timestamp;
    bool isAutoTimestamp;
    bool async;
    sp<Fence> fence;
    // MStar Android Patch Begin
    #if defined(RENDER_IN_QUEUE_BUFFER) && defined(BLOCK_UNTIL_DISP_INIT_DONE)
    MS_DispFrameFormat dff = {0xFFFF,};
    #endif
    // MStar Android Patch End

    input.deflate(&timestamp, &isAutoTimestamp, &crop, &scalingMode, &transform,
            &async, &fence);

    if (fence == NULL) {
        ST_LOGE("queueBuffer: fence is NULL");
        return BAD_VALUE;
    }

    switch (scalingMode) {
        case NATIVE_WINDOW_SCALING_MODE_FREEZE:
        case NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW:
        case NATIVE_WINDOW_SCALING_MODE_SCALE_CROP:
        case NATIVE_WINDOW_SCALING_MODE_NO_SCALE_CROP:
            break;
        default:
            ST_LOGE("unknown scaling mode: %d", scalingMode);
            return -EINVAL;
    }

    sp<IConsumerListener> listener;

    { // scope for the lock
        Mutex::Autolock lock(mMutex);

        if (mAbandoned) {
            ST_LOGE("queueBuffer: BufferQueue has been abandoned!");
            return NO_INIT;
        }

        const int maxBufferCount = getMaxBufferCountLocked(async);
        if (async && mOverrideMaxBufferCount) {
            // FIXME: some drivers are manually setting the buffer-count (which they
            // shouldn't), so we do this extra test here to handle that case.
            // This is TEMPORARY, until we get this fixed.
            if (mOverrideMaxBufferCount < maxBufferCount) {
                ST_LOGE("queueBuffer: async mode is invalid with buffercount override");
                return BAD_VALUE;
            }
        }
        if (buf < 0 || buf >= maxBufferCount) {
            ST_LOGE("queueBuffer: slot index out of range [0, %d]: %d",
                    maxBufferCount, buf);
            return -EINVAL;
        } else if (mSlots[buf].mBufferState != BufferSlot::DEQUEUED) {
            ST_LOGE("queueBuffer: slot %d is not owned by the client "
                    "(state=%d)", buf, mSlots[buf].mBufferState);
            return -EINVAL;
        } else if (!mSlots[buf].mRequestBufferCalled) {
            ST_LOGE("queueBuffer: slot %d was enqueued without requesting a "
                    "buffer", buf);
            return -EINVAL;
        }

        ST_LOGV("queueBuffer: slot=%d/%llu time=%#llx crop=[%d,%d,%d,%d] "
                "tr=%#x scale=%s",
                buf, mFrameCounter + 1, timestamp,
                crop.left, crop.top, crop.right, crop.bottom,
                transform, scalingModeName(scalingMode));

        const sp<GraphicBuffer>& graphicBuffer(mSlots[buf].mGraphicBuffer);
        Rect bufferRect(graphicBuffer->getWidth(), graphicBuffer->getHeight());
        Rect croppedCrop;
        crop.intersect(bufferRect, &croppedCrop);
        if (croppedCrop != crop) {
            ST_LOGE("queueBuffer: crop rect is not contained within the "
                    "buffer in slot %d", buf);
            return -EINVAL;
        }

        mSlots[buf].mFence = fence;
        mSlots[buf].mBufferState = BufferSlot::QUEUED;
        mFrameCounter++;
        mSlots[buf].mFrameNumber = mFrameCounter;

        BufferItem item;
        item.mAcquireCalled = mSlots[buf].mAcquireCalled;
        item.mGraphicBuffer = mSlots[buf].mGraphicBuffer;
        item.mCrop = crop;
        item.mTransform = transform & ~NATIVE_WINDOW_TRANSFORM_INVERSE_DISPLAY;
        item.mTransformToDisplayInverse = bool(transform & NATIVE_WINDOW_TRANSFORM_INVERSE_DISPLAY);
        item.mScalingMode = scalingMode;
        item.mTimestamp = timestamp;
        item.mIsAutoTimestamp = isAutoTimestamp;
        item.mFrameNumber = mFrameCounter;
        item.mBuf = buf;
        item.mFence = fence;
        item.mIsDroppable = mDequeueBufferCannotBlock || async;

        // MStar Android Patch Begin
        #ifdef RENDER_IN_QUEUE_BUFFER
        if ((graphicBuffer->getUsage() & GRALLOC_USAGE_PRIVATE_2) && (mConsumerName == "SurfaceView")) {
            void *vaddr = NULL;
            status_t err = graphicBuffer->lock(GRALLOC_USAGE_SW_READ_MASK, (void**)(&vaddr));
            MS_DispFrameFormat *dff1 = (MS_DispFrameFormat*)vaddr;
            dff1->u32ScalingMode = scalingMode;
            vsync_bridge_render_frame((MS_DispFrameFormat*)vaddr, VSYNC_BRIDGE_UPDATE);
            #ifdef BLOCK_UNTIL_DISP_INIT_DONE
            memcpy(&dff, vaddr, sizeof(MS_DispFrameFormat));
            #endif
            graphicBuffer->unlock();
            mQueue.push_back(item);
            listener = mConsumerListener;
        } else
        #endif
        // MStar Android Patch End
        if (mQueue.empty()) {
            // when the queue is empty, we can ignore "mDequeueBufferCannotBlock", and
            // simply queue this buffer.
            mQueue.push_back(item);
            listener = mConsumerListener;
        } else {
            // when the queue is not empty, we need to look at the front buffer
            // state and see if we need to replace it.
            Fifo::iterator front(mQueue.begin());
            if (front->mIsDroppable) {
                // buffer slot currently queued is marked free if still tracked
                if (stillTracking(front)) {
                    mSlots[front->mBuf].mBufferState = BufferSlot::FREE;
                    // reset the frame number of the freed buffer so that it is the first in
                    // line to be dequeued again.
                    mSlots[front->mBuf].mFrameNumber = 0;
                }
                // and we record the new buffer in the queued list
                *front = item;
            } else {
                mQueue.push_back(item);
                listener = mConsumerListener;
            }
        }

        mBufferHasBeenQueued = true;
        mDequeueCondition.broadcast();

        output->inflate(mDefaultWidth, mDefaultHeight, mTransformHint,
                mQueue.size());

        ATRACE_INT(mConsumerName.string(), mQueue.size());
    } // scope for the lock

    // call back without lock held
    if (listener != 0) {
        listener->onFrameAvailable();
    }

    // MStar Android Patch Begin
    #if defined(RENDER_IN_QUEUE_BUFFER) && defined(BLOCK_UNTIL_DISP_INIT_DONE)
    if ((mConsumerName == "SurfaceView") && (dff.OverlayID != 0xFFFF)) {
        if (vsync_bridge_wait_init_done(&dff) && !dff.u8IsNoWaiting) {
            Mutex::Autolock lock(mMutex);
            const sp<GraphicBuffer>& graphicBuffer(mSlots[buf].mGraphicBuffer);
            if (graphicBuffer != NULL) {
                void *vaddr = NULL;
                status_t err = graphicBuffer->lock(GRALLOC_USAGE_SW_READ_MASK, (void**)(&vaddr));
                if (vaddr != NULL) {
                    ST_LOGD("wait disp path init done");
                    vsync_bridge_render_frame((MS_DispFrameFormat*)vaddr, VSYNC_BRIDGE_UPDATE);
                    graphicBuffer->unlock();
                }
            }
        }
    }
    #endif
    // MStar Android Patch End

    return NO_ERROR;
}

void BufferQueue::cancelBuffer(int buf, const sp<Fence>& fence) {
    ATRACE_CALL();
    ST_LOGV("cancelBuffer: slot=%d", buf);
    Mutex::Autolock lock(mMutex);

    if (mAbandoned) {
        ST_LOGW("cancelBuffer: BufferQueue has been abandoned!");
        return;
    }

    if (buf < 0 || buf >= NUM_BUFFER_SLOTS) {
        ST_LOGE("cancelBuffer: slot index out of range [0, %d]: %d",
                NUM_BUFFER_SLOTS, buf);
        return;
    } else if (mSlots[buf].mBufferState != BufferSlot::DEQUEUED) {
        ST_LOGE("cancelBuffer: slot %d is not owned by the client (state=%d)",
                buf, mSlots[buf].mBufferState);
        return;
    } else if (fence == NULL) {
        ST_LOGE("cancelBuffer: fence is NULL");
        return;
    }
    mSlots[buf].mBufferState = BufferSlot::FREE;
    mSlots[buf].mFrameNumber = 0;
    mSlots[buf].mFence = fence;
    mDequeueCondition.broadcast();
}


status_t BufferQueue::connect(const sp<IBinder>& token,
        int api, bool producerControlledByApp, QueueBufferOutput* output) {
    ATRACE_CALL();
    ST_LOGV("connect: api=%d producerControlledByApp=%s", api,
            producerControlledByApp ? "true" : "false");
    Mutex::Autolock lock(mMutex);

retry:
    if (mAbandoned) {
        ST_LOGE("connect: BufferQueue has been abandoned!");
        return NO_INIT;
    }

    if (mConsumerListener == NULL) {
        ST_LOGE("connect: BufferQueue has no consumer!");
        return NO_INIT;
    }

    if (mConnectedApi != NO_CONNECTED_API) {
        ST_LOGE("connect: already connected (cur=%d, req=%d)",
                mConnectedApi, api);
        return -EINVAL;
    }

    // If we disconnect and reconnect quickly, we can be in a state where our slots are
    // empty but we have many buffers in the queue.  This can cause us to run out of
    // memory if we outrun the consumer.  Wait here if it looks like we have too many
    // buffers queued up.
    int maxBufferCount = getMaxBufferCountLocked(false);    // worst-case, i.e. largest value
    if (mQueue.size() > (size_t) maxBufferCount) {
        // TODO: make this bound tighter?
        ST_LOGV("queue size is %d, waiting", mQueue.size());
        mDequeueCondition.wait(mMutex);
        goto retry;
    }

    int err = NO_ERROR;
    switch (api) {
        case NATIVE_WINDOW_API_EGL:
        case NATIVE_WINDOW_API_CPU:
        case NATIVE_WINDOW_API_MEDIA:
        case NATIVE_WINDOW_API_CAMERA:
            mConnectedApi = api;
            output->inflate(mDefaultWidth, mDefaultHeight, mTransformHint, mQueue.size());

            // set-up a death notification so that we can disconnect
            // automatically when/if the remote producer dies.
            if (token != NULL && token->remoteBinder() != NULL) {
                status_t err = token->linkToDeath(static_cast<IBinder::DeathRecipient*>(this));
                if (err == NO_ERROR) {
                    mConnectedProducerToken = token;
                } else {
                    ALOGE("linkToDeath failed: %s (%d)", strerror(-err), err);
                }
            }
            break;
        default:
            err = -EINVAL;
            break;
    }

    mBufferHasBeenQueued = false;
    mDequeueBufferCannotBlock = mConsumerControlledByApp && producerControlledByApp;

    return err;
}

void BufferQueue::binderDied(const wp<IBinder>& who) {
    // If we're here, it means that a producer we were connected to died.
    // We're GUARANTEED that we still are connected to it because it has no other way
    // to get disconnected -- or -- we wouldn't be here because we're removing this
    // callback upon disconnect. Therefore, it's okay to read mConnectedApi without
    // synchronization here.
    int api = mConnectedApi;
    this->disconnect(api);
}

status_t BufferQueue::disconnect(int api) {
    ATRACE_CALL();
    ST_LOGV("disconnect: api=%d", api);

    int err = NO_ERROR;
    sp<IConsumerListener> listener;

    { // Scope for the lock
        Mutex::Autolock lock(mMutex);

        if (mAbandoned) {
            // it is not really an error to disconnect after the surface
            // has been abandoned, it should just be a no-op.
            return NO_ERROR;
        }

        switch (api) {
            case NATIVE_WINDOW_API_EGL:
            case NATIVE_WINDOW_API_CPU:
            case NATIVE_WINDOW_API_MEDIA:
            case NATIVE_WINDOW_API_CAMERA:
                if (mConnectedApi == api) {
                    freeAllBuffersLocked();
                    // remove our death notification callback if we have one
                    sp<IBinder> token = mConnectedProducerToken;
                    if (token != NULL) {
                        // this can fail if we're here because of the death notification
                        // either way, we just ignore.
                        token->unlinkToDeath(static_cast<IBinder::DeathRecipient*>(this));
                    }
                    mConnectedProducerToken = NULL;
                    mConnectedApi = NO_CONNECTED_API;
                    mDequeueCondition.broadcast();
                    listener = mConsumerListener;
                } else {
                    ST_LOGE("disconnect: connected to another api (cur=%d, req=%d)",
                            mConnectedApi, api);
                    err = -EINVAL;
                }
                break;
            default:
                ST_LOGE("disconnect: unknown API %d", api);
                err = -EINVAL;
                break;
        }
    }

    if (listener != NULL) {
        listener->onBuffersReleased();
    }

    return err;
}

void BufferQueue::dump(String8& result, const char* prefix) const {
    Mutex::Autolock _l(mMutex);

    String8 fifo;
    int fifoSize = 0;
    Fifo::const_iterator i(mQueue.begin());
    while (i != mQueue.end()) {
        fifo.appendFormat("%02d:%p crop=[%d,%d,%d,%d], "
                "xform=0x%02x, time=%#llx, scale=%s\n",
                i->mBuf, i->mGraphicBuffer.get(),
                i->mCrop.left, i->mCrop.top, i->mCrop.right,
                i->mCrop.bottom, i->mTransform, i->mTimestamp,
                scalingModeName(i->mScalingMode)
                );
        i++;
        fifoSize++;
    }


    result.appendFormat(
            "%s-BufferQueue mMaxAcquiredBufferCount=%d, mDequeueBufferCannotBlock=%d, default-size=[%dx%d], "
            "default-format=%d, transform-hint=%02x, FIFO(%d)={%s}\n",
            prefix, mMaxAcquiredBufferCount, mDequeueBufferCannotBlock, mDefaultWidth,
            mDefaultHeight, mDefaultBufferFormat, mTransformHint,
            fifoSize, fifo.string());

    struct {
        const char * operator()(int state) const {
            switch (state) {
                case BufferSlot::DEQUEUED: return "DEQUEUED";
                case BufferSlot::QUEUED: return "QUEUED";
                case BufferSlot::FREE: return "FREE";
                case BufferSlot::ACQUIRED: return "ACQUIRED";
                default: return "Unknown";
            }
        }
    } stateName;

    // just trim the free buffers to not spam the dump
    int maxBufferCount = 0;
    for (int i=NUM_BUFFER_SLOTS-1 ; i>=0 ; i--) {
        const BufferSlot& slot(mSlots[i]);
        if ((slot.mBufferState != BufferSlot::FREE) || (slot.mGraphicBuffer != NULL)) {
            maxBufferCount = i+1;
            break;
        }
    }

    for (int i=0 ; i<maxBufferCount ; i++) {
        const BufferSlot& slot(mSlots[i]);
        const sp<GraphicBuffer>& buf(slot.mGraphicBuffer);
        result.appendFormat(
            "%s%s[%02d:%p] state=%-8s",
                prefix, (slot.mBufferState == BufferSlot::ACQUIRED)?">":" ", i, buf.get(),
                stateName(slot.mBufferState)
        );

        if (buf != NULL) {
            result.appendFormat(
                    ", %p [%4ux%4u:%4u,%3X]",
                    buf->handle, buf->width, buf->height, buf->stride,
                    buf->format);
            // MStar Android Patch Begin
            graphic_buffer_dump_helper::dump_graphic_buffer_if_needed(buf);
            // MStar Android Patch End
        }
        result.append("\n");
    }
}

// MStar Android Patch Begin
bool BufferQueue::isSurfaceTextureLayer() {
    return false;
}
// MStar Android Patch End

void BufferQueue::freeBufferLocked(int slot) {
    ST_LOGV("freeBufferLocked: slot=%d", slot);
    mSlots[slot].mGraphicBuffer = 0;

    // MStar Android Patch Begin
    #ifdef BUILD_MSTARTV
    mSlots[slot].mYUVBuffer = 0;
    #endif
    // MStar Android Patch End

    if (mSlots[slot].mBufferState == BufferSlot::ACQUIRED) {
        mSlots[slot].mNeedsCleanupOnRelease = true;
    }
    mSlots[slot].mBufferState = BufferSlot::FREE;
    mSlots[slot].mFrameNumber = 0;
    mSlots[slot].mAcquireCalled = false;

    // destroy fence as BufferQueue now takes ownership
    if (mSlots[slot].mEglFence != EGL_NO_SYNC_KHR) {
        eglDestroySyncKHR(mSlots[slot].mEglDisplay, mSlots[slot].mEglFence);
        mSlots[slot].mEglFence = EGL_NO_SYNC_KHR;
    }
    mSlots[slot].mFence = Fence::NO_FENCE;
}

void BufferQueue::freeAllBuffersLocked() {
    mBufferHasBeenQueued = false;
    for (int i = 0; i < NUM_BUFFER_SLOTS; i++) {
        freeBufferLocked(i);
    }
}

// MStar Android Patch Begin
bool BufferQueue::yuv420Detile(BufferItem *buffer) {
#ifdef BUILD_MSTARTV
    int buf = buffer->mBuf;
    status_t err;
    char* vaddr = NULL;
    uint32_t    width, height;
    char* out_buf = NULL;
    EGLClientBuffer clientBuffer = NULL;
    MS_DispFrameFormat *dff = NULL;
    ST_LOGV("yuv420Detile [%d]", buf);

    vsync_bridge_init();

    err = mSlots[buf].mGraphicBuffer->lock(GRALLOC_USAGE_SW_READ_MASK, (void**)(&vaddr));
    if (err != 0) {
        ALOGE("mActiveBuffer->lock(...) failed: %d\n", err);
        goto ERROR_HANDLE;
    }

    dff = (MS_DispFrameFormat*)vaddr;
    dff->sFrames[MS_VIEW_TYPE_CENTER].u32Height = dff->sFrames[MS_VIEW_TYPE_CENTER].u32Height - dff->sFrames[MS_VIEW_TYPE_CENTER].u32CropBottom;
    dff->sFrames[MS_VIEW_TYPE_CENTER].u32CropBottom = 0;
    MsVdec_FrameSizeGet(vaddr, &width, &height);

    if (width == 0 || height == 0) {
        ALOGE("yuv420Detile fail: width = %d, height = %d", width, height);
        goto ERROR_HANDLE;
    }


    buffer->mCrop.left = dff->sFrames[MS_VIEW_TYPE_CENTER].u32CropLeft;
    buffer->mCrop.right = dff->sFrames[MS_VIEW_TYPE_CENTER].u32Width - dff->sFrames[MS_VIEW_TYPE_CENTER].u32CropRight;
    buffer->mCrop.top = dff->sFrames[MS_VIEW_TYPE_CENTER].u32CropTop;
    buffer->mCrop.bottom = dff->sFrames[MS_VIEW_TYPE_CENTER].u32Height - dff->sFrames[MS_VIEW_TYPE_CENTER].u32CropBottom;

     ST_LOGV("pts = %lld, %d %d [%d %d] [%d %d %d %d] mDetileCount = %d", dff->u64Pts, width , height,dff->sFrames[MS_VIEW_TYPE_CENTER].u32Width,
        dff->sFrames[MS_VIEW_TYPE_CENTER].u32Height, dff->sFrames[MS_VIEW_TYPE_CENTER].u32CropLeft,
        dff->sFrames[MS_VIEW_TYPE_CENTER].u32CropTop,
        dff->sFrames[MS_VIEW_TYPE_CENTER].u32CropRight, dff->sFrames[MS_VIEW_TYPE_CENTER].u32CropBottom,
        mDetileCount);

    mDetileCount++;

    // for the crop not apply in time, push 2 blank frame
        // patch for tearing issue, reallocate UMP buffer to prevent issue
        if (mSlots[buf].mYUVBuffer == 0 || mSlots[buf].mYUVBuffer->getWidth() != width || mSlots[buf].mYUVBuffer->getHeight() != height || mSlots[buf].mYUVBuffer->getPixelFormat() != HAL_PIXEL_FORMAT_YV12) {
            mSlots[buf].mYUVBuffer = new GraphicBuffer(width, height, HAL_PIXEL_FORMAT_YV12, GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_SW_WRITE_OFTEN);
        }

        err = mSlots[buf].mYUVBuffer->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&out_buf));
        if (err != 0) {
            ST_LOGE("mSlots[buf].mYUVBuffer->lock(HW) failed: %d\n", err);
            goto ERROR_HANDLE;
        }

    if (dff->sFrames[MS_VIEW_TYPE_CENTER].eColorFormat == MS_COLOR_FORMAT_YUYV) {
        MsVdec_DetileYUV422(vaddr, out_buf);
    } else {
        MsVdec_DetileYUV420(vaddr, out_buf);
        MsVdec_BlankCropAreaYUV((uint8_t*)out_buf, width, height, buffer->mCrop.top, buffer->mCrop.left,
            buffer->mCrop.right - buffer->mCrop.left, buffer->mCrop.bottom - buffer->mCrop.top);
    }

        err = mSlots[buf].mYUVBuffer->unlock();
        if (err != 0) {
            ST_LOGE("mSlots[buf].mYUVBuffer->unlock() failed: %d\n", err);
        }

    err = mSlots[buf].mGraphicBuffer->unlock();
    if (err != 0) {
        ST_LOGE("mSlots[buf].mGraphicBuffer->unlock() failed: %d\n", err);
    }
    return true;

ERROR_HANDLE:
#endif
    return false;
}
// MStar Android Patch End

status_t BufferQueue::acquireBuffer(BufferItem *buffer, nsecs_t expectedPresent) {
    ATRACE_CALL();
    Mutex::Autolock _l(mMutex);

    // MStar Android Patch Begin
    #ifdef BUILD_MSTARTV
    bool bReplaceGraphicBuffer = false;
    #endif
    // MStar Android Patch End

    // Check that the consumer doesn't currently have the maximum number of
    // buffers acquired.  We allow the max buffer count to be exceeded by one
    // buffer, so that the consumer can successfully set up the newly acquired
    // buffer before releasing the old one.
    int numAcquiredBuffers = 0;
    for (int i = 0; i < NUM_BUFFER_SLOTS; i++) {
        if (mSlots[i].mBufferState == BufferSlot::ACQUIRED) {
            numAcquiredBuffers++;
        }
    }
    if (numAcquiredBuffers >= mMaxAcquiredBufferCount+1) {
        ST_LOGE("acquireBuffer: max acquired buffer count reached: %d (max=%d)",
                numAcquiredBuffers, mMaxAcquiredBufferCount);
        return INVALID_OPERATION;
    }

    // check if queue is empty
    // In asynchronous mode the list is guaranteed to be one buffer
    // deep, while in synchronous mode we use the oldest buffer.
    if (mQueue.empty()) {
        return NO_BUFFER_AVAILABLE;
    }

    Fifo::iterator front(mQueue.begin());
    // MStar Android Patch Begin
    if (mConsumerName == "SurfaceView") {
        while (front != mQueue.end()) {
            const BufferSlot &slot = mSlots[front->mBuf];
            if (slot.mBufferState == BufferSlot::DEQUEUED || slot.mGraphicBuffer.get() == NULL) {
                mQueue.erase(front);
                front = mQueue.begin();
                continue;
            }
            break;
        }
        if (mQueue.empty()) {
            mDequeueCondition.broadcast();
            return NO_BUFFER_AVAILABLE;
        }
    }
    // MStar Android Patch End

    // If expectedPresent is specified, we may not want to return a buffer yet.
    // If it's specified and there's more than one buffer queued, we may
    // want to drop a buffer.
    if (expectedPresent != 0) {
        const int MAX_REASONABLE_NSEC = 1000000000ULL;  // 1 second

        // The "expectedPresent" argument indicates when the buffer is expected
        // to be presented on-screen.  If the buffer's desired-present time
        // is earlier (less) than expectedPresent, meaning it'll be displayed
        // on time or possibly late if we show it ASAP, we acquire and return
        // it.  If we don't want to display it until after the expectedPresent
        // time, we return PRESENT_LATER without acquiring it.
        //
        // To be safe, we don't defer acquisition if expectedPresent is
        // more than one second in the future beyond the desired present time
        // (i.e. we'd be holding the buffer for a long time).
        //
        // NOTE: code assumes monotonic time values from the system clock are
        // positive.

        // Start by checking to see if we can drop frames.  We skip this check
        // if the timestamps are being auto-generated by Surface -- if the
        // app isn't generating timestamps explicitly, they probably don't
        // want frames to be discarded based on them.
        while (mQueue.size() > 1 && !mQueue[0].mIsAutoTimestamp) {
            // If entry[1] is timely, drop entry[0] (and repeat).  We apply
            // an additional criteria here: we only drop the earlier buffer if
            // our desiredPresent falls within +/- 1 second of the expected
            // present.  Otherwise, bogus desiredPresent times (e.g. 0 or
            // a small relative timestamp), which normally mean "ignore the
            // timestamp and acquire immediately", would cause us to drop
            // frames.
            //
            // We may want to add an additional criteria: don't drop the
            // earlier buffer if entry[1]'s fence hasn't signaled yet.
            //
            // (Vector front is [0], back is [size()-1])
            const BufferItem& bi(mQueue[1]);
            nsecs_t desiredPresent = bi.mTimestamp;
            if (desiredPresent < expectedPresent - MAX_REASONABLE_NSEC ||
                    desiredPresent > expectedPresent) {
                // This buffer is set to display in the near future, or
                // desiredPresent is garbage.  Either way we don't want to
                // drop the previous buffer just to get this on screen sooner.
                ST_LOGV("pts nodrop: des=%lld expect=%lld (%lld) now=%lld",
                        desiredPresent, expectedPresent, desiredPresent - expectedPresent,
                        systemTime(CLOCK_MONOTONIC));
                break;
            }
            ST_LOGV("pts drop: queue1des=%lld expect=%lld size=%d",
                    desiredPresent, expectedPresent, mQueue.size());
            if (stillTracking(front)) {
                // front buffer is still in mSlots, so mark the slot as free
                mSlots[front->mBuf].mBufferState = BufferSlot::FREE;
            }
            mQueue.erase(front);
            front = mQueue.begin();
        }

        // See if the front buffer is due.
        nsecs_t desiredPresent = front->mTimestamp;
        if (desiredPresent > expectedPresent &&
                desiredPresent < expectedPresent + MAX_REASONABLE_NSEC) {
            ST_LOGV("pts defer: des=%lld expect=%lld (%lld) now=%lld",
                    desiredPresent, expectedPresent, desiredPresent - expectedPresent,
                    systemTime(CLOCK_MONOTONIC));
            return PRESENT_LATER;
        }

        ST_LOGV("pts accept: des=%lld expect=%lld (%lld) now=%lld",
                desiredPresent, expectedPresent, desiredPresent - expectedPresent,
                systemTime(CLOCK_MONOTONIC));
    }


    int buf = front->mBuf;
    *buffer = *front;
    ATRACE_BUFFER_INDEX(buf);

    ST_LOGV("acquireBuffer: acquiring { slot=%d/%llu, buffer=%p }",
            front->mBuf, front->mFrameNumber,
            front->mGraphicBuffer->handle);

    // If the buffer has previously been acquired by the consumer, set
    // mGraphicBuffer to NULL to avoid unnecessarily remapping this
    // buffer on the consumer side.
    if (buffer->mAcquireCalled) {
        buffer->mGraphicBuffer = NULL;
    }

    // if front buffer still being tracked update slot state
    if (stillTracking(front)) {

        // MStar Android Patch Begin
        #ifdef BUILD_MSTARTV
        if (mSlots[buf].mGraphicBuffer != NULL) {
            if ((mSlots[buf].mGraphicBuffer->getUsage() & GRALLOC_USAGE_PRIVATE_2) && (mConsumerName != "SurfaceView")) {
                if (yuv420Detile(buffer)) {
                    buffer->mGraphicBuffer = mSlots[buf].mYUVBuffer;
                }
            } else if (mSlots[buf].mGraphicBuffer->getUsage() & GRALLOC_USAGE_PRIVATE_2) {
                // freeze
                const sp<GraphicBuffer>& graphicBuffer(mSlots[buf].mGraphicBuffer);
                void *vaddr = NULL;
                unsigned char u8FreezeMode = 0;
                unsigned int OverlayID = 0;
                status_t err = graphicBuffer->lock(GRALLOC_USAGE_SW_READ_MASK, (void**)(&vaddr));
                MS_DispFrameFormat *dff = (MS_DispFrameFormat*)vaddr;
                if (err == 0) {
                    u8FreezeMode = (u8FreezeMode < MS_FREEZE_MODE_MAX) ? dff->u8FreezeThisFrame : 0;
                    if (dff->u8SeamlessDS && (u8FreezeMode == MS_FREEZE_MODE_VIDEO)) {
                        u8FreezeMode = 0;       // seamless DS mode no needs to capture
                    }
                    OverlayID = dff->OverlayID;
                }
                graphicBuffer->unlock();

                if (u8FreezeMode) {
                    void *out_buf = NULL;
                    int width, height, left, top, right, bottom;
                    bool bGotVideoSize = false;

                    if (vsync_bridge_capture_video(u8FreezeMode, OverlayID, 0, 0, &width, &height, &left, &top, &right, &bottom, out_buf) >= 0) {
                        bGotVideoSize = true;
                    }

                    if (bGotVideoSize) {
                        ST_LOGD("Capture Video w = %d, h = %d, crop[%d %d %d %d]", width, height, left, top, right, bottom);

                        mSlots[buf].mYUVBuffer = 0;
                        mSlots[buf].mYUVBuffer = new GraphicBuffer(width, height, HAL_PIXEL_FORMAT_RGBX_8888, GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_SW_WRITE_OFTEN);
                        err = mSlots[buf].mYUVBuffer->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&out_buf));
                        if (err == 0) {
                            int status = vsync_bridge_capture_video(u8FreezeMode, OverlayID, 0, 0, &width, &height,
                                &left, &top, &right, &bottom, out_buf);
                            mSlots[buf].mYUVBuffer->unlock();

                            if (status < 0) {
                                ST_LOGE("vsync_bridge_capture_video failed\n");
                                mSlots[buf].mYUVBuffer = 0;
                            } else {
                                buffer->mGraphicBuffer = mSlots[buf].mYUVBuffer;
                                front->mCrop.left = left;
                                front->mCrop.top = top;
                                front->mCrop.right = right;
                                front->mCrop.bottom = bottom;
                                bReplaceGraphicBuffer = true;
                            }
                        } else {
                            ST_LOGE("mSlots[buf].mYUVBuffer->lock(HW) failed: %d\n", err);
                            mSlots[buf].mYUVBuffer = 0;
                        }
                    }
                }
            }
        }
        #endif
        // MStar Android Patch End

        mSlots[buf].mAcquireCalled = true;
        // MStar Android Patch Begin
        #ifdef BUILD_MSTARTV
        if (bReplaceGraphicBuffer) {
            mSlots[buf].mAcquireCalled = false;     // clear to set buffer->mGraphicBuffer next time
        }
        #endif
        // MStar Android Patch End

        mSlots[buf].mNeedsCleanupOnRelease = false;
        mSlots[buf].mBufferState = BufferSlot::ACQUIRED;
        mSlots[buf].mFence = Fence::NO_FENCE;
    }

    mQueue.erase(front);
    mDequeueCondition.broadcast();

    ATRACE_INT(mConsumerName.string(), mQueue.size());

    return NO_ERROR;
}

status_t BufferQueue::releaseBuffer(
        int buf, uint64_t frameNumber, EGLDisplay display,
        EGLSyncKHR eglFence, const sp<Fence>& fence) {
    ATRACE_CALL();
    ATRACE_BUFFER_INDEX(buf);

    if (buf == INVALID_BUFFER_SLOT || fence == NULL) {
        return BAD_VALUE;
    }

    Mutex::Autolock _l(mMutex);

    // MStar Android Patch Begin
    #ifdef RENDER_IN_QUEUE_BUFFER
    if ((mConsumerName == "SurfaceView") && (mSlots[buf].mGraphicBuffer != NULL) && (mSlots[buf].mGraphicBuffer->getUsage() & GRALLOC_USAGE_PRIVATE_2)) {

    } else
    #endif
    // MStar Android Patch End
    // If the frame number has changed because buffer has been reallocated,
    // we can ignore this releaseBuffer for the old buffer.
    if (frameNumber != mSlots[buf].mFrameNumber) {
        return STALE_BUFFER_SLOT;
    }


    // MStar Android Patch Begin
    #ifdef RENDER_IN_QUEUE_BUFFER
    if ((mConsumerName == "SurfaceView") && (mSlots[buf].mGraphicBuffer != NULL) && (mSlots[buf].mGraphicBuffer->getUsage() & GRALLOC_USAGE_PRIVATE_2)) {

    } else {
    #endif
    // MStar Android Patch End
    // Internal state consistency checks:
    // Make sure this buffers hasn't been queued while we were owning it (acquired)
    Fifo::iterator front(mQueue.begin());
    Fifo::const_iterator const end(mQueue.end());
    while (front != end) {
        if (front->mBuf == buf) {
            LOG_ALWAYS_FATAL("[%s] received new buffer(#%lld) on slot #%d that has not yet been "
                    "acquired", mConsumerName.string(), frameNumber, buf);
            break; // never reached
        }
        front++;
    }
    // MStar Android Patch Begin
    #ifdef RENDER_IN_QUEUE_BUFFER
    }
    #endif
    // MStar Android Patch End


    // The buffer can now only be released if its in the acquired state
    if (mSlots[buf].mBufferState == BufferSlot::ACQUIRED) {
        mSlots[buf].mEglDisplay = display;
        mSlots[buf].mEglFence = eglFence;
        mSlots[buf].mFence = fence;
        mSlots[buf].mBufferState = BufferSlot::FREE;
    } else if (mSlots[buf].mNeedsCleanupOnRelease) {
        ST_LOGV("releasing a stale buf %d its state was %d", buf, mSlots[buf].mBufferState);
        mSlots[buf].mNeedsCleanupOnRelease = false;
        return STALE_BUFFER_SLOT;
    // MStar Android Patch Begin
    #ifdef RENDER_IN_QUEUE_BUFFER
    } else if ((mConsumerName == "SurfaceView") && (mSlots[buf].mBufferState == BufferSlot::FREE || mSlots[buf].mBufferState == BufferSlot::DEQUEUED ||
               mSlots[buf].mBufferState == BufferSlot::QUEUED)) {
        // OK Case for RENDER_IN_QUEUE_BUFFER
    #endif
    // MStar Android Patch End
    } else {
        ST_LOGE("attempted to release buf %d but its state was %d", buf, mSlots[buf].mBufferState);
        return -EINVAL;
    }

    mDequeueCondition.broadcast();
    return NO_ERROR;
}

status_t BufferQueue::consumerConnect(const sp<IConsumerListener>& consumerListener,
        bool controlledByApp) {
    ST_LOGV("consumerConnect controlledByApp=%s",
            controlledByApp ? "true" : "false");
    Mutex::Autolock lock(mMutex);

    if (mAbandoned) {
        ST_LOGE("consumerConnect: BufferQueue has been abandoned!");
        return NO_INIT;
    }
    if (consumerListener == NULL) {
        ST_LOGE("consumerConnect: consumerListener may not be NULL");
        return BAD_VALUE;
    }

    mConsumerListener = consumerListener;
    mConsumerControlledByApp = controlledByApp;

    return NO_ERROR;
}

status_t BufferQueue::consumerDisconnect() {
    ST_LOGV("consumerDisconnect");
    Mutex::Autolock lock(mMutex);

    if (mConsumerListener == NULL) {
        ST_LOGE("consumerDisconnect: No consumer is connected!");
        return -EINVAL;
    }

    mAbandoned = true;
    mConsumerListener = NULL;
    mQueue.clear();
    freeAllBuffersLocked();
    mDequeueCondition.broadcast();
    return NO_ERROR;
}

status_t BufferQueue::getReleasedBuffers(uint32_t* slotMask) {
    ST_LOGV("getReleasedBuffers");
    Mutex::Autolock lock(mMutex);

    if (mAbandoned) {
        ST_LOGE("getReleasedBuffers: BufferQueue has been abandoned!");
        return NO_INIT;
    }

    uint32_t mask = 0;
    for (int i = 0; i < NUM_BUFFER_SLOTS; i++) {
        if (!mSlots[i].mAcquireCalled) {
            mask |= 1 << i;
        }
    }

    // Remove buffers in flight (on the queue) from the mask where acquire has
    // been called, as the consumer will not receive the buffer address, so
    // it should not free these slots.
    Fifo::iterator front(mQueue.begin());
    while (front != mQueue.end()) {
        if (front->mAcquireCalled)
            mask &= ~(1 << front->mBuf);
        front++;
    }

    *slotMask = mask;

    ST_LOGV("getReleasedBuffers: returning mask %#x", mask);
    return NO_ERROR;
}

status_t BufferQueue::setDefaultBufferSize(uint32_t w, uint32_t h) {
    ST_LOGV("setDefaultBufferSize: w=%d, h=%d", w, h);
    if (!w || !h) {
        ST_LOGE("setDefaultBufferSize: dimensions cannot be 0 (w=%d, h=%d)",
                w, h);
        return BAD_VALUE;
    }

    Mutex::Autolock lock(mMutex);
    mDefaultWidth = w;
    mDefaultHeight = h;
    return NO_ERROR;
}

status_t BufferQueue::setDefaultMaxBufferCount(int bufferCount) {
    ATRACE_CALL();
    Mutex::Autolock lock(mMutex);
    return setDefaultMaxBufferCountLocked(bufferCount);
}

status_t BufferQueue::disableAsyncBuffer() {
    ATRACE_CALL();
    Mutex::Autolock lock(mMutex);
    if (mConsumerListener != NULL) {
        ST_LOGE("disableAsyncBuffer: consumer already connected!");
        return INVALID_OPERATION;
    }
    mUseAsyncBuffer = false;
    return NO_ERROR;
}

status_t BufferQueue::setMaxAcquiredBufferCount(int maxAcquiredBuffers) {
    ATRACE_CALL();
    Mutex::Autolock lock(mMutex);
    if (maxAcquiredBuffers < 1 || maxAcquiredBuffers > MAX_MAX_ACQUIRED_BUFFERS) {
        ST_LOGE("setMaxAcquiredBufferCount: invalid count specified: %d",
                maxAcquiredBuffers);
        return BAD_VALUE;
    }
    if (mConnectedApi != NO_CONNECTED_API) {
        return INVALID_OPERATION;
    }
    mMaxAcquiredBufferCount = maxAcquiredBuffers;
    return NO_ERROR;
}

int BufferQueue::getMinUndequeuedBufferCount(bool async) const {
    // if dequeueBuffer is allowed to error out, we don't have to
    // add an extra buffer.
    if (!mUseAsyncBuffer)
        return mMaxAcquiredBufferCount;

    // we're in async mode, or we want to prevent the app to
    // deadlock itself, we throw-in an extra buffer to guarantee it.
    if (mDequeueBufferCannotBlock || async)
        return mMaxAcquiredBufferCount+1;

    return mMaxAcquiredBufferCount;
}

int BufferQueue::getMinMaxBufferCountLocked(bool async) const {
    return getMinUndequeuedBufferCount(async) + 1;
}

int BufferQueue::getMaxBufferCountLocked(bool async) const {
    int minMaxBufferCount = getMinMaxBufferCountLocked(async);

    int maxBufferCount = mDefaultMaxBufferCount;
    if (maxBufferCount < minMaxBufferCount) {
        maxBufferCount = minMaxBufferCount;
    }
    if (mOverrideMaxBufferCount != 0) {
        assert(mOverrideMaxBufferCount >= minMaxBufferCount);
        maxBufferCount = mOverrideMaxBufferCount;
    }

    // Any buffers that are dequeued by the producer or sitting in the queue
    // waiting to be consumed need to have their slots preserved.  Such
    // buffers will temporarily keep the max buffer count up until the slots
    // no longer need to be preserved.
    for (int i = maxBufferCount; i < NUM_BUFFER_SLOTS; i++) {
        BufferSlot::BufferState state = mSlots[i].mBufferState;
        if (state == BufferSlot::QUEUED || state == BufferSlot::DEQUEUED) {
            maxBufferCount = i + 1;
        }
    }

    return maxBufferCount;
}

bool BufferQueue::stillTracking(const BufferItem *item) const {
    const BufferSlot &slot = mSlots[item->mBuf];

    ST_LOGV("stillTracking?: item: { slot=%d/%llu, buffer=%p }, "
            "slot: { slot=%d/%llu, buffer=%p }",
            item->mBuf, item->mFrameNumber,
            (item->mGraphicBuffer.get() ? item->mGraphicBuffer->handle : 0),
            item->mBuf, slot.mFrameNumber,
            (slot.mGraphicBuffer.get() ? slot.mGraphicBuffer->handle : 0));

    // Compare item with its original buffer slot.  We can check the slot
    // as the buffer would not be moved to a different slot by the producer.
    return (slot.mGraphicBuffer != NULL &&
            item->mGraphicBuffer->handle == slot.mGraphicBuffer->handle);
}

BufferQueue::ProxyConsumerListener::ProxyConsumerListener(
        const wp<ConsumerListener>& consumerListener):
        mConsumerListener(consumerListener) {}

BufferQueue::ProxyConsumerListener::~ProxyConsumerListener() {}

void BufferQueue::ProxyConsumerListener::onFrameAvailable() {
    sp<ConsumerListener> listener(mConsumerListener.promote());
    if (listener != NULL) {
        listener->onFrameAvailable();
    }
}

void BufferQueue::ProxyConsumerListener::onBuffersReleased() {
    sp<ConsumerListener> listener(mConsumerListener.promote());
    if (listener != NULL) {
        listener->onBuffersReleased();
    }
}

}; // namespace android
