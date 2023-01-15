#ifndef MICRO_GPUSIM_C_CONFIG_READER_H
#define MICRO_GPUSIM_C_CONFIG_READER_H

#include <map>
#include <string>


class gpu_config {
public:
    gpu_config() = default;;
    std::map<std::string, std::string> m_gpu_config = {};
    std::map<std::string, int> m_sm_pipeline_units = {};
    std::map<std::string,int> m_compute_capability = {};
    std::map<std::string,std::string> l1_cache_config={};
    std::map<std::string,std::string> l2_cache_config={};
//    std::map<std::string, std::tuple<int, std::string> > gpu_isa_latency = {};
    std::map<std::string, std::tuple<int , std::string> > gpu_isa_latency = {};


    void read_config(const std::string &file_s);


    static void config_split(const std::string &str, const std::string &pattern, std::string &str1, std::string &str2);

    void read_m_config() {
        read_config("../gpu.config");
        read_config("../gpu_isa_latency.config");
    }
};

#endif //MICRO_GPUSIM_C_CONFIG_READER_H
