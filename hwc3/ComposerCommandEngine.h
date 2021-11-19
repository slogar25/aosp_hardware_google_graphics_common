/*
 * Copyright (C) 2021 The Android Open Source Project
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

#pragma once

#include <android/hardware/graphics/composer3/command-buffer.h>
#include <memory>

#include "include/IComposerHal.h"
#include "include/IResourceManager.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class ComposerCommandEngine {
  public:
      ComposerCommandEngine(IComposerHal* hal, IResourceManager* resources)
            : mHal(hal), mResources(resources) {}
      bool init();

      int32_t execute(const std::vector<command::CommandPayload>& commands,
                      std::vector<command::CommandResultPayload> *result);

      void reset() {
          mWriter->reset();
      }

  private:
    void dispatchDisplayCommand(const command::DisplayCommand& displayCommand);
    void dispatchLayerCommand(const command::LayerCommand& displayCommand);

    void executeSetColorTransform(int64_t display, const command::ColorTransformPayload& command);
    void executeSetClientTarget(int64_t display, const command::ClientTarget& command);
    void executeSetOutputBuffer(uint64_t display, const command::Buffer& buffer);
    void executeValidateDisplay(int64_t display);
    void executePresentOrValidateDisplay(int64_t display);
    void executeAcceptDisplayChanges(int64_t display);
    int executePresentDisplay(int64_t display);

    void executeSetLayerCursorPosition(int64_t display, int64_t layer,
                                       const common::Point& cursorPosition);
    void executeSetLayerBuffer(int64_t display, int64_t layer,
                               const command::Buffer& buffer);
    void executeSetLayerSurfaceDamage(int64_t display, int64_t layer,
                const std::vector<std::optional<common::Rect>>& damage);
    void executeSetLayerBlendMode(int64_t display, int64_t layer,
                                  const command::ParcelableBlendMode& blendMode);
    void executeSetLayerColor(int64_t display, int64_t layer, const Color& color);
    void executeSetLayerComposition(int64_t display, int64_t layer,
                                        const command::ParcelableComposition& composition);
    void executeSetLayerDataspace(int64_t display, int64_t layer,
                                  const command::ParcelableDataspace& dataspace);
    void executeSetLayerDisplayFrame(int64_t display, int64_t layer,
                                     const common::Rect& rect);
    void executeSetLayerPlaneAlpha(int64_t display, int64_t layer,
                                   const command::PlaneAlpha& planeAlpha);
    void executeSetLayerSidebandStream(int64_t display, int64_t layer,
                                       const AidlNativeHandle& sidebandStream);
    void executeSetLayerSourceCrop(int64_t display, int64_t layer,
                                   const common::FRect& sourceCrop);
    void executeSetLayerTransform(int64_t display, int64_t layer,
                                  const command::ParcelableTransform& transform);
    void executeSetLayerVisibleRegion(int64_t display, int64_t layer,
                const std::vector<std::optional<common::Rect>>& visibleRegion);
    void executeSetLayerZOrder(int64_t display, int64_t layer,
                               const command::ZOrder& zOrder);
    void executeSetLayerPerFrameMetadata(int64_t display, int64_t layer,
                const std::vector<std::optional<PerFrameMetadata>>& perFrameMetadata);
    void executeSetLayerFloatColor(int64_t display, int64_t layer,
                                   const FloatColor& floatColor);
    void executeSetLayerColorTransform(int64_t display, int64_t layer,
                                       const std::vector<float>& colorTransform);
    void executeSetLayerPerFrameMetadataBlobs(int64_t display, int64_t layer,
                const std::vector<std::optional<PerFrameMetadataBlob>>& perFrameMetadataBlob);
    void executeSetLayerGenericMetadata(int64_t display, int64_t layer,
                                        const command::GenericMetadata& genericMetadata);

    int32_t executeValidateDisplayInternal(int64_t display);

    IComposerHal* mHal;
    IResourceManager* mResources;
    std::unique_ptr<CommandWriterBase> mWriter;
    int32_t mCommandIndex;
};

} // namespace aidl::android::hardware::graphics::composer3::impl

