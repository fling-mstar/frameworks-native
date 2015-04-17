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

#define LOG_TAG "GraphicBuffer"

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
// MStar Android Patch Begin
#include <sys/stat.h>
#include <fcntl.h>
// MStar Android Patch End

#include <utils/Errors.h>
#include <utils/Log.h>

#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/PixelFormat.h>

namespace android {

// ===========================================================================
// Buffer and implementation of ANativeWindowBuffer
// ===========================================================================

GraphicBuffer::GraphicBuffer()
    : BASE(), mOwner(ownData), mBufferMapper(GraphicBufferMapper::get()),
      mInitCheck(NO_ERROR) {
    width  = 
    height = 
    stride = 
    format = 
    usage  = 0;
    handle = NULL;
}

GraphicBuffer::GraphicBuffer(uint32_t w, uint32_t h, 
        PixelFormat reqFormat, uint32_t reqUsage)
    : BASE(), mOwner(ownData), mBufferMapper(GraphicBufferMapper::get()),
      mInitCheck(NO_ERROR)
{
    width  = 
    height = 
    stride = 
    format = 
    usage  = 0;
    handle = NULL;
    mInitCheck = initSize(w, h, reqFormat, reqUsage);
}

GraphicBuffer::GraphicBuffer(uint32_t w, uint32_t h,
        PixelFormat inFormat, uint32_t inUsage,
        uint32_t inStride, native_handle_t* inHandle, bool keepOwnership)
    : BASE(), mOwner(keepOwnership ? ownHandle : ownNone),
      mBufferMapper(GraphicBufferMapper::get()),
      mInitCheck(NO_ERROR)
{
    width  = w;
    height = h;
    stride = inStride;
    format = inFormat;
    usage  = inUsage;
    handle = inHandle;
}

GraphicBuffer::GraphicBuffer(ANativeWindowBuffer* buffer, bool keepOwnership)
    : BASE(), mOwner(keepOwnership ? ownHandle : ownNone),
      mBufferMapper(GraphicBufferMapper::get()),
      mInitCheck(NO_ERROR), mWrappedBuffer(buffer)
{
    width  = buffer->width;
    height = buffer->height;
    stride = buffer->stride;
    format = buffer->format;
    usage  = buffer->usage;
    handle = buffer->handle;
}

GraphicBuffer::~GraphicBuffer()
{
    if (handle) {
        free_handle();
    }
}

void GraphicBuffer::free_handle()
{
    if (mOwner == ownHandle) {
        mBufferMapper.unregisterBuffer(handle);
        native_handle_close(handle);
        native_handle_delete(const_cast<native_handle*>(handle));
    } else if (mOwner == ownData) {
        GraphicBufferAllocator& allocator(GraphicBufferAllocator::get());
        allocator.free(handle);
    }
    mWrappedBuffer = 0;
}

status_t GraphicBuffer::initCheck() const {
    return mInitCheck;
}

void GraphicBuffer::dumpAllocationsToSystemLog()
{
    GraphicBufferAllocator::dumpToSystemLog();
}

ANativeWindowBuffer* GraphicBuffer::getNativeBuffer() const
{
    return static_cast<ANativeWindowBuffer*>(
            const_cast<GraphicBuffer*>(this));
}

status_t GraphicBuffer::reallocate(uint32_t w, uint32_t h, PixelFormat f,
        uint32_t reqUsage)
{
    if (mOwner != ownData)
        return INVALID_OPERATION;

    if (handle && w==width && h==height && f==format && reqUsage==usage)
        return NO_ERROR;

    if (handle) {
        GraphicBufferAllocator& allocator(GraphicBufferAllocator::get());
        allocator.free(handle);
        handle = 0;
    }
    return initSize(w, h, f, reqUsage);
}

status_t GraphicBuffer::initSize(uint32_t w, uint32_t h, PixelFormat format,
        uint32_t reqUsage)
{
    GraphicBufferAllocator& allocator = GraphicBufferAllocator::get();
    status_t err = allocator.alloc(w, h, format, reqUsage, &handle, &stride);
    if (err == NO_ERROR) {
        this->width  = w;
        this->height = h;
        this->format = format;
        this->usage  = reqUsage;
    }
    return err;
}

status_t GraphicBuffer::lock(uint32_t usage, void** vaddr)
{
    const Rect lockBounds(width, height);
    status_t res = lock(usage, lockBounds, vaddr);
    return res;
}

status_t GraphicBuffer::lock(uint32_t usage, const Rect& rect, void** vaddr)
{
    if (rect.left < 0 || rect.right  > this->width || 
        rect.top  < 0 || rect.bottom > this->height) {
        ALOGE("locking pixels (%d,%d,%d,%d) outside of buffer (w=%d, h=%d)",
                rect.left, rect.top, rect.right, rect.bottom, 
                this->width, this->height);
        return BAD_VALUE;
    }
    status_t res = getBufferMapper().lock(handle, usage, rect, vaddr);
    return res;
}

status_t GraphicBuffer::lockYCbCr(uint32_t usage, android_ycbcr *ycbcr)
{
    const Rect lockBounds(width, height);
    status_t res = lockYCbCr(usage, lockBounds, ycbcr);
    return res;
}

status_t GraphicBuffer::lockYCbCr(uint32_t usage, const Rect& rect,
        android_ycbcr *ycbcr)
{
    if (rect.left < 0 || rect.right  > this->width ||
        rect.top  < 0 || rect.bottom > this->height) {
        ALOGE("locking pixels (%d,%d,%d,%d) outside of buffer (w=%d, h=%d)",
                rect.left, rect.top, rect.right, rect.bottom,
                this->width, this->height);
        return BAD_VALUE;
    }
    status_t res = getBufferMapper().lockYCbCr(handle, usage, rect, ycbcr);
    return res;
}

status_t GraphicBuffer::unlock()
{
    status_t res = getBufferMapper().unlock(handle);
    return res;
}

size_t GraphicBuffer::getFlattenedSize() const {
    return (8 + (handle ? handle->numInts : 0))*sizeof(int);
}

size_t GraphicBuffer::getFdCount() const {
    return handle ? handle->numFds : 0;
}

status_t GraphicBuffer::flatten(void*& buffer, size_t& size, int*& fds, size_t& count) const {
    size_t sizeNeeded = GraphicBuffer::getFlattenedSize();
    if (size < sizeNeeded) return NO_MEMORY;

    size_t fdCountNeeded = GraphicBuffer::getFdCount();
    if (count < fdCountNeeded) return NO_MEMORY;

    int* buf = static_cast<int*>(buffer);
    buf[0] = 'GBFR';
    buf[1] = width;
    buf[2] = height;
    buf[3] = stride;
    buf[4] = format;
    buf[5] = usage;
    buf[6] = 0;
    buf[7] = 0;

    if (handle) {
        buf[6] = handle->numFds;
        buf[7] = handle->numInts;
        native_handle_t const* const h = handle;
        memcpy(fds,     h->data,             h->numFds*sizeof(int));
        memcpy(&buf[8], h->data + h->numFds, h->numInts*sizeof(int));
    }

    buffer = reinterpret_cast<void*>(static_cast<int*>(buffer) + sizeNeeded);
    size -= sizeNeeded;
    fds += handle->numFds;
    count -= handle->numFds;

    return NO_ERROR;
}

status_t GraphicBuffer::unflatten(
        void const*& buffer, size_t& size, int const*& fds, size_t& count) {
    if (size < 8*sizeof(int)) return NO_MEMORY;

    int const* buf = static_cast<int const*>(buffer);
    if (buf[0] != 'GBFR') return BAD_TYPE;

    const size_t numFds  = buf[6];
    const size_t numInts = buf[7];

    const size_t sizeNeeded = (8 + numInts) * sizeof(int);
    if (size < sizeNeeded) return NO_MEMORY;

    size_t fdCountNeeded = 0;
    if (count < fdCountNeeded) return NO_MEMORY;

    if (handle) {
        // free previous handle if any
        free_handle();
    }

    if (numFds || numInts) {
        width  = buf[1];
        height = buf[2];
        stride = buf[3];
        format = buf[4];
        usage  = buf[5];
        native_handle* h = native_handle_create(numFds, numInts);
        memcpy(h->data,          fds,     numFds*sizeof(int));
        memcpy(h->data + numFds, &buf[8], numInts*sizeof(int));
        handle = h;
    } else {
        width = height = stride = format = usage = 0;
        handle = NULL;
    }

    mOwner = ownHandle;

    if (handle != 0) {
        status_t err = mBufferMapper.registerBuffer(handle);
        if (err != NO_ERROR) {
            ALOGE("unflatten: registerBuffer failed: %s (%d)",
                    strerror(-err), err);
            return err;
        }
    }

    buffer = reinterpret_cast<void const*>(static_cast<int const*>(buffer) + sizeNeeded);
    size -= sizeNeeded;
    fds += numFds;
    count -= numFds;

    return NO_ERROR;
}

// ---------------------------------------------------------------------------
// MStar Android Patch Begin
bool graphic_buffer_dump_helper::dump_convert_to_rgb24(PixelFormat format,   unsigned char *src8,  unsigned char*dest8, int w) {
    int i=0;
    int j=0;

    while (j < w*3) {
        switch (format) {
            case PIXEL_FORMAT_RGBA_8888:
            case HAL_PIXEL_FORMAT_RGBX_8888:
                dest8[j+0] = src8[i+0];
                dest8[j+1] = src8[i+1];
                dest8[j+2] = src8[i+2];
                i+=4;
                j+=3;
                break;
            case PIXEL_FORMAT_BGRA_8888:
                dest8[j+0] = src8[i+2];
                dest8[j+1] = src8[i+1];
                dest8[j+2] = src8[i+0];
                i+=4;
                j+=3;
                break;
            case HAL_PIXEL_FORMAT_RGB_888:
                dest8[j+0] = src8[i+2];
                dest8[j+1] = src8[i+1];
                dest8[j+2] = src8[i+0];
                i+=3;
                j+=3;
                break;
            case HAL_PIXEL_FORMAT_RGB_565:
               {
                    const unsigned int RGB565_MASK_RED = 0xF800;
                    const unsigned int RGB565_MASK_GREEN = 0x07E0;
                    const unsigned int RGB565_MASK_BLUE = 0x001F;
                    unsigned int rgb565 = ((unsigned int)src8[i+1]<<8)|src8[i+0];

                    dest8[j+2] = (rgb565 & RGB565_MASK_RED) >> 11;
                    dest8[j+1] = (rgb565 & RGB565_MASK_GREEN) >> 5;
                    dest8[j+0] = (rgb565 & RGB565_MASK_BLUE);

                    //amplify the image
                    dest8[j+2] = (dest8[2] << 3)|(dest8[2]>>2);
                    dest8[j+1] = (dest8[1] <<2)|(dest8[1]>>4);
                    dest8[j+0] = (dest8[0] << 3)|(dest8[0]>>2);
                    i+=2;
                    j+=3;
                }
                break;
            case PIXEL_FORMAT_RGBA_4444:
                dest8[j+0] =  ((src8[i+0]&0xf0)>>4)| (src8[i+0]&0xf0);
                dest8[j+1] = ((src8[i+1]&0x0f)<<4)| (src8[i+1]&0x0f);
                dest8[j+2] =  ((src8[i+1]&0xf0)>>4)| (src8[i+1]&0xf0);
                i+=2;
                j+=3;
                break;
           default:
               ALOGE("----->dump_convert_to_rgb24, invalid format!\n");
               return false;
        }
    }
    return true;
}

bool graphic_buffer_dump_helper::dump_convert_to_a8(PixelFormat format, unsigned char *src8, unsigned char*dest8, int w) {
    int i = 0, j = 0;

    while (j < w) {
        switch (format) {
            case PIXEL_FORMAT_RGBA_8888:
            case PIXEL_FORMAT_BGRA_8888:
                dest8[j] = src8[i+3];
                i+=4;
                j+=1;
                break;
            case PIXEL_FORMAT_RGBA_4444:
                dest8[j] = ((src8[i+0]&0x0f)<<4)| (src8[i+0]&0x0f);
                i+=2;
                j+=1;
            default:
                ALOGE("   ----->dump_convert_to_rgb24, invalid format!\n");
                return false;
        }
    }
    return true;
}
graphic_buffer_dump_helper *graphic_buffer_dump_helper::m_instance=NULL;
graphic_buffer_dump_helper* graphic_buffer_dump_helper::GetInstance()
{
    if (m_instance) {
        return m_instance;
    }
    m_instance = new graphic_buffer_dump_helper();
    return m_instance;
}
bool graphic_buffer_dump_helper::dump_graphic_buffer_if_needed(const sp<GraphicBuffer>& buffer) {
    int num  = -1;
    int fd_p = -1;
    int fd_g = -1;
    int i;
    graphic_buffer_dump_helper *self = GetInstance();
    if (!self || !self->m_enabled) {
        return false;
    }
    int len =  strlen(self->m_dump_directory) +  strlen(self->m_dump_prefix) + 40;
    char filename[len];
    char head[30];
    bool rgb = false;
    bool alpha = false;
    void *addr;
    int dumpWidth = buffer->getWidth();
    int dumpHeight = buffer->getHeight();
    int dumpStride;


    /* Check pixel format. */
    switch (buffer->getPixelFormat()) {
        case PIXEL_FORMAT_RGBA_8888:
        case PIXEL_FORMAT_BGRA_8888:
        case PIXEL_FORMAT_RGBA_4444:
            alpha = true;
            rgb = true;
            break;
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_RGB_888:
        case HAL_PIXEL_FORMAT_RGB_565:
            alpha = false;
            rgb = true;
            break;
        default:
            ALOGE("----->Unsupport color format[%d], can not dump layer buffer!\n", buffer->getPixelFormat());
            return false;
    }
    dumpStride = buffer->getStride()*bytesPerPixel(buffer->getPixelFormat());

    status_t ret = buffer->lock(GRALLOC_USAGE_SW_READ_MASK, &addr);
    if (ret != 0) {
        ALOGE("         mActiveBuffer->lock(...) failed: %d\n", ret);
        return false;
    }

    if (self->m_dump_prefix[0]) {
        /* Find the lowest unused index. */
        while (++num < 10000) {
            snprintf(filename, len, "%s/%s_%04d.ppm",
                     self->m_dump_directory, self->m_dump_prefix, num);

            if (access(filename, F_OK) != 0) {
                snprintf(filename, len, "%s/%s_%04d.pgm",
                         self->m_dump_directory, self->m_dump_prefix, num);

                if (access(filename, F_OK) != 0)
                    break;
            }
        }

        if (num == 10000) {
            ALOGE("   ---->"
                          "couldn't find an unused index for buffer dump!\n");
            buffer->unlock();
            return false;
        }
    }

    /* Create a file with the found index. */
    if  (rgb) {
        if (self->m_dump_prefix[0]) {
            snprintf(filename, len, "%s/%s_%04d.ppm", self->m_dump_directory, self->m_dump_prefix, num);
        } else {
            snprintf(filename, len, "%s.ppm", self->m_dump_directory);
        }

        fd_p = open(filename, O_EXCL | O_CREAT | O_WRONLY, 0644);
        if (fd_p < 0) {
            ALOGE("Dump Layer Buffercould not open %s!\n", filename);
            buffer->unlock();
            return false;
        } else {
            ALOGE("Dump graphic buffer color channel to:%s",filename);
        }
    }

    /* Create a graymap for the alpha channel using the found index. */
    if (alpha) {
        if (self->m_dump_prefix[0]) {
            snprintf(filename, len, "%s/%s_%04d.pgm", self->m_dump_directory, self->m_dump_prefix, num);
        } else {
            snprintf(filename, len, "%s.pgm", self->m_dump_directory);
         }

        fd_g = open(filename, O_EXCL | O_CREAT | O_WRONLY, 0644);
        if (fd_g < 0) {
            ALOGE("Dump Layer Buffer"
                                "could not open %s!\n", filename);

            buffer->unlock();

            if (rgb) {
                close(fd_p);
                snprintf(filename, len, "%s/%s_%04d.ppm",
                         self->m_dump_directory, self->m_dump_prefix, num);
                unlink(filename);
            }

            return false;
        } else {
            ALOGE("Dump graphic buffer alpha channel to:%s",filename);
        }
    }

    if (rgb) {
        /* Write the pixmap header. */
        snprintf(head, 30,
                 "P6\n%d %d\n255\n", dumpWidth, dumpHeight);

        write(fd_p, head, strlen(head));
    }

    /* Write the graymap header. */
    if (alpha) {
        snprintf(head, 30,
                 "P5\n%d %d\n255\n", dumpWidth, dumpHeight);
        write(fd_g, head, strlen(head));
    }

    /* Write the pixmap (and graymap) data. */
    for (i=0; i<dumpHeight; i++) {
        /* Prepare one row. */
        unsigned char *src8 = ((unsigned char*)addr)+i*dumpStride;

        /* Write color buffer to pixmap file. */
        if (rgb) {
            unsigned char buf_p[dumpWidth * 3];

            self->dump_convert_to_rgb24(buffer->getPixelFormat(), src8, buf_p, dumpWidth);

            write(fd_p, buf_p, dumpWidth * 3);
        }

        /* Write alpha buffer to graymap file. */
        if (alpha) {
            unsigned char buf_g[dumpWidth];
            self->dump_convert_to_a8(buffer->getPixelFormat(), src8, buf_g, dumpWidth);
            write(fd_g, buf_g, dumpWidth);
        }
    }

    /* Unlock the surface buffer. */
     buffer->unlock();

    /* Close pixmap file. */
    if (rgb)
        close(fd_p);

    /* Close graymap file. */
    if (alpha)
        close(fd_g);

    return true;
}
void graphic_buffer_dump_helper::enable_dump_graphic_buffer(bool enable, const char *directory)
{
    graphic_buffer_dump_helper *self = GetInstance();
    if (!self) {
        return;
    }
    if (!directory) {
        self->m_enabled = false;
    } else {
        self->m_enabled = enable;
    }

    if (self->m_enabled) {
        ALOGE("Enable graphic buffer dump to:%s",directory);
        strncpy(self->m_dump_directory, directory, 256);
        self->m_dump_directory[255] = 0;
    }
}
void graphic_buffer_dump_helper::set_prefix(const char *prefix)
{
    graphic_buffer_dump_helper *self = GetInstance();
    if (!self) {
        return;
    }
    if (!prefix) {
        self->m_dump_prefix[0] = 0;
    } else {
        strncpy(self->m_dump_prefix, prefix, 256);
        self->m_dump_prefix[255] = 0;
        correct_filename(self->m_dump_prefix);
    }
}
graphic_buffer_dump_helper::graphic_buffer_dump_helper()
{
    m_enabled = false;
    m_dump_prefix[0] = 0;
}
graphic_buffer_dump_helper::~graphic_buffer_dump_helper()
{
}

// MStar Android Patch End
}; // namespace android
