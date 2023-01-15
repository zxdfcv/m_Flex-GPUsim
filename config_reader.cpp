//
// Created by 徐向荣 on 2022/6/2.
//

#include "config_reader.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <algorithm>
#include <queue>
#include <sstream>

void gpu_config::read_config(const std::string &file_s) {
    std::ifstream file(file_s);
    std::string single_line;
    std::string comments = "//";
    std::string tmp_s = "##";
    std::string end_config = "####";
    while (std::getline(file, single_line)) {
        if (single_line.compare(0, comments.size(), comments) == 0) {
            continue;
        }
        std::string key, value;
        config_split(single_line, ":", key, value);
        if (key == "##pipeline_units") {
            std::getline(file, single_line);
            config_split(single_line, ":", key, value);
            while (single_line.compare(0, tmp_s.size(), tmp_s) != 0) {
                m_sm_pipeline_units.insert(std::pair<std::string, int>(key, stoi(value)));
//                std::cout << key << " " << value << std::endl;
                std::getline(file, single_line);
                config_split(single_line, ":", key, value);
            }
        }
        if (key == "##compute_capability") {
            std::getline(file, single_line);
            config_split(single_line, ":", key, value);
            while (single_line.compare(0, tmp_s.size(), tmp_s) != 0) {
                m_compute_capability.insert(std::pair<std::string, int>(key, stoi(value)));
//                std::cout << key << " " << value << std::endl;
                std::getline(file, single_line);
                config_split(single_line, ":", key, value);
            }
        }
        if (key == "##l1_cache_config") {
            std::getline(file, single_line);
            config_split(single_line, ":", key, value);
            while (single_line.compare(0, tmp_s.size(), tmp_s) != 0) {
                l1_cache_config.insert(std::pair<std::string, std::string>(key, value));
//                std::cout << key << " " << value << std::endl;
                std::getline(file, single_line);
                config_split(single_line, ":", key, value);
            }
        }
        if (key == "##l2_cache_config") {
            std::getline(file, single_line);
            config_split(single_line, ":", key, value);
            while (single_line.compare(0, tmp_s.size(), tmp_s) != 0) {
                l2_cache_config.insert(std::pair<std::string, std::string>(key, value));
//                std::cout << key << " " << value << std::endl;
                std::getline(file, single_line);
                config_split(single_line, ":", key, value);
            }
        }
        if (key == "##gpu_isa_latency") {
            while (std::getline(file, single_line)) {

                if (single_line[0] == '/') {
                    continue; // "/..." => 注释;
                }
                if (single_line.compare(0, end_config.size(), end_config) == 0) {
                    break; // if "####" end;
                }
                if (single_line.length() > 1 && single_line.find(tmp_s) == 0) {
                    continue; // "##..." => 注释, but "####" not.
                }


                std::istringstream is(single_line); // String is = single_line;
                std::string str;
                std::queue<std::string> tmp_str; //queue
                while (is >> str) { // (is == abc cde def) => 分别将abc、cde、def入栈;
                    tmp_str.push(str);
                }
                std::string opcode = tmp_str.front(); // 如上，则 opcode = "abc";
                tmp_str.pop(); // then pop "abc";
                int latency; // 延迟
                std::string latency_t = (tmp_str.front()); // String latency_t = "cde";
                if (latency_t == "-") {
                    latency = -1;
                } else {
                    latency = std::stoi(tmp_str.front()); // string "cde" to int
                }
                tmp_str.pop(); // then pop "cde";
                std::string unit = tmp_str.front(); // String unit = "def";
                std::tuple<int, std::string> tuple_t(latency, unit);
                gpu_isa_latency.insert(std::pair<std::string, std::tuple<int, std::string> >(opcode, tuple_t));
//                std::cout << opcode << " " << latency << unit << std::endl;
            }
            key = end_config;
        }
        if(key!=end_config){
            m_gpu_config.insert(std::pair<std::string, std::string>(key, value));
        }
//        std::cout << key << " " << value << std::endl;
    }
    file.close();
}

void
gpu_config::config_split(const std::string &str, const std::string &pattern, std::string &str1, std::string &str2) {
    std::string strs = str + pattern;
    std::size_t pos = strs.find(pattern);
    std::size_t size = strs.size();
    bool bStop = false;
    while (pos != std::string::npos) {
        if (!bStop) {
            str1 = strs.substr(0, pos);
            bStop = true;
        } else {
            str2 = strs.substr(0, pos);
            return;
        }
        strs = strs.substr(pos + 1, size);
        pos = strs.find(pattern);
    }
}
