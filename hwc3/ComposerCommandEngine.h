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

class ComposerCommandEngine : public CommandReaderBase {
  public:
      ComposerCommandEngine(IComposerHal* hal, IResourceManager* resources)
            : mHal(hal), mResources(resources) {}
      bool init();

      void getOutputMQDescriptor(DescriptorType* outQueue) {
          *outQueue = mWriter->getMQDescriptor();
      }
      bool setInputMQDescriptor(const DescriptorType& descriptor) {
          return setMQDescriptor(descriptor);
      }
      int32_t execute(int32_t inLength, const std::vector<AidlNativeHandle>& inHandles,
                      ExecuteCommandsStatus* status);
      void reset() {
          CommandReaderBase::reset();
          mWriter->reset();
      }

  private:
    bool executeCommand(Command command, uint16_t length);
    // 2.1
    bool executeSelectDisplay(uint16_t length);
    bool executeSelectLayer(uint16_t length);
    bool executeSetColorTransform(uint16_t length);
    bool executeSetClientTarget(uint16_t length);
    bool executeSetOutputBuffer(uint16_t length);
    bool executeValidateDisplay(uint16_t length);
    bool executePresentOrValidateDisplay(uint16_t length);
    bool executeAcceptDisplayChanges(uint16_t length);
    bool executePresentDisplay(uint16_t length);
    bool executeSetLayerCursorPosition(uint16_t length);
    bool executeSetLayerBuffer(uint16_t length);
    bool executeSetLayerSurfaceDamage(uint16_t length);
    bool executeSetLayerBlendMode(uint16_t length);
    bool executeSetLayerColor(uint16_t length);
    bool executeSetLayerCompositionType(uint16_t length);
    bool executeSetLayerDataspace(uint16_t length);
    bool executeSetLayerDisplayFrame(uint16_t length);
    bool executeSetLayerPlaneAlpha(uint16_t length);
    bool executeSetLayerSidebandStream(uint16_t length);
    bool executeSetLayerSourceCrop(uint16_t length);
    bool executeSetLayerTransform(uint16_t length);
    bool executeSetLayerVisibleRegion(uint16_t length);
    bool executeSetLayerZOrder(uint16_t length);
    // 2.2
    bool executeSetLayerPerFrameMetadata(uint16_t length);
    bool executeSetLayerFloatColor(uint16_t length);
    // 2.3
    bool executeSetLayerColorTransform(uint16_t length);
    bool executeSetLayerPerFrameMetadataBlobs(uint16_t length);
    // 2.4
    bool executeSetLayerGenericMetadata(uint16_t length);

    int32_t executeValidateDisplayInternal();
    ndk::ScopedFileDescriptor readAidlFence() {
        // readFence returns a dup'd fd, take the ownership here
        auto fd = readFence();
        return ndk::ScopedFileDescriptor(fd);
    }
    int32_t takeFence(ndk::ScopedFileDescriptor& fence) {
        // take the ownership
        int fd = fence.get();
        *fence.getR() = -1;
        return fd;
    }
    std::vector<int32_t> takeFence(std::vector<ndk::ScopedFileDescriptor>& fences) {
        std::vector<int32_t> rawFences;
        for (auto& sfd : fences) {
            rawFences.push_back(takeFence(sfd));
        }
        return rawFences;
    }

    common::Rect readRect();
    std::vector<common::Rect> readRegion(size_t count);
    common::FRect readFRect();
    FloatColor readFloatColor();
    void readBlob(uint32_t size, void* blob);

    // 64KiB minus a small space for metadata such as read/write pointers
    static constexpr size_t kWriterInitialSize = 64 * 1024 / sizeof(uint32_t) - 16;
    IComposerHal* mHal;
    IResourceManager* mResources;
    std::unique_ptr<CommandWriterBase> mWriter;

    int64_t mCurrentDisplay = 0;
    int64_t mCurrentLayer = 0;
};

} // namespace aidl::android::hardware::graphics::composer3::impl

