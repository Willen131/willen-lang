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

// 把字符串内容转义回可见形式，避免真实换行/制表符破坏逐行输出格式。
static std::string escapeForDisplay(const std::string& s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:   out += ch;     break;
        }
    }
    return out;
}

// willen lex <文件> —— 打印词法单元序列，供词法器调试与测试比对。
// 模板字符串显示为 TEMPLATE，其中文本片段原样输出、插值片段用 [[...]] 标出。
static int cmdLex(const std::string& path) {
    std::string src;
    if (!readFile(path, src)) {
        std::cerr << "无法打开文件：" << path << "\n";
        return 1;
    }
    Lexer lex(src, path);
    std::vector<Token> tokens = lex.tokenize();
    for (const Token& t : tokens) {
        std::cout << t.line << " ";
        if (t.type == Tok::STR_LIT) {
            if (t.isTemplate) {
                std::cout << "TEMPLATE ";
                for (const TemplatePart& p : t.parts) {
                    if (p.isExpr) std::cout << "[[" << p.text << "]]";
                    else          std::cout << escapeForDisplay(p.text);
                }
            } else {
                std::cout << "STR_LIT " << escapeForDisplay(t.text);
            }
        } else {
            std::cout << tokName(t.type);
            if (t.type == Tok::IDENT || t.type == Tok::INT_LIT ||
                t.type == Tok::FLOAT_LIT) {
                std::cout << " " << t.text;
            }
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
