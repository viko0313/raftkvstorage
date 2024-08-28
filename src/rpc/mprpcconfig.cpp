#include "mprpcconfig.h"
#include <string>
#include<iostream>

// 负责解析加载配置文件
void MprpcConfig::LoadConfigFile(const char *config_file) {
    FILE *pf = fopen(config_file, "r");
    if (nullptr == pf) {
        std::cout << config_file << "is not exist!" << std::endl;
        exit(EXIT_FAILURE);
    }

    while (!feof(pf)) {
        char buf[512] = {0}; //存储读的每行
        fgets(buf, 512, pf);

        std::string read_buf(buf);
        Trim(read_buf);

        if (read_buf[0] = '#' || read_buf.empty()) {
            continue;
        }

        //解析配置项
        int idx = read_buf.find('=');
        if (idx == -1) {
            continue; //配置项不合法
        }
        std::string key;
        std::string value;
        key = read_buf.substr(0, idx);
        Trim(key);

        //字符串存在\n，要定位到为结尾
        //起始位置参数，用于指定从哪个位置开始查找。
        int endidx = read_buf.find('\n', idx);
        //substr()起始位置和长度
        value = read_buf.substr(idx + 1, endidx - idx - 1);
        Trim(value);
        //m_configMap[key] = value;
        m_configMap.insert({key, value});
    }
    fpclose(pf);
}

//查询配置项信息
std::string MprpcConfig::Load(const std::string &key) {
    auto it = m_configMap.find(key);
    if (it == m_configMap.end()) {
        return "";
    }
    return it->second;
}

// 去掉字符串前后的空格
void MprpcConfig::Trim(std::string &src_buf) {
    //find_first_not_of(' ') 查找字符串中第一个非空格字符的位置。
    int idx = src_buf.find_first_not_of(' ');
    if (idx != -1) {
        //说明字符串前面有空格,idx是空格从idx+1开始
        src_buf = src_buf.substr(idx + 1, src_buf.size() - idx - 1); //更新新字符串
    }
    idx = src_buf.find_last_not_of(' ');
    if (idx != -1) {
        src_buf.substr(0, idx + 1);
    }
}