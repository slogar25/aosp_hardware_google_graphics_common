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

#pragma once

#include <utils/Singleton.h>

#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>

#include <log/log.h>

namespace android::hardware::graphics::composer {

class FileNode {
public:
    FileNode(const std::string& nodePath);
    ~FileNode();

    std::string dump();

    uint32_t getLastWrittenValue(const std::string& nodeName);

    std::optional<std::string> readString(const std::string& nodeName);

    bool WriteUint32(const std::string& nodeName, uint32_t value);

private:
    int getFileHandler(const std::string& nodeName);

    std::string mNodePath;
    std::unordered_map<std::string, int> mFds;
    std::unordered_map<int, uint32_t> mLastWrittenValue;
};

class FileNodeManager : public Singleton<FileNodeManager> {
public:
    FileNodeManager() = default;
    ~FileNodeManager() = default;

    std::shared_ptr<FileNode> getFileNode(const std::string& nodePath) {
        if (mFileNodes.find(nodePath) == mFileNodes.end()) {
            mFileNodes[nodePath] = std::make_shared<FileNode>(nodePath);
        }
        return mFileNodes[nodePath];
    }

private:
    std::unordered_map<std::string, std::shared_ptr<FileNode>> mFileNodes;
};

} // namespace android::hardware::graphics::composer
