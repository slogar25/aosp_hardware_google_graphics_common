/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "FileNode.h"

#include <log/log.h>
#include <sstream>

namespace android {

ANDROID_SINGLETON_STATIC_INSTANCE(hardware::graphics::composer::FileNodeManager);

namespace hardware::graphics::composer {

FileNode::FileNode(const std::string& nodePath) : mNodePath(nodePath) {}

FileNode::~FileNode() {
    for (auto& fd : mFds) {
        close(fd.second);
    }
}

std::string FileNode::dump() {
    std::ostringstream os;
    os << "FileNode: root path: " << mNodePath << std::endl;
    for (const auto& item : mFds) {
        auto lastWrittenValue = getLastWrittenValue(item.first);
        os << "FileNode: sysfs node = " << item.first << ", last written value = 0x" << std::setw(8)
           << std::setfill('0') << std::hex << lastWrittenValue << std::endl;
    }
    return os.str();
}

uint32_t FileNode::getLastWrittenValue(const std::string& nodeName) {
    int fd = getFileHandler(nodeName);
    if ((fd < 0) || (mLastWrittenValue.count(fd) <= 0)) return 0;
    return mLastWrittenValue[fd];
}

std::optional<std::string> FileNode::readString(const std::string& nodeName) {
    std::string fullPath = mNodePath + nodeName;
    std::ifstream ifs(fullPath);
    if (ifs) {
        std::ostringstream os;
        os << ifs.rdbuf(); // reading data
        return os.str();
    }
    return std::nullopt;
}

bool FileNode::WriteUint32(const std::string& nodeName, uint32_t value) {
    int fd = getFileHandler(nodeName);
    if (fd >= 0) {
        std::string cmdString = std::to_string(value);
        int ret = write(fd, cmdString.c_str(), std::strlen(cmdString.c_str()));
        if (ret < 0) {
            ALOGE("Write 0x%x to file node %s%s failed, ret = %d errno = %d", value,
                  mNodePath.c_str(), nodeName.c_str(), ret, errno);
            return false;
        }
    } else {
        ALOGE("Write to invalid file node %s%s", mNodePath.c_str(), nodeName.c_str());
        return false;
    }
    mLastWrittenValue[fd] = value;
    return true;
}

int FileNode::getFileHandler(const std::string& nodeName) {
    if (mFds.count(nodeName) > 0) {
        return mFds[nodeName];
    }
    std::string fullPath = mNodePath + nodeName;
    int fd = open(fullPath.c_str(), O_WRONLY, 0);
    if (fd < 0) {
        ALOGE("Open file node %s failed, fd = %d", fullPath.c_str(), fd);
        return fd;
    }
    mFds[nodeName] = fd;
    return fd;
}

}; // namespace hardware::graphics::composer
}; // namespace android
