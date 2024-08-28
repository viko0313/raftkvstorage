#ifndef MPRPCCONFIG_H
#define MPRPCCONFIG_H
#include <string>
#include <unordered_map>
// rpcserverip   rpcserverport    zookeeperip   zookeeperport
// 框架读取和配置文件类
class MprpcConfig
{
private:
    std::unordered_map<std::string, std::string> m_configmap;
    //去除字符串的空格，自然传进引用最好
    void Trim(std::string &src_buf);

public:
    //负责解析或加载配置文件
    void loadConfigFile(const char *config_file);
    //查询配置项信息
    std::string Load(const std::string &key);
};

#endif