/*
 * Copyright © 2012 Intel Corporation
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Jackie Li <yaodong.li@intel.com>
 *
 */
#ifndef __INTEL_DISPLAY_DEVICE_CPP__
#define __INTEL_DISPLAY_DEVICE_CPP__

#include <EGL/egl.h>

#include <IntelHWComposerDrm.h>
#include <IntelBufferManager.h>
#include <IntelHWComposerLayer.h>
#include <IntelHWComposerDump.h>

class IntelDisplayConfig {
private:
    uint32_t mRefreshRate;
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mDpiX;
    uint32_t mDpiY;
    uint32_t mFormat;
    uint32_t mFlag;   //Interlaced;
public:
    uint32_t getRefreshRate() { return mRefreshRate; }
    uint32_t getWidth() { return mWidth; }
    uint32_t getHeight() { return mHeight; }
    uint32_t getDpiX() { return mDpiX; }
    uint32_t getDpiY() { return mDpiY; }
    uint32_t getFormat() { return mFormat; }
    uint32_t getFlag() { return mFlag; }
};


class IntelDisplayVsync {
//vsync is also need to be handled by device;
//FIXME: IMPLEMENT IT LATER
};


class IntelDisplayDevice : public IntelHWComposerDump {
protected:
    IntelDisplayConfig mConfig;
    IntelHWComposerLayerList *mLayerList;
    IntelDisplayPlaneManager *mPlaneManager;
    IntelHWComposerDrm *mDrm;
    int32_t mIsConnected;
    bool mInitialized;
    bool mHotplugEvent;
    bool mForceSwapBuffer;
    uint32_t mDisplayIndex;

protected:
    virtual bool isHWCUsage(int usage);
    virtual bool isHWCFormat(int format);
    virtual bool isHWCTransform(uint32_t transform);
    virtual bool isHWCBlending(uint32_t blending);
    virtual bool isHWCLayer(hwc_layer_1_t *layer);
    virtual bool areLayersIntersecting(hwc_layer_1_t *top, hwc_layer_1_t* bottom);
    virtual bool isLayerSandwiched(int index, hwc_display_contents_1_t *list);

    virtual bool isOverlayLayer(hwc_display_contents_1_t *list,
                        int index,
                        hwc_layer_1_t *layer,
                        int& flags);
    virtual bool isSpriteLayer(hwc_display_contents_1_t *list,
                       int index,
                       hwc_layer_1_t *layer,
                       int& flags);
    virtual bool isPrimaryLayer(hwc_display_contents_1_t *list,
                       int index,
                       hwc_layer_1_t *layer,
                       int& flags);
    virtual bool overlayPrepare(int index, hwc_layer_1_t *layer, int flags);
    virtual bool spritePrepare(int index, hwc_layer_1_t *layer, int flags);
    virtual bool primaryPrepare(int index, hwc_layer_1_t *layer, int flags);

    virtual void dumpLayerList(hwc_display_contents_1_t *list);
    virtual bool isScreenshotActive(hwc_display_contents_1_t *list);

public:
    virtual bool initCheck() { return mInitialized; }
    virtual bool prepare(hwc_display_contents_1_t *hdc) {return true;}
    virtual bool commit(hwc_display_contents_1_t *hdc) {return true;}
    virtual bool commit(hwc_display_contents_1_t *hdc,
                        buffer_handle_t *bh, int &numBuffers) {return true;}
    virtual bool release();
    virtual bool dump(char *buff, int buff_len, int *cur_len);
    virtual bool blank(int blank);
    virtual void onHotplugEvent(bool hpd);

    virtual bool getDisplayConfig(uint32_t* configs, size_t* numConfigs);
    virtual bool getDisplayAttributes(uint32_t config,
            const uint32_t* attributes, int32_t* values);

    IntelDisplayDevice(IntelDisplayPlaneManager *pm,
                       IntelHWComposerDrm *drm,
                       uint32_t index);

    virtual ~IntelDisplayDevice();
};

class IntelMIPIDisplayDevice : public IntelDisplayDevice {
protected:
    IntelBufferManager *mBufferManager;
    IntelBufferManager *mGrallocBufferManager;
    IMG_framebuffer_device_public_t *mFBDev;
    int* mWidiNativeWindow;

protected:
    bool isForceOverlay(hwc_layer_1_t *layer);
    bool isBobDeinterlace(hwc_layer_1_t *layer);
    bool useOverlayRotation(hwc_layer_1_t *layer, int index, uint32_t& handle,
                           int& w, int& h,
                           int& srcX, int& srcY, int& srcW, int& srcH, uint32_t& transform);
    void revisitLayerList(hwc_display_contents_1_t *list, bool isGeometryChanged);

protected:
    virtual bool isOverlayLayer(hwc_display_contents_1_t *list,
                        int index,
                        hwc_layer_1_t *layer,
                        int& flags);
    virtual bool isSpriteLayer(hwc_display_contents_1_t *list,
                       int index,
                       hwc_layer_1_t *layer,
                       int& flags);
    virtual bool isPrimaryLayer(hwc_display_contents_1_t *list,
                       int index,
                       hwc_layer_1_t *layer,
                       int& flags);

    virtual void onGeometryChanged(hwc_display_contents_1_t *list);
    virtual bool updateLayersData(hwc_display_contents_1_t *list);
    virtual bool flipFramebufferContexts(void *contexts);
public:
    IntelMIPIDisplayDevice(IntelBufferManager *bm,
                           IntelBufferManager *gm,
                           IntelDisplayPlaneManager *pm,
                           IMG_framebuffer_device_public_t *fbdev,
                           IntelHWComposerDrm *drm,
                           uint32_t index);
    ~IntelMIPIDisplayDevice();
    virtual bool prepare(hwc_display_contents_1_t *hdc);
    virtual bool commit(hwc_display_contents_1_t *hdc,
                        buffer_handle_t *bh, int &numBuffers);
    virtual bool dump(char *buff, int buff_len, int *cur_len);
    virtual bool blank(int blank);

    virtual bool getDisplayConfig(uint32_t* configs, size_t* numConfigs);
    virtual bool getDisplayAttributes(uint32_t config,
            const uint32_t* attributes, int32_t* values);
};

class IntelHDMIDisplayDevice : public IntelDisplayDevice {
protected:
    enum {
        HDMI_BUF_NUM = 2,
    };

    IMG_framebuffer_device_public_t *mFBDev;
    IntelBufferManager *mBufferManager;
    IntelBufferManager *mGrallocBufferManager;

    struct hdmi_buffer{
        unsigned long long ui64Stamp;
        IntelDisplayBuffer *buffer;
    } mHDMIBuffers[HDMI_BUF_NUM];
    int mNextBuffer;

protected:
    virtual void onGeometryChanged(hwc_display_contents_1_t *list);
    virtual bool updateLayersData(hwc_display_contents_1_t *list);
    virtual bool flipFramebufferContexts(void *contexts, hwc_layer_1_t *target_layer);
public:
    IntelHDMIDisplayDevice(IntelBufferManager *bm,
                       IntelBufferManager *gm,
                       IntelDisplayPlaneManager *pm,
                       IMG_framebuffer_device_public_t *fbdev,
                       IntelHWComposerDrm *drm,
                       uint32_t index);

    ~IntelHDMIDisplayDevice();
    virtual bool prepare(hwc_display_contents_1_t *hdc);
    virtual bool commit(hwc_display_contents_1_t *hdc,
                        buffer_handle_t *bh, int &numBuffers);
    virtual bool dump(char *buff, int buff_len, int *cur_len);
    virtual bool blank(int blank);

    virtual bool getDisplayConfig(uint32_t* configs, size_t* numConfigs);
    virtual bool getDisplayAttributes(uint32_t config,
            const uint32_t* attributes, int32_t* values);

};
#endif /*__INTEL__DISPLAY_DEVICE__*/
