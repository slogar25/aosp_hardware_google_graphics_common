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

#pragma once

#include <utils/Mutex.h>
#include <condition_variable>
#include <list>
#include <map>
#include <optional>
#include <queue>
#include <thread>

#include "../libdevice/ExynosDisplay.h"
#include "ExternalEventHandlerLoader.h"
#include "RingBuffer.h"
#include "interface/DisplayContextProvider.h"
#include "interface/VariableRefreshRateInterface.h"

namespace android::hardware::graphics::composer {

class VariableRefreshRateController : public VsyncListener,
                                      public PresentListener,
                                      public DisplayContextProvider {
public:
    ~VariableRefreshRateController();

    auto static CreateInstance(ExynosDisplay* display, const std::string& panelName)
            -> std::shared_ptr<VariableRefreshRateController>;

    int notifyExpectedPresent(int64_t timestamp, int32_t frameIntervalNs);

    // Clear historical record data.
    void reset();

    // After setting the active Vrr configuration, we will automatically transition into the
    // rendering state and post the timeout event.
    void setActiveVrrConfiguration(hwc2_config_t config);

    void setEnable(bool isEnabled);

    void setPowerMode(int32_t mode);

    void setVrrConfigurations(std::unordered_map<hwc2_config_t, VrrConfig_t> configs);

    // Inherit from DisplayContextProvider.
    int getAmbientLightSensorOutput() const override;
    BrightnessMode getBrightnessMode() const override;
    int getBrightnessNits() const override;
    int getEstimatedPlaybackFrameRate() const override;
    OperationSpeedMode getOperationSpeedMode() const override;
    bool isProximityThrottingEnabled() const override;

    const DisplayContextProviderInterface* getDisplayContextProviderInterface() const {
        return &mDisplayContextProviderInterface;
    }

private:
    static constexpr int kDefaultRingBufferCapacity = 128;
    static constexpr int64_t kDefaultWakeUpTimeInPowerSaving =
            500 * (std::nano::den / std::milli::den); // 500 ms
    static constexpr int64_t SIGNAL_TIME_PENDING = INT64_MAX;
    static constexpr int64_t SIGNAL_TIME_INVALID = -1;

    static constexpr int64_t kDefaultVendorPresentTimeoutNs =
            33 * (std::nano::den / std::milli::den); // 33 ms

    static constexpr std::string_view kVendorDisplayPanelLibrary = "libdisplaypanel.so";

    enum class VrrControllerState {
        kDisable = 0,
        kRendering,
        kHibernate,
    };

    typedef struct PresentEvent {
        hwc2_config_t config;
        int64_t mTime;
        int mDuration;
    } PresentEvent;

    typedef struct VsyncEvent {
        enum class Type {
            kVblank,
            kReleaseFence,
        };
        Type mType;
        int64_t mTime;
    } VsyncEvent;

    typedef struct VrrRecord {
        static constexpr int kDefaultRingBufferCapacity = 128;

        void clear() {
            mNextExpectedPresentTime = std::nullopt;
            mPendingCurrentPresentTime = std::nullopt;
            mPresentHistory.clear();
            mVsyncHistory.clear();
        }

        std::optional<PresentEvent> mNextExpectedPresentTime = std::nullopt;
        std::optional<PresentEvent> mPendingCurrentPresentTime = std::nullopt;

        typedef RingBuffer<PresentEvent, kDefaultRingBufferCapacity> PresentTimeRecord;
        typedef RingBuffer<VsyncEvent, kDefaultRingBufferCapacity> VsyncRecord;
        PresentTimeRecord mPresentHistory;
        VsyncRecord mVsyncHistory;
    } VrrRecord;

    enum VrrControllerEventType {
        // kSystemRenderingTimeout is responsible for managing present timeout according to the
        // configuration specified in the system HAL API.
        kSystemRenderingTimeout = 0,
        // kVendorRenderingTimeout is responsible for managing present timeout based on the vendor's
        // proprietary definition.
        kVendorRenderingTimeout,
        // kHandleVendorRenderingTimeout is responsible for addressing present timeout by invoking
        // the
        // handling function provided by the vendor.
        kHandleVendorRenderingTimeout,
        kHibernateTimeout,
        kNotifyExpectedPresentConfig,
        // Sensors, outer events...
    };

    struct VrrControllerEvent {
        bool operator<(const VrrControllerEvent& b) const { return mWhenNs > b.mWhenNs; }
        std::string getName() const {
            switch (mEventType) {
                case kSystemRenderingTimeout:
                    return "kSystemRenderingTimeout";
                case kVendorRenderingTimeout:
                    return "kVendorRenderingTimeout";
                case kHandleVendorRenderingTimeout:
                    return "kHandleVendorRenderingTimeout";
                case kHibernateTimeout:
                    return "kHibernateTimeout";
                case kNotifyExpectedPresentConfig:
                    return "NotifyExpectedPresentConfig";
                default:
                    return "Unknown";
            }
        }

        std::string toString() const {
            std::ostringstream os;
            os << "Vrr event: [";
            os << "type = " << getName() << ", ";
            os << "when = " << mWhenNs << "ns]";
            return os.str();
        }
        int64_t mDisplay;
        VrrControllerEventType mEventType;
        int64_t mWhenNs;
        std::function<int()> mFunctor;
    };

    VariableRefreshRateController(ExynosDisplay* display, const std::string& panelName);

    // Implement interface PresentListener.
    virtual void onPresent(int32_t fence) override;
    virtual void setExpectedPresentTime(int64_t timestampNanos, int frameIntervalNs) override;

    // Implement interface VsyncListener.
    virtual void onVsync(int64_t timestamp, int32_t vsyncPeriodNanos) override;

    void cancelPresentTimeoutHandlingLocked();

    void dropEventLocked();
    void dropEventLocked(VrrControllerEventType event_type);

    std::string dumpEventQueueLocked();

    int64_t getLastFenceSignalTimeUnlocked(int fd);

    int64_t getNextEventTimeLocked() const;

    std::string getStateName(VrrControllerState state) const;

    // Functions responsible for state machine transitions.
    void handleCadenceChange();
    void handleResume();
    void handleHibernate();
    void handleStayHibernate();

    void handlePresentTimeout(const VrrControllerEvent& event);

    void postEvent(VrrControllerEventType type, TimedEvent& eventHandleContext);
    void postEvent(VrrControllerEventType type, int64_t when);

    void stopThread(bool exit);

    // The core function of the VRR controller thread.
    void threadBody();

    void updateVsyncHistory();

    ExynosDisplay* mDisplay;

    // The subsequent variables must be guarded by mMutex when accessed.
    std::priority_queue<VrrControllerEvent> mEventQueue;
    VrrRecord mRecord;
    int32_t mPowerMode = -1;
    VrrControllerState mState;
    hwc2_config_t mVrrActiveConfig = -1;
    std::unordered_map<hwc2_config_t, VrrConfig_t> mVrrConfigs;
    std::optional<int> mLastPresentFence;

    std::unique_ptr<FileNodeWriter> mFileNodeWritter;

    DisplayContextProviderInterface mDisplayContextProviderInterface;
    std::unique_ptr<ExternalEventHandlerLoader> mPresentTimeoutEventHandlerLoader;
    ExternalEventHandler* mPresentTimeoutEventHandler = nullptr;

    std::string mPanelName;

    bool mEnabled = false;
    bool mThreadExit = false;

    std::mutex mMutex;
    std::condition_variable mCondition;
};

} // namespace android::hardware::graphics::composer
