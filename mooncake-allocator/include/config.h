#ifndef CONFIG_H
#define CONFIG_H

#include <iostream>
#include <map>
#include <string>

// 配置项结构体
struct Config
{
    std::map<std::string, std::string> settings;

    // 添加获取配置值的方法
    std::string get(const std::string &key, const std::string &defaultValue = "") const
    {
        auto it = settings.find(key);
        if (it != settings.end())
        {
            return it->second;
        }
        std::cerr << "use defaultValue, key: " << key << ", defaultvalue: " << defaultValue << std::endl;
        return defaultValue;
    }
};

// 单例类用于管理配置
class ConfigManager
{
public:
    static ConfigManager &getInstance()
    {
        static ConfigManager instance;
        return instance;
    }

    bool loadConfig(const std::string &filename);

    // 添加获取配置值的方法
    std::string get(const std::string &key, const std::string &defaultValue = "") const
    {
        return config.get(key, defaultValue);
    }

private:
    Config config; // 存储配置信息

    ConfigManager() {} // 私有构造函数
};

#endif // CONFIG_H