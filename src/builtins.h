#pragma once
#include "value.h"
#include <string>
#include <vector>

// 全局内置函数的分发入口。
// name 不是内置函数名时返回 false，由调用方继续按普通方式处理。
bool callBuiltin(const std::string& name, std::vector<Value>& args,
                 Value& result, int line);
