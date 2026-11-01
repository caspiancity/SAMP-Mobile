#pragma once
#include <string>
namespace LuaManager {
    bool Init();
    void Shutdown();
    bool OnCommand(const std::string& cmd, const std::string& args);
}