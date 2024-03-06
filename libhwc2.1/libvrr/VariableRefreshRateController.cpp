/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "VariableRefreshRateController.h"

#include <android-base/logging.h>
#include <sync/sync.h>
#include <utils/Trace.h>

#include "ExynosHWCHelper.h"
#include "drmmode.h"

#include <chrono>
#include <tuple>

namespace android::hardware::graphics::composer {

const std::string VariableRefreshRateController::kFrameInsertionNodeName = "refresh_ctrl";

namespace {

int64_t getNowNs() {
    const auto t = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}

} // namespace

auto VariableRefreshRateController::CreateInstance(ExynosDisplay* display)
        -> std::shared_ptr<VariableRefreshRateController> {
    if (!display) {
        LOG(ERROR)
                << "VrrController: create VariableRefreshRateController without display handler.";
        return nullptr;
    }
    auto controller = std::shared_ptr<VariableRefreshRateController>(
            new VariableRefreshRateController(display));
    std::thread thread = std::thread(&VariableRefreshRateController::threadBody, controller.get());
    std::string threadName = "VrrCtrl_";
    threadName += display->mIndex == 0 ? "Primary" : "Second";
    int error = pthread_setname_np(thread.native_handle(), threadName.c_str());
    if (error != 0) {
        LOG(WARNING) << "VrrController: Unable to set thread name, error = " << strerror(error);
    }
    thread.detach();
    return controller;
}

VariableRefreshRateController::VariableRefreshRateController(ExynosDisplay* display)
      : mDisplay(display) {
    mState = VrrControllerState::kDisable;
    std::string displayFileNodePath = mDisplay->getPanelSysfsPath();
    if (displayFileNodePath.empty()) {
        LOG(WARNING) << "VrrController: Cannot find file node of display: "
                     << mDisplay->mDisplayName;
    } else {
        mFileNodeWritter = std::make_unique<FileNodeWriter>(displayFileNodePath);
    }
}

VariableRefreshRateController::~VariableRefreshRateController() {
    stopThread(true);

    const std::lock_guard<std::mutex> lock(mMutex);
    if (mLastPresentFence.has_value()) {
        if (close(mLastPresentFence.value())) {
            LOG(ERROR) << "VrrController: close fence file failed, errno = " << errno;
        }
        mLastPresentFence = std::nullopt;
    }
};

int VariableRefreshRateController::notifyExpectedPresent(int64_t timestamp,
                                                         int32_t frameIntervalNs) {
    ATRACE_CALL();
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        mRecord.mNextExpectedPresentTime = {mVrrActiveConfig, timestamp, frameIntervalNs};
        // Post kNotifyExpectedPresentConfig event.
        postEvent(VrrControllerEventType::kNotifyExpectedPresentConfig, getNowNs());
    }
    mCondition.notify_all();
    return 0;
}

void VariableRefreshRateController::reset() {
    ATRACE_CALL();

    const std::lock_guard<std::mutex> lock(mMutex);
    mEventQueue = std::priority_queue<VrrControllerEvent>();
    mRecord.clear();
    dropEventLocked();
    if (mLastPresentFence.has_value()) {
        if (close(mLastPresentFence.value())) {
            LOG(ERROR) << "VrrController: close fence file failed, errno = " << errno;
        }
        mLastPresentFence = std::nullopt;
    }
}

void VariableRefreshRateController::setActiveVrrConfiguration(hwc2_config_t config) {
    LOG(INFO) << "VrrController: Set active Vrr configuration = " << config
              << ", power mode = " << mPowerMode;
    ATRACE_CALL();
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        if (mVrrConfigs.count(config) == 0) {
            LOG(ERROR) << "VrrController: Set an undefined active configuration";
            return;
        }
        mVrrActiveConfig = config;
        if (mState == VrrControllerState::kDisable) {
            return;
        }
        mState = VrrControllerState::kRendering;
        dropEventLocked(kRenderingTimeout);

        const auto& vrrConfig = mVrrConfigs[mVrrActiveConfig];
        postEvent(VrrControllerEventType::kRenderingTimeout,
                  getNowNs() + vrrConfig.notifyExpectedPresentConfig.TimeoutNs);
    }
    mCondition.notify_all();
}

void VariableRefreshRateController::setEnable(bool isEnabled) {
    ATRACE_CALL();
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        if (mEnabled == isEnabled) {
            return;
        }
        mEnabled = isEnabled;
        if (mEnabled == false) {
            dropEventLocked();
        }
    }
    mCondition.notify_all();
}

void VariableRefreshRateController::setPowerMode(int32_t powerMode) {
    ATRACE_CALL();
    LOG(INFO) << "VrrController: Set power mode to " << powerMode;

    {
        const std::lock_guard<std::mutex> lock(mMutex);
        if (mPowerMode == powerMode) {
            return;
        }
        switch (powerMode) {
            case HWC_POWER_MODE_OFF:
            case HWC_POWER_MODE_DOZE:
            case HWC_POWER_MODE_DOZE_SUSPEND: {
                mState = VrrControllerState::kDisable;
                dropEventLocked();
                break;
            }
            case HWC_POWER_MODE_NORMAL: {
                // We should transition from either HWC_POWER_MODE_OFF, HWC_POWER_MODE_DOZE, or
                // HWC_POWER_MODE_DOZE_SUSPEND. At this point, there should be no pending events
                // posted.
                if (!mEventQueue.empty()) {
                    LOG(WARNING) << "VrrController: there should be no pending event when resume "
                                    "from power mode = "
                                 << mPowerMode << " to power mode = " << powerMode;
                    LOG(INFO) << dumpEventQueueLocked();
                }
                mState = VrrControllerState::kRendering;
                const auto& vrrConfig = mVrrConfigs[mVrrActiveConfig];
                postEvent(VrrControllerEventType::kRenderingTimeout,
                          getNowNs() + vrrConfig.notifyExpectedPresentConfig.TimeoutNs);
                break;
            }
            default: {
                LOG(ERROR) << "VrrController: Unknown power mode = " << powerMode;
                return;
            }
        }
        mPowerMode = powerMode;
    }
    mCondition.notify_all();
}

void VariableRefreshRateController::setVrrConfigurations(
        std::unordered_map<hwc2_config_t, VrrConfig_t> configs) {
    ATRACE_CALL();

    for (const auto& it : configs) {
        LOG(INFO) << "VrrController: set Vrr configuration id = " << it.first;
    }

    const std::lock_guard<std::mutex> lock(mMutex);
    mVrrConfigs = std::move(configs);
}

void VariableRefreshRateController::stopThread(bool exit) {
    ATRACE_CALL();
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        mThreadExit = exit;
        mEnabled = false;
        mState = VrrControllerState::kDisable;
    }
    mCondition.notify_all();
}

void VariableRefreshRateController::onPresent(int fence) {
    if (fence < 0) {
        return;
    }
    ATRACE_CALL();
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        if (mState == VrrControllerState::kDisable) {
            return;
        }
        if (!mRecord.mPendingCurrentPresentTime.has_value()) {
            LOG(WARNING) << "VrrController: VrrController: Present without expected present time "
                            "information";
            return;
        } else {
            mRecord.mPresentHistory.next() = mRecord.mPendingCurrentPresentTime.value();
            mRecord.mPendingCurrentPresentTime = std::nullopt;
        }
        if (mState == VrrControllerState::kHibernate) {
            LOG(WARNING) << "VrrController: Present during hibernation without prior notification "
                            "via notifyExpectedPresent.";
            mState = VrrControllerState::kRendering;
            dropEventLocked(kHibernateTimeout);
        }
    }

    // Prior to pushing the most recent fence update, verify the release timestamps of all preceding
    // fences.
    // TODO(b/309873055): delegate the task of executing updateVsyncHistory to the Vrr controller's
    // loop thread in order to reduce the workload of calling thread.
    updateVsyncHistory();
    int dupFence = dup(fence);
    if (dupFence < 0) {
        LOG(ERROR) << "VrrController: duplicate fence file failed." << errno;
    }

    {
        const std::lock_guard<std::mutex> lock(mMutex);
        if (mLastPresentFence.has_value()) {
            LOG(WARNING) << "VrrController: last present fence remains open.";
        }
        mLastPresentFence = dupFence;
        // Drop the out of date timeout.
        dropEventLocked(kRenderingTimeout);
        cancelFrameInsertionLocked();
        // Post next rendering timeout.
        postEvent(VrrControllerEventType::kRenderingTimeout,
                  getNowNs() + mVrrConfigs[mVrrActiveConfig].notifyExpectedPresentConfig.TimeoutNs);
        // Post next frmae insertion event.
        mPendingFramesToInsert = kDefaultNumFramesToInsert;
        postEvent(VrrControllerEventType::kNextFrameInsertion,
                  getNowNs() + kDefaultFrameInsertionTimer);
    }
    mCondition.notify_all();
}

void VariableRefreshRateController::setExpectedPresentTime(int64_t timestampNanos,
                                                           int frameIntervalNs) {
    ATRACE_CALL();

    const std::lock_guard<std::mutex> lock(mMutex);
    mRecord.mPendingCurrentPresentTime = {mVrrActiveConfig, timestampNanos, frameIntervalNs};
}

void VariableRefreshRateController::onVsync(int64_t timestampNanos,
                                            int32_t __unused vsyncPeriodNanos) {
    const std::lock_guard<std::mutex> lock(mMutex);
    mRecord.mVsyncHistory
            .next() = {.mType = VariableRefreshRateController::VsyncEvent::Type::kVblank,
                       .mTime = timestampNanos};
}

void VariableRefreshRateController::cancelFrameInsertionLocked() {
    dropEventLocked(kNextFrameInsertion);
    mPendingFramesToInsert = 0;
}

int VariableRefreshRateController::doFrameInsertionLocked() {
    ATRACE_CALL();

    if (mState == VrrControllerState::kDisable) {
        cancelFrameInsertionLocked();
        return 0;
    }
    if (mPendingFramesToInsert <= 0) {
        LOG(ERROR) << "VrrController: the number of frames to be inserted should >= 1, but is "
                   << mPendingFramesToInsert << " now.";
        return -1;
    }
    bool ret = mFileNodeWritter->WriteCommandString(kFrameInsertionNodeName, PANEL_REFRESH_CTRL_FI);
    if (!ret) {
        LOG(ERROR) << "VrrController: write command to file node failed. " << getStateName(mState)
                   << " " << mPowerMode;
        return -1;
    }
    if (--mPendingFramesToInsert > 0) {
        postEvent(VrrControllerEventType::kNextFrameInsertion,
                  getNowNs() + mVrrConfigs[mVrrActiveConfig].minFrameIntervalNs);
    }
    return 0;
}

int VariableRefreshRateController::doFrameInsertionLocked(int frames) {
    mPendingFramesToInsert = frames;
    return doFrameInsertionLocked();
}

void VariableRefreshRateController::dropEventLocked() {
    mEventQueue = std::priority_queue<VrrControllerEvent>();
    mPendingFramesToInsert = 0;
}

void VariableRefreshRateController::dropEventLocked(VrrControllerEventType event_type) {
    std::priority_queue<VrrControllerEvent> q;
    while (!mEventQueue.empty()) {
        const auto& it = mEventQueue.top();
        if (it.mEventType != event_type) {
            q.push(it);
        }
        mEventQueue.pop();
    }
    mEventQueue = std::move(q);
}

std::string VariableRefreshRateController::dumpEventQueueLocked() {
    std::string content;
    if (mEventQueue.empty()) {
        return content;
    }

    std::priority_queue<VrrControllerEvent> q;
    while (!mEventQueue.empty()) {
        const auto& it = mEventQueue.top();
        content += "VrrController: event = ";
        content += it.toString();
        content += "\n";
        q.push(it);
        mEventQueue.pop();
    }
    mEventQueue = std::move(q);
    return content;
}

int64_t VariableRefreshRateController::getLastFenceSignalTimeUnlocked(int fd) {
    if (fd == -1) {
        return SIGNAL_TIME_INVALID;
    }
    struct sync_file_info* finfo = sync_file_info(fd);
    if (finfo == nullptr) {
        LOG(ERROR) << "VrrController: sync_file_info returned NULL for fd " << fd;
        return SIGNAL_TIME_INVALID;
    }
    if (finfo->status != 1) {
        const auto status = finfo->status;
        if (status < 0) {
            LOG(ERROR) << "VrrController: sync_file_info contains an error: " << status;
        }
        sync_file_info_free(finfo);
        return status < 0 ? SIGNAL_TIME_INVALID : SIGNAL_TIME_PENDING;
    }
    uint64_t timestamp = 0;
    struct sync_fence_info* pinfo = sync_get_fence_info(finfo);
    if (finfo->num_fences != 1) {
        LOG(WARNING) << "VrrController:: there is more than one fence in the file descriptor = "
                     << fd;
    }
    for (size_t i = 0; i < finfo->num_fences; i++) {
        if (pinfo[i].timestamp_ns > timestamp) {
            timestamp = pinfo[i].timestamp_ns;
        }
    }
    sync_file_info_free(finfo);
    return timestamp;
}

int64_t VariableRefreshRateController::getNextEventTimeLocked() const {
    if (mEventQueue.empty()) {
        LOG(WARNING) << "VrrController: event queue should NOT be empty.";
        return -1;
    }
    const auto& event = mEventQueue.top();
    return event.mWhenNs;
}

std::string VariableRefreshRateController::getStateName(VrrControllerState state) const {
    switch (state) {
        case VrrControllerState::kDisable:
            return "Disable";
        case VrrControllerState::kRendering:
            return "Rendering";
        case VrrControllerState::kHibernate:
            return "Hibernate";
        default:
            return "Unknown";
    }
}

void VariableRefreshRateController::handleCadenceChange() {
    ATRACE_CALL();
    if (!mRecord.mNextExpectedPresentTime.has_value()) {
        LOG(WARNING) << "VrrController: cadence change occurs without the expected present timing "
                        "information.";
        return;
    }
    // TODO(b/305311056): handle frame rate change.
    mRecord.mNextExpectedPresentTime = std::nullopt;
}

void VariableRefreshRateController::handleResume() {
    ATRACE_CALL();
    if (!mRecord.mNextExpectedPresentTime.has_value()) {
        LOG(WARNING)
                << "VrrController: resume occurs without the expected present timing information.";
        return;
    }
    // TODO(b/305311281): handle panel resume.
    mRecord.mNextExpectedPresentTime = std::nullopt;
}

void VariableRefreshRateController::handleHibernate() {
    ATRACE_CALL();
    // TODO(b/305311206): handle entering panel hibernate.
    postEvent(VrrControllerEventType::kHibernateTimeout,
              getNowNs() + kDefaultWakeUpTimeInPowerSaving);
}

void VariableRefreshRateController::handleStayHibernate() {
    ATRACE_CALL();
    // TODO(b/305311698): handle keeping panel hibernate.
    postEvent(VrrControllerEventType::kHibernateTimeout,
              getNowNs() + kDefaultWakeUpTimeInPowerSaving);
}

void VariableRefreshRateController::threadBody() {
    struct sched_param param = {.sched_priority = 2};
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        LOG(ERROR) << "VrrController: fail to set scheduler to SCHED_FIFO.";
        return;
    }
    for (;;) {
        bool stateChanged = false;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            if (mThreadExit) break;
            if (!mEnabled) mCondition.wait(lock);
            if (!mEnabled) continue;

            if (mEventQueue.empty()) {
                mCondition.wait(lock);
            }
            int64_t whenNs = getNextEventTimeLocked();
            int64_t nowNs = getNowNs();
            if (whenNs > nowNs) {
                int64_t delayNs = whenNs - nowNs;
                auto res = mCondition.wait_for(lock, std::chrono::nanoseconds(delayNs));
                if (res != std::cv_status::timeout) {
                    continue;
                }
            }
            if (mEventQueue.empty()) {
                LOG(ERROR) << "VrrController: event queue should NOT be empty.";
                break;
            }
            const auto event = mEventQueue.top();
            mEventQueue.pop();

            if (mState == VrrControllerState::kRendering) {
                if (event.mEventType == VrrControllerEventType::kHibernateTimeout) {
                    LOG(ERROR) << "VrrController: receiving a hibernate timeout event while in the "
                                  "rendering state.";
                }
                switch (event.mEventType) {
                    case VrrControllerEventType::kRenderingTimeout: {
                        handleHibernate();
                        mState = VrrControllerState::kHibernate;
                        stateChanged = true;
                        break;
                    }
                    case VrrControllerEventType::kNotifyExpectedPresentConfig: {
                        handleCadenceChange();
                        break;
                    }
                    case VrrControllerEventType::kNextFrameInsertion: {
                        doFrameInsertionLocked();
                        break;
                    }
                    default: {
                        break;
                    }
                }
            } else {
                if (event.mEventType == VrrControllerEventType::kRenderingTimeout) {
                    LOG(ERROR) << "VrrController: receiving a rendering timeout event while in the "
                                  "hibernate state.";
                }
                if (mState != VrrControllerState::kHibernate) {
                    LOG(ERROR) << "VrrController: expecting to be in hibernate, but instead in "
                                  "state = "
                               << getStateName(mState);
                }
                switch (event.mEventType) {
                    case VrrControllerEventType::kHibernateTimeout: {
                        handleStayHibernate();
                        break;
                    }
                    case VrrControllerEventType::kNotifyExpectedPresentConfig: {
                        handleResume();
                        mState = VrrControllerState::kRendering;
                        stateChanged = true;
                        break;
                    }
                    default: {
                        break;
                    }
                }
            }
        }
        // TODO(b/309873055): implement a handler to serialize all outer function calls to the same
        // thread owned by the VRR controller.
        if (stateChanged) {
            updateVsyncHistory();
        }
    }
}

void VariableRefreshRateController::postEvent(VrrControllerEventType type, int64_t when) {
    VrrControllerEvent event;
    event.mEventType = type;
    event.mWhenNs = when;
    mEventQueue.emplace(event);
}

void VariableRefreshRateController::updateVsyncHistory() {
    int fence = -1;

    {
        const std::lock_guard<std::mutex> lock(mMutex);
        if (!mLastPresentFence.has_value()) {
            return;
        }
        fence = mLastPresentFence.value();
        mLastPresentFence = std::nullopt;
    }

    // Execute the following logic unlocked to enhance performance.
    int64_t lastSignalTime = getLastFenceSignalTimeUnlocked(fence);
    if (close(fence)) {
        LOG(ERROR) << "VrrController: close fence file failed, errno = " << errno;
        return;
    } else if (lastSignalTime == SIGNAL_TIME_PENDING || lastSignalTime == SIGNAL_TIME_INVALID) {
        return;
    }

    {
        // Acquire the mutex again to store the vsync record.
        const std::lock_guard<std::mutex> lock(mMutex);
        mRecord.mVsyncHistory
                .next() = {.mType = VariableRefreshRateController::VsyncEvent::Type::kReleaseFence,
                           .mTime = lastSignalTime};
    }
}

} // namespace android::hardware::graphics::composer
