#include "lexer.h"
#include <fcntl.h>
#include <io.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <windows.h>

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// willen lex <文件> —— 打印词法单元序列，供词法器调试与测试比对。
static int cmdLex(const std::string& path) {
    std::string src;
    if (!readFile(path, src)) {
        std::cerr << "无法打开文件：" << path << "\n";
        return 1;
    }
    Lexer lex(src, path);
    std::vector<Token> tokens = lex.tokenize();
    for (const Token& t : tokens) {
        std::cout << t.line << " " << tokName(t.type);
        if (t.type == Tok::IDENT || t.type == Tok::INT_LIT ||
            t.type == Tok::FLOAT_LIT) {
            std::cout << " " << t.text;
        }
        std::cout << "\n";
    }
    return lex.hadError() ? 1 : 0;
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 关闭 stdout 的文本模式换行转换：Windows 下文本模式会把 "\n"
    // 写成 "\r\n"，导致同一份期望输出在 Windows 与其它平台字节不一致，
    // 测试比对与 Git 版本管理都会出现假差异。
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);

    if (argc < 3) {
        std::cerr << "用法：willen <命令> <文件>\n"
                  << "命令：\n"
                  << "  lex <文件>   打印词法单元\n";
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "lex") return cmdLex(argv[2]);

    std::cerr << "未知命令：" << cmd << "\n";
    return 1;
}
