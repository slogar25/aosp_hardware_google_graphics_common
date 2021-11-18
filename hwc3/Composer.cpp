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

#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include <android-base/logging.h>

#include "Composer.h"
#include "Util.h"

namespace aidl::android::hardware::graphics::composer3::impl {

ndk::ScopedAStatus Composer::createClient(std::shared_ptr<IComposerClient>* outClient) {
    DEBUG_FUNC();
    std::unique_lock<std::mutex> lock(mClientMutex);
    if (!waitForClientDestroyedLocked(lock)) {
        *outClient = nullptr;
        return TO_BINDER_STATUS(EX_NO_RESOURCES);
    }

    auto client = ndk::SharedRefBase::make<ComposerClient>(mHal.get());
    if (!client || !client->init()) {
        *outClient = nullptr;
        return TO_BINDER_STATUS(EX_NO_RESOURCES);
    }

    auto clientDestroyed = [this]() { onClientDestroyed(); };
    client->setOnClientDestroyed(clientDestroyed);

    mClient = client;
    *outClient = client;

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Composer::dumpDebugInfo(std::string* output) {
    mHal->dumpDebugInfo(output);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Composer::getCapabilities(std::vector<Capability>* caps) {
    DEBUG_FUNC();
    mHal->getCapabilities(caps);
    return ndk::ScopedAStatus::ok();
}

bool Composer::waitForClientDestroyedLocked(std::unique_lock<std::mutex>& lock) {
    if (!mClient.expired()) {
        using namespace std::chrono_literals;

        // In surface flinger we delete a composer client on one thread and
        // then create a new client on another thread. Although surface
        // flinger ensures the calls are made in that sequence (destroy and
        // then create), sometimes the calls land in the composer service
        // inverted (create and then destroy). Wait for a brief period to
        // see if the existing client is destroyed.
        LOG(DEBUG) << "waiting for previous client to be destroyed";
        mClientDestroyedCondition.wait_for(lock, 1s,
                                           [this]() -> bool { return mClient.expired(); });
        if (!mClient.expired()) {
            LOG(DEBUG) << "previous client was not destroyed";
        }
    }

    return mClient.expired() ;
}

void Composer::onClientDestroyed() {
    std::lock_guard<std::mutex> lock(mClientMutex);
    mClient.reset();
    mClientDestroyedCondition.notify_all();
}

} // namespace aidl::android::hardware::graphics::composer3::impl

