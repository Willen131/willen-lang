#include "builtins.h"
#include <iostream>

bool callBuiltin(const std::string& name, std::vector<Value>& args,
                 Value& result, int line) {
    (void)line;

    // 打印：接受任意个参数，以空格分隔后换行
    if (name == "打印") {
        for (size_t i = 0; i < args.size(); i++) {
            if (i) std::cout << " ";
            std::cout << args[i].toString();
        }
        std::cout << "\n";
        result = Value{};
        return true;
    }

    return false;
}
