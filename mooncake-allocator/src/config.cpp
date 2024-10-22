
#include "config.h"
#include <fstream>
#include <iostream>
#include <sstream>

bool ConfigManager::loadConfig(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Failed to open config file: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line))
    {
        // 忽略注释和空行
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        size_t pos = line.find('=');
        if (pos != std::string::npos)
        {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            config.settings[key] = value;
        }
    }
    file.close();
    return true;
}
