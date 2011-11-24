/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef __INTEL_HWCOMPOSER_CPP__
#define __INTEL_HWCOMPOSER_CPP__

#include <EGL/egl.h>
#include <hardware/hwcomposer.h>

#include <IntelHWComposerDrm.h>
#include <IntelBufferManager.h>
#include <IntelHWComposerLayer.h>

class IntelHWComposer : public hwc_composer_device_t {
private:
    IntelHWComposerDrm *mDrm;
    IntelBufferManager *mBufferManager;
    IntelBufferManager *mGrallocBufferManager;
    IntelDisplayPlaneManager *mPlaneManager;
    IntelHWComposerLayerList *mLayerList;
    bool mNeedSwapBuffer;
    bool mInitialized;
private:
    void onGeometryChanged(hwc_layer_list_t *list);
    bool overlayPrepare(int index, hwc_layer_t *layer, int flags);
    bool spritePrepare(int index, hwc_layer_t *layer, int flags);
    bool isOverlayHandle(uint32_t handle);
    bool isSpriteHandle(uint32_t);
    bool isOverlayLayer(hwc_layer_list_t *list,
                        int index,
                        hwc_layer_t *layer,
                        int& flags);
    bool isSpriteLayer(hwc_layer_list_t *list,
                       int index,
                       hwc_layer_t *layer,
                       int& flags);
    bool updateLayersData(hwc_layer_list_t *list);
    bool isHWCUsage(int usage);
    bool isHWCFormat(int format);
    bool isHWCTransform(uint32_t transform);
    bool isHWCBlending(uint32_t blending);
    bool isHWCLayer(hwc_layer_t *layer);
    bool areLayersIntersecting(hwc_layer_t *top, hwc_layer_t* bottom);
public:
    bool initCheck() { return mInitialized; }
    bool initialize();
    bool prepare(hwc_layer_list_t *list);
    bool commit(hwc_display_t dpy, hwc_surface_t sur, hwc_layer_list_t *list);
    IntelHWComposer()
        : mDrm(0), mBufferManager(0), mGrallocBufferManager(0),
          mPlaneManager(0), mLayerList(0),
          mNeedSwapBuffer(true), mInitialized(false) {}
    ~IntelHWComposer();
};

#endif /*__INTEL_HWCOMPOSER_CPP__*/