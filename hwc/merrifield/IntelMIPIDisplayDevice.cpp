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
#include <cutils/log.h>
#include <cutils/atomic.h>

#include <IntelHWComposer.h>
#include <IntelDisplayDevice.h>
#include <IntelOverlayUtil.h>
#include <IntelWidiPlane.h>
#include <IntelHWComposerCfg.h>

IntelMIPIDisplayDevice::IntelMIPIDisplayDevice(IntelBufferManager *bm,
                                       IntelBufferManager *gm,
                                       IntelDisplayPlaneManager *pm,
                                       IMG_framebuffer_device_public_t *fbdev,
                                       IntelHWComposerDrm *drm,
                                       uint32_t index)
                                     : IntelDisplayDevice(pm, drm, index),
                                       mBufferManager(bm),
                                       mGrallocBufferManager(gm),
                                       mFBDev(fbdev),
                                       mWidiNativeWindow(NULL)
{
    ALOGD_IF(ALLOW_HWC_PRINT, "%s\n", __func__);

    // check buffer manager for gralloc buffer
    if (!mGrallocBufferManager) {
        ALOGE("%s: Invalid Gralloc buffer manager\n", __func__);
        goto init_err;
    }

    // check IMG frame buffer device
    if (!mFBDev) {
        ALOGE("%s: failed to open IMG FB device\n", __func__);
        goto init_err;
    }

    //create new DRM object if not exists
    if (!mDrm) {
        ALOGE("%s: failed to initialize DRM instance\n", __func__);
        goto init_err;
    }

    // check display plane manager
    if (!mPlaneManager) {
        ALOGE("%s: Invalid plane manager\n", __func__);
        goto init_err;
    }

    // create layer list
    mLayerList = new IntelHWComposerLayerList(mPlaneManager);
    if (!mLayerList) {
        ALOGE("%s: Failed to create layer list\n", __func__);
        goto init_err;
    }

    mInitialized = true;
    return;

init_err:
    mInitialized = false;
    return;
}

IntelMIPIDisplayDevice::~IntelMIPIDisplayDevice()
{
    delete mLayerList;
}

// isSpriteLayer: check whether a given @layer can be handled
// by a hardware sprite plane.
// A layer is a sprite layer when
// 1) layer is RGB layer &&
// 2) No active external display (TODO: support external display)
// 3) HWC_SKIP_LAYER flag wasn't set by surface flinger
// 4) layer requires no blending or premultipled blending
// 5) layer has no transform (rotation, scaling)
bool IntelMIPIDisplayDevice::isSpriteLayer(hwc_display_contents_1_t *list,
                                    int index,
                                    hwc_layer_1_t *layer,
                                    int& flags)
{
    bool needClearFb = false;
    bool forceSprite = false;
    bool useSprite = false;

    int srcWidth, srcHeight;
    int dstWidth, dstHeight;

    if (!list || !layer)
        return false;

    intel_gralloc_buffer_handle_t *grallocHandle =
        (intel_gralloc_buffer_handle_t*)layer->handle;

    if (!grallocHandle) {
        ALOGD_IF(ALLOW_HWC_PRINT, "%s: invalid gralloc handle\n", __func__);
        return false;
    }

    // don't handle target framebuffer layer
    if (layer->compositionType == HWC_FRAMEBUFFER_TARGET)
        return false;

    // check whether pixel format is supported RGB formats
    if (mLayerList->getLayerType(index) != IntelHWComposerLayer::LAYER_TYPE_RGB) {
        ALOGD_IF(ALLOW_HWC_PRINT,
                "%s: invalid format 0x%x\n", __func__, grallocHandle->format);
        useSprite = false;
        goto out_check;
    }

    // Got a RGB layer, disable sprite plane when Widi is active
    if (mPlaneManager->isWidiActive()) {
        useSprite = false;
        goto out_check;
    }

    // fall back if HWC_SKIP_LAYER was set
    if ((layer->flags & HWC_SKIP_LAYER)) {
        ALOGD_IF(ALLOW_HWC_PRINT, "isSpriteLayer: HWC_SKIP_LAYER");
        useSprite = false;
        goto out_check;
    }

    // check usage???

    // check blending, only support none & premultipled blending
    // clear frame buffer region if layer has no blending
    if (layer->blending != HWC_BLENDING_PREMULT &&
        layer->blending != HWC_BLENDING_NONE) {
        ALOGD("isSpriteLayer: unsupported blending");
        useSprite = false;
        goto out_check;
    }

    // check rotation
    if (layer->transform) {
        ALOGD_IF(ALLOW_HWC_PRINT, "isSpriteLayer: need do transform");
        useSprite = false;
        goto out_check;
    }

     // check scaling
    srcWidth = layer->sourceCrop.right - layer->sourceCrop.left;
    srcHeight = layer->sourceCrop.bottom - layer->sourceCrop.top;
    dstWidth = layer->displayFrame.right - layer->displayFrame.left;
    dstHeight = layer->displayFrame.bottom - layer->displayFrame.top;

    if ((srcWidth == dstWidth) && (srcHeight == dstHeight))
        useSprite = true;
    else
        ALOGD_IF(ALLOW_HWC_PRINT,
               "isSpriteLayer: src W,H [%d, %d], dst W,H [%d, %d]",
               srcWidth, srcHeight, dstWidth, dstHeight);

    if (layer->blending == HWC_BLENDING_NONE)
        needClearFb = true;
out_check:
    if (forceSprite) {
        // clear HWC_SKIP_LAYER flag so that force to use overlay
        ALOGD("isSpriteLayer: force to use sprite");
        layer->flags &= ~HWC_SKIP_LAYER;
        mLayerList->setForceOverlay(index, true);
        layer->compositionType = HWC_OVERLAY;
        useSprite = true;
    }

    // check if frame buffer clear is needed
    if (useSprite) {
        ALOGD("isSpriteLayer: got a sprite layer");
        if (needClearFb) {
            ALOGD_IF(ALLOW_HWC_PRINT, "isSpriteLayer: clear fb");
            //layer->hints |= HWC_HINT_CLEAR_FB;
            mForceSwapBuffer = true;
        }
        layer->compositionType = HWC_OVERLAY;
    }

    flags = 0;
    return useSprite;
}

// isPrimaryLayer: check whether we can use primary plane to handle
// the given @layer.
// primary plane can be used only when
// 1) @layer is on the top of other layers (FIXME: not necessary, remove it
//    after introducing z order configuration)
// 2) all other layers were handled by HWC.
// 3) @layer is a sprite layer
// 4) @layer wasn't handled by sprite
bool IntelMIPIDisplayDevice::isPrimaryLayer(hwc_display_contents_1_t *list,
                                     int index,
                                     hwc_layer_1_t *layer,
                                     int& flags)
{
    // if a layer has already been handled, further check if it's a
    // sprite layer/overlay layer, if so, we simply bypass this layer.
    if (layer->compositionType == HWC_OVERLAY) {
        IntelDisplayPlane *plane = mLayerList->getPlane(index);
        if (plane) {
            switch (plane->getPlaneType()) {
            case IntelDisplayPlane::DISPLAY_PLANE_PRIMARY:
                // detach plane & re-check it
                mLayerList->detachPlane(index, plane);
                layer->compositionType = HWC_FRAMEBUFFER;
                layer->hints = 0;
                break;
            case IntelDisplayPlane::DISPLAY_PLANE_OVERLAY:
            case IntelDisplayPlane::DISPLAY_PLANE_SPRITE:
            default:
                return false;
            }
        }
    }

    // check whether all other layers were handled by HWC
    for (size_t i = 0; i < list->numHwLayers-1; i++) {
        if ((i != size_t(index)) &&
            (list->hwLayers[i].compositionType != HWC_OVERLAY))
            return false;
    }

    return isSpriteLayer(list, index, layer, flags);
}


// TODO: re-implement this function after video interface
// is ready.
// Currently, LayerTS::setGeometry will set compositionType
// to HWC_OVERLAY. HWC will change it to HWC_FRAMEBUFFER
// if HWC found this layer was NOT a overlay layer (can NOT
// be handled by hardware overlay)
bool IntelMIPIDisplayDevice::isOverlayLayer(hwc_display_contents_1_t *list,
                                     int index,
                                     hwc_layer_1_t *layer,
                                     int& flags)
{
    bool needClearFb = false;
    bool forceOverlay = false;
    bool useOverlay = false;

    if (!list || !layer)
        return false;

    intel_gralloc_buffer_handle_t *grallocHandle =
        (intel_gralloc_buffer_handle_t*)layer->handle;

    if (!grallocHandle)
        return false;

    IntelWidiPlane* widiPlane = (IntelWidiPlane*)mPlaneManager->getWidiPlane();

    // clear hints
    layer->hints = 0;

    // check format
    if (mLayerList->getLayerType(index) != IntelHWComposerLayer::LAYER_TYPE_YUV) {
        useOverlay = false;
        goto out_check;
    }

    // Got a YUV layer, check external display status for extend video mode
    // force to use overlay in video extend mode
    if (mDrm->getDisplayMode() == OVERLAY_EXTEND)
        forceOverlay = true;

    // check buffer usage
    if ((grallocHandle->usage & GRALLOC_USAGE_PROTECTED) || isForceOverlay(layer)) {
        ALOGD_IF(ALLOW_HWC_PRINT, "isOverlayLayer: protected video/force Overlay");
        forceOverlay = true;
    }

    // check blending, overlay cannot support blending
    if (layer->blending != HWC_BLENDING_NONE) {
        useOverlay = false;
        goto out_check;
    }

    // fall back if HWC_SKIP_LAYER was set, if forced to use
    // overlay skip this check
    if (!forceOverlay && (layer->flags & HWC_SKIP_LAYER)) {
        ALOGD_IF(ALLOW_HWC_PRINT, "isOverlayLayer: skip layer was set");
        useOverlay = false;
        goto out_check;
    }

    // check visible regions
    if (layer->visibleRegionScreen.numRects > 1) {
        useOverlay = false;
        goto out_check;
    }

    // TODO: not support OVERLAY_CLONE_MIPI0
    if (mDrm->getDisplayMode() == OVERLAY_CLONE_MIPI0) {
        useOverlay = false;
        goto out_check;
    }

    // fall back if YUV Layer is in the middle of
    // other layers and covers the layers under it.
    if (!forceOverlay && index > 0 && index < int(list->numHwLayers-1)) {
        for (int i = index - 1; i >= 0; i--) {
            if (areLayersIntersecting(layer, &list->hwLayers[i])) {
                useOverlay = false;
                goto out_check;
            }
        }
    }

    // check whether layer are covered by layers above it
    // if layer is covered by a layer which needs blending,
    // clear corresponding region in frame buffer
    for (size_t i = index + 1; i < list->numHwLayers-1; i++) {
        if (areLayersIntersecting(&list->hwLayers[i], layer)) {
            ALOGD_IF(ALLOW_HWC_PRINT,
                "%s: overlay %d is covered by layer %d\n", __func__, index, i);
                if (list->hwLayers[i].blending !=  HWC_BLENDING_NONE)
                    mLayerList->setNeedClearup(index, true);
        }
    }

    useOverlay = true;
    needClearFb = true;
out_check:
    if (forceOverlay) {
        // clear HWC_SKIP_LAYER flag so that force to use overlay
        ALOGD("isOverlayLayer: force to use overlay");
        layer->flags &= ~HWC_SKIP_LAYER;
        mLayerList->setForceOverlay(index, true);
        layer->compositionType = HWC_OVERLAY;
        useOverlay = true;
        needClearFb = true;
#ifdef INTEL_EXT_SF_ANIMATION_HINT
        layer->hints |= HWC_HINT_DISABLE_ANIMATION;
#endif
    }

    // check if frame buffer clear is needed
    if (useOverlay) {
        ALOGD("isOverlayLayer: got an overlay layer");
        if (needClearFb) {
            //layer->hints |= HWC_HINT_CLEAR_FB;
            ALOGD_IF(ALLOW_HWC_PRINT, "isOverlayLayer: clear fb");
            mForceSwapBuffer = true;
        }
        layer->compositionType = HWC_OVERLAY;
    }

    flags = 0;
    return useOverlay;
}

void IntelMIPIDisplayDevice::revisitLayerList(hwc_display_contents_1_t *list, bool isGeometryChanged)
{
    int zOrderConfig = IntelDisplayPlaneManager::ZORDER_OcOaP;

    if (!list)
        return;

    for (size_t i = 0; i < list->numHwLayers-1; i++) {
        int flags = 0;

        // also need check whether a layer can be handled in general
        if (!isHWCLayer(&list->hwLayers[i]))
            continue;

        // make sure all protected layers were marked as overlay
        if (mLayerList->isProtectedLayer(i))
            list->hwLayers[i].compositionType = HWC_OVERLAY;
        // check if we can apply primary plane to an RGB layer
        // if overlay plane was used for a YUV layer, force overlay layer to
        // be the bottom layer.
        if (mLayerList->getLayerType(i) != IntelHWComposerLayer::LAYER_TYPE_RGB) {
            zOrderConfig = IntelDisplayPlaneManager::ZORDER_POcOa;
            continue;
        }

        if (isPrimaryLayer(list, i, &list->hwLayers[i], flags)) {
            bool ret = primaryPrepare(i, &list->hwLayers[i], flags);
            if (!ret) {
                ALOGE("%s: failed to prepare primary\n", __func__);
                list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
                list->hwLayers[i].hints = 0;
            }
        }
    }

    if (isGeometryChanged)
        mPlaneManager->setZOrderConfig(zOrderConfig, 0);

}

// When the geometry changed, we need
// 0) reclaim all allocated planes, reclaimed planes will be disabled
//    on the start of next frame. A little bit tricky, we cannot disable the
//    planes right after geometry is changed since there's no data in FB now,
//    so we need to wait FB is update then disable these planes.
// 1) build a new layer list for the changed hwc_layer_list
// 2) attach planes to these layers which can be handled by HWC
void IntelMIPIDisplayDevice::onGeometryChanged(hwc_display_contents_1_t *list)
{
    bool firstTime = true;

    // reclaim all planes
    bool ret = mLayerList->invalidatePlanes();
    if (!ret) {
        ALOGE("%s: failed to reclaim allocated planes\n", __func__);
        return;
    }

    // update layer list with new list
    mLayerList->updateLayerList(list);

    // TODO: uncomment it to print out layer list info
    // dumpLayerList(list);

    if (isScreenshotActive(list)) {
        ALOGD_IF(ALLOW_HWC_PRINT, "%s: Screenshot Active!\n", __func__);
        goto out_check;
    }

    for (size_t i = 0; list && i < list->numHwLayers-1; i++) {
        // check whether a layer can be handled in general
        if (!isHWCLayer(&list->hwLayers[i]))
            continue;

        // further check whether a layer can be handle by overlay/sprite
        int flags = 0;
        bool hasOverlay = mPlaneManager->hasFreeOverlays();
        bool hasSprite = mPlaneManager->hasFreeSprites();

        if (hasOverlay && isOverlayLayer(list, i, &list->hwLayers[i], flags)) {
            ret = overlayPrepare(i, &list->hwLayers[i], flags);
            if (!ret) {
                ALOGE("%s: failed to prepare overlay\n", __func__);
                list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
                list->hwLayers[i].hints = 0;
            }
        } else if (hasSprite && isSpriteLayer(list, i, &list->hwLayers[i], flags)) {
            ret = spritePrepare(i, &list->hwLayers[i], flags);
            if (!ret) {
                ALOGE("%s: failed to prepare sprite\n", __func__);
                list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
                list->hwLayers[i].hints = 0;
            }
        } else {
            list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
            if(firstTime && mPlaneManager->isWidiActive() ) {
                IntelWidiPlane *p = (IntelWidiPlane *)mPlaneManager->getWidiPlane();
                p->setOrientation(list->hwLayers[i].transform);
                firstTime = false;
            }
        }
    }

out_check:
    // revisit each layer, make sure protected layers were handled by hwc,
    // and check if we can make use of primary plane
    revisitLayerList(list, true);

    // disable reclaimed planes
    mPlaneManager->disableReclaimedPlanes(IntelDisplayPlane::DISPLAY_PLANE_SPRITE);
    mPlaneManager->disableReclaimedPlanes(IntelDisplayPlane::DISPLAY_PLANE_PRIMARY);
}

bool IntelMIPIDisplayDevice::prepare(hwc_display_contents_1_t *list)
{
    ALOGD_IF(ALLOW_HWC_PRINT, "%s\n", __func__);

    if (!initCheck()) {
        ALOGE("%s: failed to initialize HWComposer\n", __func__);
        return false;
    }

    // delay 3 circles to disable useless overlay planes
    static int counter = 0;
    if (mPlaneManager->hasReclaimedOverlays()) {
        if (++counter == 3) {
            mPlaneManager->disableReclaimedPlanes(IntelDisplayPlane::DISPLAY_PLANE_OVERLAY);
            counter = 0;
        }
    }
    else
        counter = 0;


    // clear force swap buffer flag
    mForceSwapBuffer = false;

    bool widiStatusChanged = mPlaneManager->isWidiStatusChanged();

    // handle geometry changing. attach display planes to layers
    // which can be handled by HWC.
    // plane control information (e.g. position) will be set here
    if (!list || (list->flags & HWC_GEOMETRY_CHANGED) || mHotplugEvent
        || widiStatusChanged) {
        onGeometryChanged(list);

        IntelWidiPlane* widiPlane = (IntelWidiPlane*)mPlaneManager->getWidiPlane();
        if (widiStatusChanged && mDrm) {
            if(mPlaneManager->isWidiActive()) {
                mDrm->notifyWidi(true);
                if(widiPlane->isBackgroundVideoMode())
                    mDrm->notifyMipi(true);
                else
                    mDrm->notifyMipi(!widiPlane->isStreaming());
            }
            else
            {
                mDrm->notifyWidi(false);
            }
        }

        if(mHotplugEvent) {
            if(mPlaneManager->isWidiActive()) {
                if(widiPlane->isExtVideoAllowed()) {
                    // default fps to 0. widi stack will decide what correct fps should be
                    int displayW = 0, displayH = 0, fps = 0, isInterlace = 0;
                    if(mDrm->isVideoPlaying()) {
                        if(mDrm->getVideoInfo(&displayW, &displayH, &fps, &isInterlace)) {
                            if(fps < 0) fps = 0;
                        }
                    }
                    widiPlane->setPlayerStatus(mDrm->isVideoPlaying(), fps);
                }
            }
            mHotplugEvent = false;
        }

        intel_overlay_mode_t mode = mDrm->getDisplayMode();
        if (list && (mode == OVERLAY_EXTEND ||  widiPlane->isStreaming()) &&
            (list->flags & HWC_GEOMETRY_CHANGED)) {
            bool hasSkipLayer = false;
            ALOGD_IF(ALLOW_HWC_PRINT,
                    "layers num:%d", list->numHwLayers-1);
            for (size_t j = 0 ; j < list->numHwLayers-1 ; j++) {
                if (list->hwLayers[j].flags & HWC_SKIP_LAYER) {
                hasSkipLayer = true;
                break;
                }
            }

            if (!hasSkipLayer) {
                if ((list->numHwLayers-1) == 1)
                    mDrm->notifyMipi(false);
                else
                    mDrm->notifyMipi(true);
            }
        }
    }

    // handle buffer changing. setup data buffer.
    if (list && !updateLayersData(list)) {
        ALOGD_IF(ALLOW_HWC_PRINT, "prepare: revisiting layer list\n");
        revisitLayerList(list, false);
    }

    //FIXME: we have to clear fb by hwc itself because
    //surfaceflinger can't do proper clear now.
    if (mForceSwapBuffer)
        mLayerList->clearWithOpenGL();

    return true;
}

bool IntelMIPIDisplayDevice::commit(hwc_display_contents_1_t *list,
		                     IMG_hwc_layer_t *imgList,
		                     int &numLayers)
{
    ALOGD_IF(ALLOW_HWC_PRINT, "%s\n", __func__);

    if (!initCheck()) {
        ALOGE("%s: failed to initialize HWComposer\n", __func__);
        return false;
    }

    // if hotplug was happened & didn't be handled skip the flip
    if (mHotplugEvent)
        return true;

    // need check whether eglSwapBuffers is necessary
    bool needSwapBuffer = false;

    // if all layers were attached with display planes then we don't need
    // swap buffers.
    if (!mLayerList->getLayersCount() ||
        mLayerList->getLayersCount() != mLayerList->getAttachedPlanesCount() ||
        mForceSwapBuffer) {
        ALOGD_IF(ALLOW_HWC_PRINT,
               "%s: mForceSwapBuffer: %d, layer count: %d, attached plane:%d\n",
               __func__, mForceSwapBuffer, mLayerList->getLayersCount(),
               mLayerList->getAttachedPlanesCount());

        // FIXME: it might be a surface flinger bug
        // surface flinger failed to render a layer to FB sometimes
	// because screen dirty region was unchanged, in this case
        // we don't to swap buffers.
#ifdef INTEL_EXT_SF_NEED_SWAPBUFFER
        if (!list || (list->flags & HWC_NEED_SWAPBUFFERS))
#endif
            needSwapBuffer = true;
    }

    // if primary plane is in use, skip eglSwapBuffers
    if (!mPlaneManager->primaryAvailable(0)) {
        ALOGD_IF(ALLOW_HWC_PRINT, "%s: primary plane in use\n", __func__);
        needSwapBuffer = false;
    }

    if(mPlaneManager->isWidiActive()) {
        IntelDisplayPlane *p = mPlaneManager->getWidiPlane();
        ALOGD_IF(ALLOW_HWC_PRINT, "Widi Plane is %p",p);
         if (p)
             p->flip(0, 0);
         else
             ALOGE("Widi Plane is NULL");
    }

    { //if (mFBDev->bBypassPost) {
        // setup primary plane contexts if swap buffers is needed
        if (needSwapBuffer && list->hwLayers[list->numHwLayers-1].handle)
            flipFramebufferContexts(&imgList[numLayers++],
                                    &list->hwLayers[list->numHwLayers - 1]);

        // Call plane's flip for each layer in hwc_layer_list, if a plane has
        // been attached to a layer
        // First post RGB layers, then overlay layers.
        for (size_t i=0 ; list && i<list->numHwLayers-1 ; i++) {
            IntelDisplayPlane *plane = mLayerList->getPlane(i);
            int flags = mLayerList->getFlags(i);
            IMG_hwc_layer_t *imgLayer = 0;

            if (!plane)
                continue;
            if (list->hwLayers[i].flags & HWC_SKIP_LAYER)
                continue;
            if (list->hwLayers[i].compositionType != HWC_OVERLAY)
                continue;

            ALOGD_IF(ALLOW_HWC_PRINT, "%s: flip plane %d, flags: 0x%x\n",
                __func__, i, flags);

            bool ret = plane->flip(0, flags);
            if (!ret)
                ALOGW("%s: failed to flip plane %d context !\n", __func__, i);

            // update IMG layer
            imgLayer = &imgList[numLayers++];
            imgLayer->handle = list->hwLayers[i].handle;
            imgLayer->transform = list->hwLayers[i].transform;
            imgLayer->blending = list->hwLayers[i].blending;
            imgLayer->sourceCrop = list->hwLayers[i].sourceCrop;
            imgLayer->displayFrame = list->hwLayers[i].displayFrame;
            imgLayer->custom = (uint32_t)plane->getContext();

            // clear flip flags, except for DELAY_DISABLE
            mLayerList->setFlags(i, flags & IntelDisplayPlane::DELAY_DISABLE);

            // remove clear fb hints
            list->hwLayers[i].hints &= ~HWC_HINT_CLEAR_FB;
        }
#if 0
        // commit plane contexts
        if (mFBDev && numBuffers) {
            ALOGD_IF(ALLOW_HWC_PRINT, "%s: commits %d buffers\n", __func__, numBuffers);
            int err = mFBDev->Post2(&mFBDev->base,
                                    bufferHandles,
                                    numBuffers,
                                    context,
                                    mPlaneManager->getContextLength());
            if (err) {
                ALOGE("%s: Post2 failed with errno %d\n", __func__, err);
                return false;
            }
        }
#endif
    }

#if 0
        //make sure all flips were finished
        for (size_t i=0 ; list && i<list->numHwLayers-1 ; i++) {
            IntelDisplayPlane *plane = mLayerList->getPlane(i);
            int flags = mLayerList->getFlags(i);
            if (!plane)
                continue;
            if (list->hwLayers[i].flags & HWC_SKIP_LAYER)
                continue;
            if (list->hwLayers[i].compositionType != HWC_OVERLAY)
                continue;
            if (mPlaneManager->isWidiActive())
                continue;

            plane->waitForFlipCompletion();
        }
#endif

    return true;
}

// This function performs:
// 1) update layer's transform to data buffer's payload buffer, so that video
//    driver can get the latest transform info of this layer
// 2) if rotation is needed, video driver would setup the rotated buffer, then
//    update buffer's payload to inform HWC rotation buffer is available.
// 3) HWC would keep using ST till all rotation buffers are ready.
// Return: false if HWC is NOT ready to switch to overlay, otherwise true.
bool IntelMIPIDisplayDevice::useOverlayRotation(hwc_layer_1_t *layer,
                                         int index,
                                         uint32_t& handle,
                                         int& w, int& h,
                                         int& srcX, int& srcY,
                                         int& srcW, int& srcH,
                                         uint32_t &transform)
{
    bool useOverlay = false;
    uint32_t hwcLayerTransform;
    IntelWidiPlane* widiplane = (IntelWidiPlane*)mPlaneManager->getWidiPlane();

    // FIXME: workaround for rotation issue, remove it later
    static int counter = 0;
    uint32_t metadata_transform = 0;
    uint32_t displayMode = 0;

    if (!layer)
        return false;

    intel_gralloc_buffer_handle_t *grallocHandle =
        (intel_gralloc_buffer_handle_t*)layer->handle;

    if (!grallocHandle)
        return false;

    // detect video mode change
    displayMode = mDrm->getDisplayMode();
    if(widiplane->isStreaming())
        displayMode = OVERLAY_EXTEND;

    if (grallocHandle->format == HAL_PIXEL_FORMAT_INTEL_HWC_NV12) {
        // map payload buffer
        IntelDisplayBuffer *buffer =
            mGrallocBufferManager->map((uint32_t)grallocHandle,
                                       GRALLOC_SUB_BUFFER1);
        if (!buffer) {
            ALOGE("%s: failed to map payload buffer.\n", __func__);
            return false;
        }

        intel_gralloc_payload_t *payload =
            (intel_gralloc_payload_t*)buffer->getCpuAddr();

        if (!payload) {
            ALOGE("%s: invalid address\n", __func__);
            // unmap payload buffer
            mGrallocBufferManager->unmap(buffer);
            return false;
        }

        if (payload->force_output_method == OUTPUT_FORCE_GPU) {
            ALOGD_IF(ALLOW_HWC_PRINT,
                    "%s: force to use surface texture.", __func__);
            // unmap payload buffer
            mGrallocBufferManager->unmap(buffer);
            return false;
        }

        metadata_transform = payload->metadata_transform;

        //For extend mode, we ignore WM rotate info
        if (displayMode == OVERLAY_EXTEND) {
            transform = metadata_transform;
        }

        if (!transform) {
            ALOGD_IF(ALLOW_HWC_PRINT,
                    "%s: use overlay to display original buffer.", __func__);
            // unmap payload buffer
            mGrallocBufferManager->unmap(buffer);
            return true;
        }

        if (transform != uint32_t(payload->client_transform)) {
            ALOGD_IF(ALLOW_HWC_PRINT,
                    "%s: rotation buffer was not prepared by client! ui64Stamp = %llu\n", __func__, grallocHandle->ui64Stamp);
            // unmap payload buffer
            mGrallocBufferManager->unmap(buffer);
            return false;
        }

        // update handle, w & h to rotation buffer
        handle = payload->rotated_buffer_handle;
        w = payload->rotated_width;
        h = payload->rotated_height;
        //wait video rotated buffer idle
        mGrallocBufferManager->waitIdle(handle);

        // unmap payload buffer
        mGrallocBufferManager->unmap(buffer);

        // NOTE: exchange the srcWidth & srcHeight since
        // video driver currently doesn't call native_window_*
        // helper functions to update info for rotation buffer.
        if (transform == HAL_TRANSFORM_ROT_90 ||
                transform == HAL_TRANSFORM_ROT_270) {
            int temp = srcH;
            srcH = srcW;
            srcW = temp;
            temp = srcX;
            srcX = srcY;
            srcY = temp;

        }

        // skip pading bytes in rotate buffer
        switch(transform) {
            case HAL_TRANSFORM_ROT_90:
                srcX += ((srcW + 0xf) & ~0xf) - srcW;
                break;
            case HAL_TRANSFORM_ROT_180:
                srcX += ((srcW + 0xf) & ~0xf) - srcW;
                srcY += ((srcH + 0xf) & ~0xf) - srcH;
                break;
            case HAL_TRANSFORM_ROT_270:
                srcY += ((srcH + 0xf) & ~0xf) - srcH;
                break;
            default:
                break;
        }
    } else {
        //For software codec, overlay can't handle rotate
        //and fallback to surface texture.
        if ((displayMode != OVERLAY_EXTEND) && transform) {
            ALOGD_IF(ALLOW_HWC_PRINT,
                    "%s: software codec rotation, back to ST!", __func__);
            return false;
        }
        //set this flag for mapping correct buffer
        transform = 0;
    }

    //for most of cases, we handle rotate by overlay
    useOverlay = true;
    return useOverlay;
}

bool IntelMIPIDisplayDevice::isForceOverlay(hwc_layer_1_t *layer)
{
    if (!layer)
        return false;

    intel_gralloc_buffer_handle_t *grallocHandle =
        (intel_gralloc_buffer_handle_t*)layer->handle;

    if (!grallocHandle)
        return false;

    if (grallocHandle->format == HAL_PIXEL_FORMAT_INTEL_HWC_NV12) {
        // map payload buffer
        IntelDisplayBuffer *buffer =
            mGrallocBufferManager->map((uint32_t)grallocHandle,
                                       GRALLOC_SUB_BUFFER1);
        if (!buffer) {
            ALOGE("%s: failed to map payload buffer.\n", __func__);
            return false;
        }

        intel_gralloc_payload_t *payload =
            (intel_gralloc_payload_t*)buffer->getCpuAddr();
        if (!payload) {
            ALOGE("%s: invalid address\n", __func__);
            mGrallocBufferManager->unmap(buffer);
            return false;
        }

        if (payload->force_output_method == OUTPUT_FORCE_OVERLAY) {
            mGrallocBufferManager->unmap(buffer);
            return true;
        }

        mGrallocBufferManager->unmap(buffer);
    }

    return false;
}

// This function performs:
// Acquire bool BobDeinterlace from video driver
// If ture, use bob deinterlace, otherwise, use weave deinterlace
bool IntelMIPIDisplayDevice::isBobDeinterlace(hwc_layer_1_t *layer)
{
    bool bobDeinterlace = false;

    if (!layer)
        return bobDeinterlace;

    intel_gralloc_buffer_handle_t *grallocHandle =
        (intel_gralloc_buffer_handle_t*)layer->handle;

    if (!grallocHandle) {
        return bobDeinterlace;
    }

    if (grallocHandle->format != HAL_PIXEL_FORMAT_INTEL_HWC_NV12)
        return bobDeinterlace;

    // map payload buffer
    IntelDisplayBuffer *buffer =
        mGrallocBufferManager->map((uint32_t)grallocHandle,
                                   GRALLOC_SUB_BUFFER1);
    if (!buffer) {
        ALOGE("%s: failed to map payload buffer.\n", __func__);
        return false;
    }

    intel_gralloc_payload_t *payload =
        (intel_gralloc_payload_t*)buffer->getCpuAddr();
    if (!payload) {
        ALOGE("%s: invalid address\n", __func__);
        mGrallocBufferManager->unmap(buffer);
        return bobDeinterlace;
    }

    bobDeinterlace = (payload->bob_deinterlace == 1) ? true : false;
    mGrallocBufferManager->unmap(buffer);
    return bobDeinterlace;
}

bool IntelMIPIDisplayDevice::updateLayersData(hwc_display_contents_1_t *list)
{
    IntelDisplayPlane *plane = 0;
    IntelWidiPlane* widiplane = NULL;
    bool ret = true;
    bool handled = true;

    if (mPlaneManager->isWidiActive()) {
         widiplane = (IntelWidiPlane*) mPlaneManager->getWidiPlane();
         mFBDev->bBypassPost = 0;
    } else
         mFBDev->bBypassPost = 0; //cfg.bypasspost;

    for (size_t i=0 ; i<list->numHwLayers-1 ; i++) {
        hwc_layer_1_t *layer = &list->hwLayers[i];
        intel_gralloc_buffer_handle_t *grallocHandle =
            (intel_gralloc_buffer_handle_t*)layer->handle;

        if (grallocHandle &&
            grallocHandle->format == HAL_PIXEL_FORMAT_INTEL_HWC_NV12) {
            // map payload buffer
            IntelDisplayBuffer *buffer =
                mGrallocBufferManager->map((uint32_t)grallocHandle,
                                           GRALLOC_SUB_BUFFER1);
            if (!buffer) {
                ALOGE("%s: failed to map payload buffer.\n", __func__);
                return false;
            }

            intel_gralloc_payload_t *payload =
                (intel_gralloc_payload_t*)buffer->getCpuAddr();

            if (!payload) {
                ALOGE("%s: invalid address\n", __func__);
                return false;
            }
            //wait video buffer idle
            mGrallocBufferManager->waitIdle(payload->khandle);

            // unmap payload buffer
            mGrallocBufferManager->unmap(buffer);
        }
        plane = mLayerList->getPlane(i);
        if (!plane)
            continue;

        // clear layer's visible region if need clear up flag was set
        // and sprite plane was used as primary plane (point to FB)
        if (mLayerList->getNeedClearup(i) &&
            mPlaneManager->primaryAvailable(0)) {
            ALOGD_IF(ALLOW_HWC_PRINT,
                  "updateLayersData: clear visible region of layer %d", i);
            list->hwLayers[i].hints |= HWC_HINT_CLEAR_FB;
        }

        int bobDeinterlace;
        int srcX = layer->sourceCrop.left;
        int srcY = layer->sourceCrop.top;
        int srcWidth = layer->sourceCrop.right - layer->sourceCrop.left;
        int srcHeight = layer->sourceCrop.bottom - layer->sourceCrop.top;
        int planeType = plane->getPlaneType();

        if(srcHeight == 1 || srcWidth == 1) {
            mLayerList->detachPlane(i, plane);
            layer->compositionType = HWC_FRAMEBUFFER;
            handled = false;
            continue;
        }

        // get & setup overlay data buffer
        IntelDisplayBuffer *buffer = plane->getDataBuffer();
        IntelDisplayDataBuffer *dataBuffer =
            reinterpret_cast<IntelDisplayDataBuffer*>(buffer);
        if (!dataBuffer) {
            ALOGE("%s: invalid overlay data buffer\n", __func__);
            continue;
        }

        // if invalid gralloc buffer handle, throw back this layer to SF
        if (!grallocHandle) {
                ALOGE("%s: invalid buffer handle\n", __func__);
                mLayerList->detachPlane(i, plane);
                layer->compositionType = HWC_FRAMEBUFFER;
                handled = false;
                continue;
        }

        int bufferWidth = grallocHandle->width;
        int bufferHeight = grallocHandle->height;
        uint32_t bufferHandle = grallocHandle->fd[GRALLOC_SUB_BUFFER0];
        int format = grallocHandle->format;
        uint32_t transform = layer->transform;

        if (planeType == IntelDisplayPlane::DISPLAY_PLANE_OVERLAY) {
            if (widiplane) {
                widiplane->setOverlayData(grallocHandle, srcWidth, srcHeight);
                if(widiplane->isBackgroundVideoMode()) {
                    if((mWidiNativeWindow == NULL) &&  widiplane->isStreaming()) {
                        widiplane->getNativeWindow(mWidiNativeWindow);
                        if(mWidiNativeWindow != NULL) {
                            if(mDrm->isMdsSurface(mWidiNativeWindow)) {
                                 widiplane->setNativeWindow(mWidiNativeWindow);
                                 ALOGD_IF(ALLOW_HWC_PRINT,
                                        "Native window is from MDS for widi at composer = 0x%p ", mWidiNativeWindow);
                            }
                            else {
                                mWidiNativeWindow = NULL;
                                ALOGD_IF(ALLOW_HWC_PRINT,"Native window is not from MDS");
                            }
                        }
                    }
                }
                else {
                    mWidiNativeWindow = NULL;
                    ALOGD_IF(ALLOW_HWC_PRINT,
                           "Native window from widiplane for background  = %p", mWidiNativeWindow);
                }
                continue;
            }

            IntelOverlayContext *overlayContext =
                reinterpret_cast<IntelOverlayContext*>(plane->getContext());
            int flags = mLayerList->getFlags(i);

            // disable overlay if DELAY_DISABLE flag was set
            if (flags & IntelDisplayPlane::DELAY_DISABLE) {
                ALOGD_IF(ALLOW_HWC_PRINT,
                       "updateLayerData: disable plane (DELAY)!");
                flags &= ~IntelDisplayPlane::DELAY_DISABLE;
                mLayerList->setFlags(i, flags);
                plane->disable();
            }

            // check if can switch to overlay
            bool useOverlay = useOverlayRotation(layer, i,
                                                 bufferHandle,
                                                 bufferWidth,
                                                 bufferHeight,
                                                 srcX,
                                                 srcY,
                                                 srcWidth,
                                                 srcHeight,
                                                 transform);

            if (!useOverlay) {
                ALOGD_IF(ALLOW_HWC_PRINT,
                       "updateLayerData: useOverlayRotation failed!");
                if (!mLayerList->getForceOverlay(i)) {
                    ALOGD_IF(ALLOW_HWC_PRINT,
                           "updateLayerData: fallback to ST to do rendering!");
                    // fallback to ST to render this frame
                    layer->compositionType = HWC_FRAMEBUFFER;
                    mForceSwapBuffer = true;
                    handled = false;
                }
                // disable overlay when rotated buffer is not ready
                flags |= IntelDisplayPlane::DELAY_DISABLE;
                mLayerList->setFlags(i, flags);
                continue;
            }

            bobDeinterlace = isBobDeinterlace(layer);
            if (bobDeinterlace) {
                flags |= IntelDisplayPlane::BOB_DEINTERLACE;
            } else {
                flags &= ~IntelDisplayPlane::BOB_DEINTERLACE;
            }
            mLayerList->setFlags(i, flags);

            // clear FB first on first overlay frame
            if (layer->compositionType == HWC_FRAMEBUFFER) {
                ALOGD_IF(ALLOW_HWC_PRINT,
                       "updateLayerData: first overlay frame clear fb");
                //layer->hints |= HWC_HINT_CLEAR_FB;
                mForceSwapBuffer = true;
            }

            // switch to overlay
            layer->compositionType = HWC_OVERLAY;

            // gralloc buffer is not aligned to 32 pixels
            uint32_t grallocStride = align_to(bufferWidth, 32);
            int format = grallocHandle->format;

            dataBuffer->setFormat(format);
            dataBuffer->setStride(grallocStride);
            dataBuffer->setWidth(bufferWidth);
            dataBuffer->setHeight(bufferHeight);
            dataBuffer->setCrop(srcX, srcY, srcWidth, srcHeight);
            dataBuffer->setDeinterlaceType(bobDeinterlace);
            // set the data buffer back to plane
            ret = ((IntelOverlayPlane*)plane)->setDataBuffer(bufferHandle,
                                                             transform,
                                                             grallocHandle);
            if (!ret) {
                ALOGE("%s: failed to update overlay data buffer\n", __func__);
                mLayerList->detachPlane(i, plane);
                layer->compositionType = HWC_FRAMEBUFFER;
                handled = false;
            }
        } else if (planeType == IntelDisplayPlane::DISPLAY_PLANE_SPRITE ||
                   planeType == IntelDisplayPlane::DISPLAY_PLANE_PRIMARY) {

            // adjust the buffer format if no blending is needed
            // some test cases would fail due to a weird format!
            if (layer->blending == HWC_BLENDING_NONE) {
                switch (format) {
                    case HAL_PIXEL_FORMAT_BGRA_8888:
                    format = HAL_PIXEL_FORMAT_BGRX_8888;
                    break;
                case HAL_PIXEL_FORMAT_RGBA_8888:
                    format = HAL_PIXEL_FORMAT_RGBX_8888;
                    break;
                }
            }

            // set data buffer format
            dataBuffer->setFormat(format);
            dataBuffer->setWidth(bufferWidth);
            dataBuffer->setHeight(bufferHeight);
            dataBuffer->setCrop(srcX, srcY, srcWidth, srcHeight);
            // set the data buffer back to plane
            ret = plane->setDataBuffer(bufferHandle, transform, grallocHandle);
            if (!ret) {
                ALOGE("%s: failed to update sprite data buffer\n", __func__);
                mLayerList->detachPlane(i, plane);
                layer->compositionType = HWC_FRAMEBUFFER;
                handled = false;
            }
        } else {
            ALOGW("%s: invalid plane type %d\n", __func__, planeType);
            continue;
        }
    }

    return handled;
}

bool IntelMIPIDisplayDevice::flipFramebufferContexts(IMG_hwc_layer_t *imgLayer,
                                                         hwc_layer_1_t *layer)
{
    intel_dc_plane_context_t *context;
    uint32_t fbWidth, fbHeight;
    int zOrderConfig;
    bool forceBottom = false;

    if (!imgLayer || !layer) {
        ALOGE("%s: Invalid plane contexts\n", __func__);
        return false;
    }

    drmModeConnection connection = mDrm->getOutputConnection(mDisplayIndex);
    if (connection != DRM_MODE_CONNECTED)
        return false;

    drmModeFBPtr fbInfo = mDrm->getOutputFBInfo(mDisplayIndex);

    fbWidth = fbInfo->width;
    fbHeight = fbInfo->height;

    zOrderConfig = mPlaneManager->getZOrderConfig(mDisplayIndex);
    if ((zOrderConfig == IntelDisplayPlaneManager::ZORDER_OcOaP) ||
        (zOrderConfig == IntelDisplayPlaneManager::ZORDER_OaOcP))
        forceBottom = true;

    context = &mFBContext;

    // update context
    context->type = DC_PRIMARY_PLANE;
    context->ctx.prim_ctx.update_mask = SPRITE_UPDATE_ALL;
    context->ctx.prim_ctx.index = OUTPUT_MIPI0;
    context->ctx.prim_ctx.pipe = OUTPUT_MIPI0;
    context->ctx.prim_ctx.linoff = 0;
    context->ctx.prim_ctx.stride = align_to((4 * align_to(fbWidth, 32)), 64);
    context->ctx.prim_ctx.pos = 0;
    context->ctx.prim_ctx.size =
        ((fbHeight - 1) & 0xfff) << 16 | ((fbWidth - 1) & 0xfff);
    context->ctx.prim_ctx.surf = 0;

    // config z order; switch z order may cause flicker
    if (forceBottom) {
        context->ctx.prim_ctx.cntr = INTEL_SPRITE_PIXEL_FORMAT_BGRX8888;
    } else {
        context->ctx.prim_ctx.cntr = INTEL_SPRITE_PIXEL_FORMAT_BGRA8888;
    }

    context->ctx.prim_ctx.cntr |= 0x80000000;

    // update IMG layer
    imgLayer->handle = layer->handle;
    imgLayer->transform = 0;
    imgLayer->blending = HWC_BLENDING_NONE;
    imgLayer->sourceCrop.left =0;
    imgLayer->sourceCrop.top = 0;
    imgLayer->sourceCrop.right = fbWidth;
    imgLayer->sourceCrop.bottom = fbHeight;
    imgLayer->displayFrame = imgLayer->sourceCrop;
    imgLayer->custom = (uint32_t)context;

    return true;
}

bool IntelMIPIDisplayDevice::dump(char *buff,
                           int buff_len, int *cur_len)
{
    IntelDisplayPlane *plane = NULL;
    bool ret = true;
    int i;

    mDumpBuf = buff;
    mDumpBuflen = buff_len;
    mDumpLen = (int) (*cur_len);

    if (mLayerList) {
       dumpPrintf("------------ MIPI Totally %d layers -------------\n",
                                     mLayerList->getLayersCount());
       for (i = 0; i < mLayerList->getLayersCount(); i++) {
           plane = mLayerList->getPlane(i);

           if (plane) {
               int planeType = plane->getPlaneType();
               dumpPrintf("   # layer %d attached to %s plane \n", i,
                            (planeType == 3) ? "overlay" : "sprite");
           } else
               dumpPrintf("   # layer %d goes through eglswapbuffer\n ", i);
       }

       dumpPrintf("-------------MIPI runtime parameters -------------\n");
       dumpPrintf("  + mHotplugEvent: %d \n", mHotplugEvent);
       dumpPrintf("  + bypassPost: %d \n", mFBDev->bBypassPost);
       dumpPrintf("  + mForceSwapBuffer: %d \n", mForceSwapBuffer);
       dumpPrintf("  + mForceSwapBuffer: %d \n", mForceSwapBuffer);
       dumpPrintf("  + Display Mode: %d \n", mDrm->getDisplayMode());
    }

    *cur_len = mDumpLen;
    return ret;
}

bool IntelMIPIDisplayDevice::getDisplayConfig(uint32_t* configs,
                                        size_t* numConfigs)
{
    if (!numConfigs || !numConfigs[0])
        return false;

    if (mDrm->getOutputConnection(mDisplayIndex) != DRM_MODE_CONNECTED)
        return false;

    *numConfigs = 1;
    configs[0] = 0;

    return true;
}

bool IntelMIPIDisplayDevice::getDisplayAttributes(uint32_t config,
            const uint32_t* attributes, int32_t* values)
{
    if (config != 0)
        return false;

    if (!attributes || !values)
        return false;

    if (mDrm->getOutputConnection(mDisplayIndex) != DRM_MODE_CONNECTED)
        return false;

    while (*attributes != HWC_DISPLAY_NO_ATTRIBUTE) {
        switch (*attributes) {
        case HWC_DISPLAY_VSYNC_PERIOD:
            *values = 1e9 / mFBDev->base.fps;
            break;
        case HWC_DISPLAY_WIDTH:
            *values = mFBDev->base.width;
            break;
        case HWC_DISPLAY_HEIGHT:
            *values = mFBDev->base.height;
            break;
        case HWC_DISPLAY_DPI_X:
            *values = mFBDev->base.xdpi;
            break;
        case HWC_DISPLAY_DPI_Y:
            *values = mFBDev->base.ydpi;
            break;
        default:
            break;
        }
        attributes ++;
        values ++;
    }

    return true;
}
