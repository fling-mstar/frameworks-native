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

// tag as surfaceflinger
#define LOG_TAG "SurfaceFlinger"

#include <stdint.h>
#include <sys/types.h>

#include <binder/Parcel.h>
#include <binder/IMemory.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>

#include <gui/BitTube.h>
#include <gui/IDisplayEventConnection.h>
#include <gui/ISurfaceComposer.h>
#include <gui/IGraphicBufferProducer.h>

#include <private/gui/LayerState.h>

#include <ui/DisplayInfo.h>

#include <utils/Log.h>

// ---------------------------------------------------------------------------

namespace android {

class IDisplayEventConnection;

class BpSurfaceComposer : public BpInterface<ISurfaceComposer>
{
public:
    BpSurfaceComposer(const sp<IBinder>& impl)
        : BpInterface<ISurfaceComposer>(impl)
    {
    }

    virtual sp<ISurfaceComposerClient> createConnection()
    {
        uint32_t n;
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::CREATE_CONNECTION, data, &reply);
        return interface_cast<ISurfaceComposerClient>(reply.readStrongBinder());
    }

    virtual sp<IGraphicBufferAlloc> createGraphicBufferAlloc()
    {
        uint32_t n;
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::CREATE_GRAPHIC_BUFFER_ALLOC, data, &reply);
        return interface_cast<IGraphicBufferAlloc>(reply.readStrongBinder());
    }

    virtual void setTransactionState(
            const Vector<ComposerState>& state,
            const Vector<DisplayState>& displays,
            uint32_t flags)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        {
            Vector<ComposerState>::const_iterator b(state.begin());
            Vector<ComposerState>::const_iterator e(state.end());
            data.writeInt32(state.size());
            for ( ; b != e ; ++b ) {
                b->write(data);
            }
        }
        {
            Vector<DisplayState>::const_iterator b(displays.begin());
            Vector<DisplayState>::const_iterator e(displays.end());
            data.writeInt32(displays.size());
            for ( ; b != e ; ++b ) {
                b->write(data);
            }
        }
        data.writeInt32(flags);
        remote()->transact(BnSurfaceComposer::SET_TRANSACTION_STATE, data, &reply);
    }

    virtual void bootFinished()
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::BOOT_FINISHED, data, &reply);
    }

    virtual status_t captureScreen(const sp<IBinder>& display,
            const sp<IGraphicBufferProducer>& producer,
            uint32_t reqWidth, uint32_t reqHeight,
            uint32_t minLayerZ, uint32_t maxLayerZ)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeStrongBinder(display);
        data.writeStrongBinder(producer->asBinder());
        data.writeInt32(reqWidth);
        data.writeInt32(reqHeight);
        data.writeInt32(minLayerZ);
        data.writeInt32(maxLayerZ);
        remote()->transact(BnSurfaceComposer::CAPTURE_SCREEN, data, &reply);
        return reply.readInt32();
    }

    virtual bool authenticateSurfaceTexture(
            const sp<IGraphicBufferProducer>& bufferProducer) const
    {
        Parcel data, reply;
        int err = NO_ERROR;
        err = data.writeInterfaceToken(
                ISurfaceComposer::getInterfaceDescriptor());
        if (err != NO_ERROR) {
            ALOGE("ISurfaceComposer::authenticateSurfaceTexture: error writing "
                    "interface descriptor: %s (%d)", strerror(-err), -err);
            return false;
        }
        err = data.writeStrongBinder(bufferProducer->asBinder());
        if (err != NO_ERROR) {
            ALOGE("ISurfaceComposer::authenticateSurfaceTexture: error writing "
                    "strong binder to parcel: %s (%d)", strerror(-err), -err);
            return false;
        }
        err = remote()->transact(BnSurfaceComposer::AUTHENTICATE_SURFACE, data,
                &reply);
        if (err != NO_ERROR) {
            ALOGE("ISurfaceComposer::authenticateSurfaceTexture: error "
                    "performing transaction: %s (%d)", strerror(-err), -err);
            return false;
        }
        int32_t result = 0;
        err = reply.readInt32(&result);
        if (err != NO_ERROR) {
            ALOGE("ISurfaceComposer::authenticateSurfaceTexture: error "
                    "retrieving result: %s (%d)", strerror(-err), -err);
            return false;
        }
        return result != 0;
    }

    virtual sp<IDisplayEventConnection> createDisplayEventConnection()
    {
        Parcel data, reply;
        sp<IDisplayEventConnection> result;
        int err = data.writeInterfaceToken(
                ISurfaceComposer::getInterfaceDescriptor());
        if (err != NO_ERROR) {
            return result;
        }
        err = remote()->transact(
                BnSurfaceComposer::CREATE_DISPLAY_EVENT_CONNECTION,
                data, &reply);
        if (err != NO_ERROR) {
            ALOGE("ISurfaceComposer::createDisplayEventConnection: error performing "
                    "transaction: %s (%d)", strerror(-err), -err);
            return result;
        }
        result = interface_cast<IDisplayEventConnection>(reply.readStrongBinder());
        return result;
    }

    virtual sp<IBinder> createDisplay(const String8& displayName, bool secure)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeString8(displayName);
        data.writeInt32(secure ? 1 : 0);
        remote()->transact(BnSurfaceComposer::CREATE_DISPLAY, data, &reply);
        return reply.readStrongBinder();
    }

    virtual void destroyDisplay(const sp<IBinder>& display)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeStrongBinder(display);
        remote()->transact(BnSurfaceComposer::DESTROY_DISPLAY, data, &reply);
    }

    virtual sp<IBinder> getBuiltInDisplay(int32_t id)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeInt32(id);
        remote()->transact(BnSurfaceComposer::GET_BUILT_IN_DISPLAY, data, &reply);
        return reply.readStrongBinder();
    }

    virtual void blank(const sp<IBinder>& display)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeStrongBinder(display);
        remote()->transact(BnSurfaceComposer::BLANK, data, &reply);
    }

    virtual void unblank(const sp<IBinder>& display)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeStrongBinder(display);
        remote()->transact(BnSurfaceComposer::UNBLANK, data, &reply);
    }

    virtual status_t getDisplayInfo(const sp<IBinder>& display, DisplayInfo* info)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeStrongBinder(display);
        remote()->transact(BnSurfaceComposer::GET_DISPLAY_INFO, data, &reply);
        memcpy(info, reply.readInplace(sizeof(DisplayInfo)), sizeof(DisplayInfo));
        return reply.readInt32();
    }

    // MStar Android Patch Begin
    virtual status_t setAutoStereoMode(int32_t identity, int32_t autoStereo)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeInt32(identity);
        data.writeInt32(autoStereo);
        remote()->transact(BnSurfaceComposer::SET_AUTO_STEREO_MODE, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setBypassTransformMode(int32_t identity, int32_t bypassTransform)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeInt32(identity);
        data.writeInt32(bypassTransform);
        remote()->transact(BnSurfaceComposer::SET_BYPASS_TRANSFORM_MODE, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setPanelMode(int32_t panelMode)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeInt32(panelMode);
        remote()->transact(BnSurfaceComposer::SET_PANEL_MODE, data, &reply);
        return reply.readInt32();
    }

    virtual status_t getPanelMode(int32_t* panelMode)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::GET_PANEL_MODE, data, &reply);
        memcpy(panelMode, reply.readInplace(sizeof(int32_t)), sizeof(int32_t));
        return reply.readInt32();
    }

    virtual status_t setGopStretchWin(int32_t gopNo, int32_t dest_Width, int32_t dest_Height)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeInt32(gopNo);
        data.writeInt32(dest_Width);
        data.writeInt32(dest_Height);
        remote()->transact(BnSurfaceComposer::SET_GOP_STRETCH_WIN, data,&reply);
        return reply.readInt32();
    }

    virtual status_t setSurfaceResolutionMode(int32_t width, int32_t height, int32_t hstart, int32_t interleave, int32_t orientation, int32_t value)
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeInt32(width);
        data.writeInt32(height);
        data.writeInt32(hstart);
        data.writeInt32(interleave);
        data.writeInt32(orientation);
        data.writeInt32(value);
        remote()->transact(BnSurfaceComposer::SET_SURFACE_RESOLUTION_MODE, data, &reply);
        return reply.readInt32();
    }

#ifdef ENABLE_HWCURSOR
    virtual status_t setHwCursorShow()
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::SET_HWCURSOR_SHOW, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setHwCursorHide()
    {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::SET_HWCURSOR_HIDE, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setHwCursorMatrix(float dsdx, float dtdx, float dsdy, float dtdy)
   {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeFloat(dsdx);
        data.writeFloat(dtdx);
        data.writeFloat(dsdy);
        data.writeFloat(dtdy);
        remote()->transact(BnSurfaceComposer::SET_HWCURSOR_MATRIX, data, &reply);
        return reply.readInt32();
   }

   virtual status_t setHwCursorPosition(float positionX, float positionY, float hotSpotX, float hotSpotY, int iconWidth, int iconHeight)
   {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeFloat(positionX);
        data.writeFloat(positionY);
        data.writeFloat(hotSpotX);
        data.writeFloat(hotSpotY);
        data.writeInt32(iconWidth);
        data.writeInt32(iconHeight);
        remote()->transact(BnSurfaceComposer::SET_HWCURSOR_POSITION, data, &reply);
        return reply.readInt32();
   }

   virtual status_t setHwCursorAlpha(float alpha)
   {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        data.writeFloat(alpha);
        remote()->transact(BnSurfaceComposer::SET_HWCURSOR_ALPHA, data, &reply);
        return reply.readInt32();
   }

    virtual status_t changeHwCursorIcon()
   {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::CHANGE_HWCURSOR_ICON, data, &reply);
        return reply.readInt32();
   }

   virtual status_t doHwCursorTransaction()
   {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::DO_HWCURSOR_TRANSACTION, data, &reply);
        return reply.readInt32();
   }

   virtual sp<IMemory> getHwCursorIconBuf()
   {
        Parcel data,reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::GET_HWCURSOR_ICONBUF, data, &reply);

        sp<IMemory> buffer = interface_cast<IMemory>(reply.readStrongBinder());
        return buffer;
   }

   virtual status_t getHwCursorWidth(int32_t* cursorWidth)
   {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::GET_HWCURSOR_WIDTH, data, &reply);
        memcpy(cursorWidth, reply.readInplace(sizeof(int32_t)), sizeof(int32_t));
        return reply.readInt32();
   }

   virtual status_t getHwCursorHeight(int32_t* cursorHeight)
   {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::GET_HWCURSOR_HEIGHT, data, &reply);
        memcpy(cursorHeight, reply.readInplace(sizeof(int32_t)), sizeof(int32_t));
        return reply.readInt32();
   }

   virtual status_t getHwCursorStride(int32_t* cursorStride)
   {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::GET_HWCURSOR_STRIDE, data, &reply);
        memcpy(cursorStride, reply.readInplace(sizeof(int32_t)), sizeof(int32_t));
        return reply.readInt32();
   }

   virtual status_t loadHwCursorModule() {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());
        remote()->transact(BnSurfaceComposer::LOAD_HWCURSOR_MODULE, data, &reply);
        return reply.readInt32();
   }
#endif
    // MStar Android Patch End
};

IMPLEMENT_META_INTERFACE(SurfaceComposer, "android.ui.ISurfaceComposer");

// ----------------------------------------------------------------------

status_t BnSurfaceComposer::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    switch(code) {
        case CREATE_CONNECTION: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            sp<IBinder> b = createConnection()->asBinder();
            reply->writeStrongBinder(b);
            return NO_ERROR;
        }
        case CREATE_GRAPHIC_BUFFER_ALLOC: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            sp<IBinder> b = createGraphicBufferAlloc()->asBinder();
            reply->writeStrongBinder(b);
            return NO_ERROR;
        }
        case SET_TRANSACTION_STATE: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            size_t count = data.readInt32();
            ComposerState s;
            Vector<ComposerState> state;
            state.setCapacity(count);
            for (size_t i=0 ; i<count ; i++) {
                s.read(data);
                state.add(s);
            }
            count = data.readInt32();
            DisplayState d;
            Vector<DisplayState> displays;
            displays.setCapacity(count);
            for (size_t i=0 ; i<count ; i++) {
                d.read(data);
                displays.add(d);
            }
            uint32_t flags = data.readInt32();
            setTransactionState(state, displays, flags);
            return NO_ERROR;
        }
        case BOOT_FINISHED: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            bootFinished();
            return NO_ERROR;
        }
        case CAPTURE_SCREEN: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            sp<IBinder> display = data.readStrongBinder();
            sp<IGraphicBufferProducer> producer =
                    interface_cast<IGraphicBufferProducer>(data.readStrongBinder());
            uint32_t reqWidth = data.readInt32();
            uint32_t reqHeight = data.readInt32();
            uint32_t minLayerZ = data.readInt32();
            uint32_t maxLayerZ = data.readInt32();
            status_t res = captureScreen(display, producer,
                    reqWidth, reqHeight, minLayerZ, maxLayerZ);
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case AUTHENTICATE_SURFACE: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            sp<IGraphicBufferProducer> bufferProducer =
                    interface_cast<IGraphicBufferProducer>(data.readStrongBinder());
            int32_t result = authenticateSurfaceTexture(bufferProducer) ? 1 : 0;
            reply->writeInt32(result);
            return NO_ERROR;
        }
        case CREATE_DISPLAY_EVENT_CONNECTION: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            sp<IDisplayEventConnection> connection(createDisplayEventConnection());
            reply->writeStrongBinder(connection->asBinder());
            return NO_ERROR;
        }
        case CREATE_DISPLAY: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            String8 displayName = data.readString8();
            bool secure = bool(data.readInt32());
            sp<IBinder> display(createDisplay(displayName, secure));
            reply->writeStrongBinder(display);
            return NO_ERROR;
        }
        case DESTROY_DISPLAY: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            sp<IBinder> display = data.readStrongBinder();
            destroyDisplay(display);
            return NO_ERROR;
        }
        case GET_BUILT_IN_DISPLAY: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            int32_t id = data.readInt32();
            sp<IBinder> display(getBuiltInDisplay(id));
            reply->writeStrongBinder(display);
            return NO_ERROR;
        }
        case BLANK: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            sp<IBinder> display = data.readStrongBinder();
            blank(display);
            return NO_ERROR;
        }
        case UNBLANK: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            sp<IBinder> display = data.readStrongBinder();
            unblank(display);
            return NO_ERROR;
        }
        case GET_DISPLAY_INFO: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            DisplayInfo info;
            sp<IBinder> display = data.readStrongBinder();
            status_t result = getDisplayInfo(display, &info);
            memcpy(reply->writeInplace(sizeof(DisplayInfo)), &info, sizeof(DisplayInfo));
            reply->writeInt32(result);
            return NO_ERROR;
        }
        // MStar Android Patch Begin
        case SET_AUTO_STEREO_MODE: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            int32_t identity = data.readInt32();
            int32_t autoStereo = data.readInt32();
            status_t res = setAutoStereoMode(identity, autoStereo);
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case SET_BYPASS_TRANSFORM_MODE: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            int32_t identity = data.readInt32();
            int32_t bypassTransform = data.readInt32();
            status_t res = setBypassTransformMode(identity, bypassTransform);
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case SET_PANEL_MODE: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            int32_t panelMode = data.readInt32();
            status_t res = setPanelMode(panelMode);
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case SET_SURFACE_RESOLUTION_MODE: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            int32_t width = data.readInt32();
            int32_t height = data.readInt32();
            int32_t hstart = data.readInt32();
            int32_t interleave=data.readInt32();
            int32_t orientation = data.readInt32();
            int32_t value=data.readInt32();
            status_t res = setSurfaceResolutionMode(width, height,hstart,interleave,orientation,value);
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case GET_PANEL_MODE: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            int32_t panelMode;
            status_t res = getPanelMode(&panelMode);
            memcpy(reply->writeInplace(sizeof(int32_t)), &panelMode, sizeof(int32_t));
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case  SET_GOP_STRETCH_WIN: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            int32_t gopNo = data.readInt32();
            int32_t destWidth = data.readInt32();
            int32_t destHeight = data.readInt32();
            status_t res = setGopStretchWin(gopNo, destWidth, destHeight);
            reply->writeInt32(res);
            return NO_ERROR;
        }
#ifdef ENABLE_HWCURSOR
        case SET_HWCURSOR_SHOW: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            status_t res = setHwCursorShow();
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case SET_HWCURSOR_HIDE: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            status_t res = setHwCursorHide();
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case SET_HWCURSOR_MATRIX: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            float dsdx =  data.readFloat();
            float dtdx = data.readFloat();
            float dsdy = data.readFloat();
            float dtdy = data.readFloat();
            status_t res = setHwCursorMatrix(dsdx, dtdx, dsdy, dtdy);
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case SET_HWCURSOR_POSITION: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            float positionX = data.readFloat();
            float positionY = data.readFloat();
            float hotSpotX = data.readFloat();
            float hotSpotY = data.readFloat();
            int32_t iconWidth = data.readInt32();
            int32_t iconHeight = data.readInt32();
            status_t res = setHwCursorPosition(positionX, positionY, hotSpotX, hotSpotY, iconWidth, iconHeight);
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case SET_HWCURSOR_ALPHA: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            float alpha = data.readFloat();
            status_t res = setHwCursorAlpha(alpha);
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case CHANGE_HWCURSOR_ICON: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            status_t res = changeHwCursorIcon();
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case DO_HWCURSOR_TRANSACTION: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            status_t res = doHwCursorTransaction();
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case GET_HWCURSOR_ICONBUF: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            sp<IMemory> buffer = getHwCursorIconBuf();
            if ( buffer != NULL ) {
                reply->writeStrongBinder(buffer->asBinder());
            } else {
                ALOGE("HWCURSOR_GET_ICONBUF request can not get Shared Memory!");
            }
            return NO_ERROR;
        }
        case GET_HWCURSOR_WIDTH: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            int32_t cursorWidth;
            status_t res = getHwCursorWidth(&cursorWidth);
            memcpy(reply->writeInplace(sizeof(int32_t)), &cursorWidth, sizeof(int32_t));
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case GET_HWCURSOR_HEIGHT: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            int32_t cursorHeight;
            status_t res = getHwCursorHeight(&cursorHeight);
            memcpy(reply->writeInplace(sizeof(int32_t)), &cursorHeight, sizeof(int32_t));
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case GET_HWCURSOR_STRIDE: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            int32_t cursorStride;
            status_t res = getHwCursorStride(&cursorStride);
            memcpy(reply->writeInplace(sizeof(int32_t)), &cursorStride, sizeof(int32_t));
            reply->writeInt32(res);
            return NO_ERROR;
        }
        case LOAD_HWCURSOR_MODULE: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);
            status_t res = loadHwCursorModule();
            reply->writeInt32(res);
            return NO_ERROR;
        }
#endif
        // MStar Android Patch End
        default: {
            return BBinder::onTransact(code, data, reply, flags);
        }
    }
    // should be unreachable
    return NO_ERROR;
}

// ----------------------------------------------------------------------------

};
