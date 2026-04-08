#pragma once
// config_parser.h — 极简 INI 配置解析器 (header-only)
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>

class ConfigParser {
public:
    bool Load(const std::string& path) {
        std::ifstream fs(path);
        if (!fs.is_open()) return false;
        std::string line, sec;
        while (std::getline(fs, line)) {
            Trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            if (line.front() == '[' && line.back() == ']') {
                sec = line.substr(1, line.size() - 2);
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            Trim(k); Trim(v);
            // 去除引号
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                v = v.substr(1, v.size() - 2);
            data_[sec][k] = v;
        }
        return true;
    }

    std::string Get(const std::string& sec, const std::string& key,
                    const std::string& def = "") const {
        auto s = data_.find(sec);
        if (s == data_.end()) return def;
        auto k = s->second.find(key);
        return (k != s->second.end()) ? k->second : def;
    }

    int GetInt(const std::string& sec, const std::string& key, int def = 0) const {
        auto v = Get(sec, key);
        return v.empty() ? def : std::stoi(v);
    }

    bool GetBool(const std::string& sec, const std::string& key, bool def = false) const {
        auto v = Get(sec, key);
        if (v.empty()) return def;
        return v == "true" || v == "1" || v == "yes";
    }

    double GetDouble(const std::string& sec, const std::string& key, double def = 0.0) const {
        auto v = Get(sec, key);
        return v.empty() ? def : std::stod(v);
    }

    std::vector<std::string> GetList(const std::string& sec, const std::string& key) const {
        std::vector<std::string> res;
        std::istringstream iss(Get(sec, key));
        std::string item;
        while (std::getline(iss, item, ',')) { Trim(item); if (!item.empty()) res.push_back(item); }
        return res;
    }

private:
    static void Trim(std::string& s) {
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    }
    std::map<std::string, std::map<std::string, std::string>> data_;
};
