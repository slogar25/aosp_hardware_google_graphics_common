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

#include <aidl/android/hardware/graphics/composer3/Command.h>

#include "ComposerCommandEngine.h"
#include "Util.h"

/// The command engine interface is not 'pure' aidl. Conversion to aidl
// structure is done within this class. Don't mix it with impl/translate.
// Expect to have an AIDL command interface in the future.
//
// The initial implementation is a combination of asop ComposerCommandEngine 2.1 to 2.4
// and adapt to aidl structures.
namespace aidl::android::hardware::graphics::composer3::impl {

bool ComposerCommandEngine::init() {
    mWriter = std::make_unique<CommandWriterBase>(kWriterInitialSize);
    return (mWriter != nullptr);
}

int32_t ComposerCommandEngine::execute(int32_t inLength,
                                       const std::vector<AidlNativeHandle>& inHandles,
                                       ExecuteCommandsStatus* status) {
    DEBUG_FUNC();
    // inHandles is not declared as const in AIDL sub code and is not used after this call.
    // drop the const to avoid dup the handles
    auto& handles = const_cast<std::vector<AidlNativeHandle>&>(inHandles);
    if (!readQueue(inLength, std::move(handles))) {
        return IComposerClient::EX_BAD_PARAMETER;
    }

    Command command;
    uint16_t length = 0;
    while (!isEmpty()) {
        if (!beginCommand(&command, &length)) {
            break;
        }

        bool parsed = executeCommand(command, length);
        endCommand();

        if (!parsed) {
            LOG(ERROR) << "failed to parse command: 0x" << std::hex
                       << static_cast<uint32_t>(command)
                       << ", len: " << std::dec << length;
            break;
        }
    }

    if (!isEmpty()) {
        return IComposerClient::EX_BAD_PARAMETER;
    }

    return mWriter->writeQueue(&status->queueChanged, &status->length, &status->handles)
            ? 0
            : IComposerClient::EX_NO_RESOURCES;
}

bool ComposerCommandEngine::executeCommand(Command command, uint16_t length) {
    DEBUG_FUNC();
    switch (command) {
        case Command::SELECT_DISPLAY:
            return executeSelectDisplay(length);
        case Command::SELECT_LAYER:
            return executeSelectLayer(length);
        case Command::SET_COLOR_TRANSFORM:
            return executeSetColorTransform(length);
        case Command::SET_CLIENT_TARGET:
            return executeSetClientTarget(length);
        case Command::SET_OUTPUT_BUFFER:
            return executeSetOutputBuffer(length);
        case Command::VALIDATE_DISPLAY:
            return executeValidateDisplay(length);
        case Command::PRESENT_OR_VALIDATE_DISPLAY:
            return executePresentOrValidateDisplay(length);
        case Command::ACCEPT_DISPLAY_CHANGES:
            return executeAcceptDisplayChanges(length);
        case Command::PRESENT_DISPLAY:
            return executePresentDisplay(length);
        case Command::SET_LAYER_CURSOR_POSITION:
            return executeSetLayerCursorPosition(length);
        case Command::SET_LAYER_BUFFER:
            return executeSetLayerBuffer(length);
        case Command::SET_LAYER_SURFACE_DAMAGE:
            return executeSetLayerSurfaceDamage(length);
        case Command::SET_LAYER_BLEND_MODE:
            return executeSetLayerBlendMode(length);
        case Command::SET_LAYER_COLOR:
            return executeSetLayerColor(length);
        case Command::SET_LAYER_COMPOSITION_TYPE:
            return executeSetLayerCompositionType(length);
        case Command::SET_LAYER_DATASPACE:
            return executeSetLayerDataspace(length);
        case Command::SET_LAYER_DISPLAY_FRAME:
            return executeSetLayerDisplayFrame(length);
        case Command::SET_LAYER_PLANE_ALPHA:
            return executeSetLayerPlaneAlpha(length);
        case Command::SET_LAYER_SIDEBAND_STREAM:
            return executeSetLayerSidebandStream(length);
        case Command::SET_LAYER_SOURCE_CROP:
            return executeSetLayerSourceCrop(length);
        case Command::SET_LAYER_TRANSFORM:
            return executeSetLayerTransform(length);
        case Command::SET_LAYER_VISIBLE_REGION:
            return executeSetLayerVisibleRegion(length);
        case Command::SET_LAYER_Z_ORDER:
            return executeSetLayerZOrder(length);
        case Command::SET_LAYER_PER_FRAME_METADATA:
            return executeSetLayerPerFrameMetadata(length);
        case Command::SET_LAYER_FLOAT_COLOR:
            return executeSetLayerFloatColor(length);
        case Command::SET_LAYER_COLOR_TRANSFORM:
            return executeSetLayerColorTransform(length);
        case Command::SET_LAYER_PER_FRAME_METADATA_BLOBS:
            return executeSetLayerPerFrameMetadataBlobs(length);
        case Command::SET_LAYER_GENERIC_METADATA:
            return executeSetLayerGenericMetadata(length);
        default:
            return false;
    }
}

int32_t ComposerCommandEngine::executeValidateDisplayInternal() {
    std::vector<int64_t> changedLayers;
    std::vector<Composition> compositionTypes;
    uint32_t displayRequestMask = 0x0;
    std::vector<int64_t> requestedLayers;
    std::vector<uint32_t> requestMasks;
    ClientTargetProperty clientTargetProperty{common::PixelFormat::RGBA_8888,
                                              common::Dataspace::UNKNOWN};
    auto err = mHal->validateDisplay(mCurrentDisplay, &changedLayers, &compositionTypes,
                                     &displayRequestMask, &requestedLayers, &requestMasks,
                                     &clientTargetProperty);
    mResources->setDisplayMustValidateState(mCurrentDisplay, false);
    if (!err) {
        mWriter->setChangedCompositionTypes(changedLayers, compositionTypes);
        mWriter->setDisplayRequests(displayRequestMask, requestedLayers, requestMasks);
        mWriter->setClientTargetProperty(clientTargetProperty);
    } else {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }
    return err;
}

bool ComposerCommandEngine::executeSelectDisplay(uint16_t length) {
    if (length != CommandWriterBase::kSelectDisplayLength) {
        return false;
    }

    mCurrentDisplay = read64();
    mWriter->selectDisplay(mCurrentDisplay);

    return true;
}

bool ComposerCommandEngine::executeSelectLayer(uint16_t length) {
    if (length != CommandWriterBase::kSelectLayerLength) {
        return false;
    }

    mCurrentLayer = read64();

    return true;
}

bool ComposerCommandEngine::executeSetColorTransform(uint16_t length) {
    if (length != CommandWriterBase::kSetColorTransformLength) {
        return false;
    }

    std::vector<float> matrix(16);
    for (int i = 0; i < 16; i++) {
        matrix[i] = readFloat();
    }
    auto transform = readSigned();

    auto err = mHal->setColorTransform(mCurrentDisplay, matrix, transform);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetClientTarget(uint16_t length) {
    // 4 parameters followed by N rectangles
    if ((length - 4) % 4 != 0) {
        return false;
    }

    bool useCache = false;
    auto slot = read();
    auto handle = readHandle(&useCache);
    auto fence = readAidlFence();
    common::Dataspace dataspace = static_cast<common::Dataspace>(readSigned());
    auto damage = readRegion((length - 4) / 4);
    buffer_handle_t clientTarget;

    auto bufferReleaser = mResources->createReleaser(true);
    auto err = mResources->getDisplayClientTarget(mCurrentDisplay, slot, useCache, handle,
                                                  clientTarget, bufferReleaser.get());
    if (!err) {
        err = mHal->setClientTarget(mCurrentDisplay, clientTarget, fence, dataspace,
                                    damage);
    } else {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetOutputBuffer(uint16_t length) {
    if (length != CommandWriterBase::kSetOutputBufferLength) {
        return false;
    }

    bool useCache = false;
    auto slot = read();
    auto handle = readHandle(&useCache);
    auto fence = readAidlFence();
    buffer_handle_t outputBuffer;

    auto bufferReleaser = mResources->createReleaser(true);
    auto err = mResources->getDisplayOutputBuffer(mCurrentDisplay, slot, useCache, handle,
                                                  outputBuffer, bufferReleaser.get());
    if (!err) {
        err = mHal->setOutputBuffer(mCurrentDisplay, outputBuffer, fence);
    } else {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeValidateDisplay(uint16_t length) {
    if (length != CommandWriterBase::kValidateDisplayLength) {
        return false;
    }
    executeValidateDisplayInternal();
    return true;
}

bool ComposerCommandEngine::executePresentOrValidateDisplay(uint16_t length) {
    if (length != CommandWriterBase::kPresentOrValidateDisplayLength) {
        return false;
    }

    // First try to Present as is.
    if (mHal->hasCapability(Capability::SKIP_VALIDATE)) {
        ndk::ScopedFileDescriptor presentFence;
        std::vector<int64_t> layers;
        std::vector<ndk::ScopedFileDescriptor> fences;
        auto err = mResources->mustValidateDisplay(mCurrentDisplay)
                ? IComposerClient::EX_NOT_VALIDATED
                : mHal->presentDisplay(mCurrentDisplay, presentFence, &layers, &fences);
        if (!err) {
            mWriter->setPresentOrValidateResult(1);
            // ownership is transferred to Writer
            mWriter->setPresentFence(takeFence(presentFence));
            mWriter->setReleaseFences(layers, takeFence(fences));
            return true;
        }
    }

    // Present has failed. We need to fallback to validate
    auto err = executeValidateDisplayInternal();
    if (!err) {
        mWriter->setPresentOrValidateResult(0);
    }

    return true;
}

bool ComposerCommandEngine::executeAcceptDisplayChanges(uint16_t length) {
    if (length != CommandWriterBase::kAcceptDisplayChangesLength) {
        return false;
    }

    auto err = mHal->acceptDisplayChanges(mCurrentDisplay);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executePresentDisplay(uint16_t length) {
    if (length != CommandWriterBase::kPresentDisplayLength) {
        return false;
    }

    ndk::ScopedFileDescriptor presentFence;
    std::vector<int64_t> layers;
    std::vector<ndk::ScopedFileDescriptor> fences;
    auto err = mHal->presentDisplay(mCurrentDisplay, presentFence, &layers, &fences);
    if (!err) {
        // ownership is transferred to Writer
        mWriter->setPresentFence(takeFence(presentFence));
        mWriter->setReleaseFences(layers, takeFence(fences));
    } else {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerCursorPosition(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerCursorPositionLength) {
        return false;
    }

    auto err = mHal->setLayerCursorPosition(mCurrentDisplay, mCurrentLayer, readSigned(),
                                            readSigned());
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerBuffer(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerBufferLength) {
        return false;
    }

    bool useCache = false;
    auto slot = read();
    auto handle = readHandle(&useCache);
    auto fence = readAidlFence();

    buffer_handle_t buffer;
    auto bufferReleaser = mResources->createReleaser(true);
    auto err = mResources->getLayerBuffer(mCurrentDisplay, mCurrentLayer, slot, useCache,
                                          handle, buffer, bufferReleaser.get());
    if (!err) {
        err = mHal->setLayerBuffer(mCurrentDisplay, mCurrentLayer, buffer, fence);
    } else {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerSurfaceDamage(uint16_t length) {
    // N rectangles
    if (length % 4 != 0) {
        return false;
    }

    auto damage = readRegion(length / 4);
    auto err = mHal->setLayerSurfaceDamage(mCurrentDisplay, mCurrentLayer, damage);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerBlendMode(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerBlendModeLength) {
        return false;
    }

    BlendMode mode = static_cast<BlendMode>(readSigned());
    auto err = mHal->setLayerBlendMode(mCurrentDisplay, mCurrentLayer, mode);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerColor(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerColorLength) {
        return false;
    }

    auto err = mHal->setLayerColor(mCurrentDisplay, mCurrentLayer, readColor());
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerCompositionType(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerCompositionTypeLength) {
        return false;
    }

    Composition type = static_cast<Composition>(readSigned());
    auto err = mHal->setLayerCompositionType(mCurrentDisplay, mCurrentLayer, type);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerDataspace(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerDataspaceLength) {
        return false;
    }

    common::Dataspace dataspace = static_cast<common::Dataspace>(readSigned());
    auto err = mHal->setLayerDataspace(mCurrentDisplay, mCurrentLayer, dataspace);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerDisplayFrame(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerDisplayFrameLength) {
        return false;
    }

    auto err = mHal->setLayerDisplayFrame(mCurrentDisplay, mCurrentLayer, readRect());
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerPlaneAlpha(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerPlaneAlphaLength) {
        return false;
    }

    auto err = mHal->setLayerPlaneAlpha(mCurrentDisplay, mCurrentLayer, readFloat());
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerSidebandStream(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerSidebandStreamLength) {
        return false;
    }

    bool useCache;
    auto handle = readHandle(&useCache);
    buffer_handle_t stream;

    auto bufferReleaser = mResources->createReleaser(false);
    auto err = mResources->getLayerSidebandStream(mCurrentDisplay, mCurrentLayer, handle,
                                                  stream, bufferReleaser.get());
    if (err) {
        err = mHal->setLayerSidebandStream(mCurrentDisplay, mCurrentLayer, stream);
    }
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerSourceCrop(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerSourceCropLength) {
        return false;
    }

    auto err = mHal->setLayerSourceCrop(mCurrentDisplay, mCurrentLayer, readFRect());
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerTransform(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerTransformLength) {
        return false;
    }

    common::Transform transform = static_cast<common::Transform>(readSigned());
    auto err = mHal->setLayerTransform(mCurrentDisplay, mCurrentLayer, transform);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerVisibleRegion(uint16_t length) {
    // N rectangles
    if (length % 4 != 0) {
        return false;
    }

    auto region = readRegion(length / 4);
    auto err = mHal->setLayerVisibleRegion(mCurrentDisplay, mCurrentLayer, region);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerZOrder(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerZOrderLength) {
        return false;
    }

    auto err = mHal->setLayerZOrder(mCurrentDisplay, mCurrentLayer, read());
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerPerFrameMetadata(uint16_t length) {
    // (key, value) pairs
    if (length % 2 != 0) {
        return false;
    }

    std::vector<PerFrameMetadata> metadata;
    metadata.reserve(length / 2);
    while (length > 0) {
        metadata.emplace_back(
                PerFrameMetadata{static_cast<PerFrameMetadataKey>(readSigned()), readFloat()});
        length -= 2;
    }

    auto err = mHal->setLayerPerFrameMetadata(mCurrentDisplay, mCurrentLayer, metadata);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerFloatColor(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerFloatColorLength) {
        return false;
    }

    auto err = mHal->setLayerFloatColor(mCurrentDisplay, mCurrentLayer, readFloatColor());
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerColorTransform(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerColorTransformLength) {
        return false;
    }

    std::vector<float> matrix(16);
    for (int i = 0; i < 16; i++) {
        matrix[i] = readFloat();
    }
    auto err = mHal->setLayerColorTransform(mCurrentDisplay, mCurrentLayer, matrix);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerPerFrameMetadataBlobs(uint16_t length) {
    // must have at least one metadata blob
    // of at least size 1 in queue (i.e {/*numBlobs=*/1, key, size, blob})
    if (length < 4) {
        return false;
    }

    uint32_t numBlobs = read();
    length--;

    std::vector<PerFrameMetadataBlob> metadata;

    for (size_t i = 0; i < numBlobs; i++) {
        PerFrameMetadataKey key = static_cast<PerFrameMetadataKey>(readSigned());
        uint32_t blobSize = read();

        length -= 2;

        if (length * sizeof(uint32_t) < blobSize) {
            return false;
        }

        metadata.push_back({key, std::vector<uint8_t>()});
        PerFrameMetadataBlob& metadataBlob = metadata.back();
        metadataBlob.blob.resize(blobSize);
        readBlob(blobSize, metadataBlob.blob.data());
    }
    auto err = mHal->setLayerPerFrameMetadataBlobs(mCurrentDisplay, mCurrentLayer, metadata);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }
    return true;
}

bool ComposerCommandEngine::executeSetLayerGenericMetadata(uint16_t length) {
    // We expect at least two buffer lengths and a mandatory flag
    if (length < 3) {
        return false;
    }

    const uint32_t keySize = read();
    std::string key;
    key.resize(keySize);
    readBlob(keySize, key.data());

    const bool mandatory = read();

    const uint32_t valueSize = read();
    std::vector<uint8_t> value(valueSize);
    readBlob(valueSize, value.data());

    auto err =
            mHal->setLayerGenericMetadata(mCurrentDisplay, mCurrentLayer, key, mandatory, value);
    if (err) {
        LOG(WARNING) << __func__ << ": err " << err;
        mWriter->setError(getCommandLoc(), err);
    }

    return true;
}

common::Rect ComposerCommandEngine::readRect() {
    return common::Rect{
            readSigned(),
            readSigned(),
            readSigned(),
            readSigned(),
    };
}

std::vector<common::Rect> ComposerCommandEngine::readRegion(size_t count) {
    std::vector<common::Rect> region;
    region.reserve(count);
    while (count > 0) {
        region.emplace_back(readRect());
        count--;
    }

    return region;
}

common::FRect ComposerCommandEngine::readFRect() {
    return common::FRect {
            readFloat(),
            readFloat(),
            readFloat(),
            readFloat(),
    };
}

FloatColor ComposerCommandEngine::readFloatColor() {
    return FloatColor {
            readFloat(),
            readFloat(),
            readFloat(),
            readFloat(),
    };
}

void ComposerCommandEngine::readBlob(uint32_t size, void* blob) {
    memcpy(blob, &mData[mDataRead], size);
    uint32_t numElements = size / sizeof(uint32_t);
    mDataRead += numElements;
    mDataRead += (size - numElements * sizeof(uint32_t) != 0) ? 1 : 0;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
