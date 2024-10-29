// Copyright 2024 KVCache.AI
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CONFIG_H
#define CONFIG_H

#include <iostream>
#include <map>
#include <string>

// 配置项结构体
struct Config {
    std::map<std::string, std::string> settings;

    // 添加获取配置值的方法
    std::string get(const std::string &key,
                    const std::string &defaultValue = "") const {
        auto it = settings.find(key);
        if (it != settings.end()) {
            return it->second;
        }
        std::cerr << "use defaultValue, key: " << key
                  << ", defaultvalue: " << defaultValue << std::endl;
        return defaultValue;
    }
};

// 单例类用于管理配置
class ConfigManager {
   public:
    static ConfigManager &getInstance() {
        static ConfigManager instance;
        return instance;
    }

    bool loadConfig(const std::string &filename);

    // 添加获取配置值的方法
    std::string get(const std::string &key,
                    const std::string &defaultValue = "") const {
        return config.get(key, defaultValue);
    }

   private:
    Config config;  // 存储配置信息

    ConfigManager() {}  // 私有构造函数
};

#endif  // CONFIG_H