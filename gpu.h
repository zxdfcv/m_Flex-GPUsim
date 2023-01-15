//
// Created by 刘浩 on 2022/6/14.
// class gpu sm

#ifndef FLEX_GPUSIM_GPU_H
#define FLEX_GPUSIM_GPU_H
#define GPM_NUM 1
#define g_mod 0

#include <utility>
#include <vector>
#include "config_reader.h"
#include "trace_reader.h"
#include "cache.h"
#include "mem_fetch.h"
#include "crossbar.h"

extern int request_id;
class streaming_multiprocessor;
class sm_unit;

class gpu {
public:
    explicit gpu(std::string benchmark);

    ~gpu() {
        delete traceReader;
        delete m_active_kernel;
    }

    void init_config();

    void build_gpu();

    void creat_icnt(){
        g_icnt_config.in_buffer_limit = 512;
        g_icnt_config.out_buffer_limit = 512;
        g_icnt_config.subnets = 2;
        g_icnt_config.arbiter_algo = NAIVE_RR;
        m_icnt = LocalInterconnect::New(g_icnt_config);
        m_icnt->CreateInterconnect(stoi(m_gpu_configs.m_gpu_config["sm_num"]),stoi(m_gpu_configs.m_gpu_config["mem_num"])); //new X-bar
    }

    void launch_kernel(int kernel_id);

    void execute_kernel(int kernel_id);

    void read_trace(int kernel_id, kernel_info &kernelInfo,
                    std::vector<trace_inst> &trace_insts);

    void first_spawn_block();

    void gpu_cycle();


    gpu_config m_gpu_configs;
    struct inct_config g_icnt_config;
    LocalInterconnect* m_icnt;
    std::string active_benchmark;
    trace_reader* traceReader;
    kernel* m_active_kernel;
    std::vector<streaming_multiprocessor *> m_sm;
    std::vector<std::vector<streaming_multiprocessor *> > g_sm;
//    l2_data_cache* m_l2_data_cache;
    memory_partition* m_memory_partition;
    std::vector<memory_partition *> g_memory_partition;
//    std::vector<int> g_l2_list;
    std::map<int, std::queue<mem_fetch*>> m_queue_l1_to_l2; // bank id: mf
    std::vector<std::pair<mem_fetch*, int>> l2_to_sm; // mf: time



private:
    int max_block_per_sm(kernel_info kernel_info);

    static int max_block_limit_by_others();

    int max_block_limit_by_warps(kernel_info kernelInfo);

    int max_block_limit_by_regs(kernel_info kernelInfo);

    int max_block_limit_by_smem(kernel_info kernelInfo);

    int gpu_sim_cycles;

};


class streaming_multiprocessor {
public:
    streaming_multiprocessor(unsigned sm_id, gpu_config &gpu_config, cache_config &cache_config)
            : m_l1_cache_config(cache_config), m_gpu_config(gpu_config), cycles(0){
        m_sm_id = sm_id;
        is_active = false;
        m_max_blocks = 0;
        m_execute_inst = 0;
        m_queue_sm_to_l1_busy.reset();
        load_units();
    }

    void init(gpu* gpu){
        m_gpu = gpu;
        m_l1_data_cache = new l1_data_cache(m_l1_cache_config, m_gpu_config, m_sm_id, m_queue_sm_to_l1,gpu);
    }

    int schedule_warp(std::vector<warp *> &warp_vec);

    bool check_unit(warp &warp);

    bool check_unit(int delay, const string &unit_name);

    bool check_dependency(warp &);

    bool check_warp_active(warp &warp);

    bool check_at_barrier(warp &warp);

    int issue_inst(warp &);

    void sm_cycle();

    void load_units();

    bool sm_active() {
        for (auto &block: block_vec) {
            if (block->is_active(cycles)) {
                is_active = true;
                return is_active;
            }
        }
        if (m_active_kernel->block_pointer < m_active_kernel->m_blocks.size()) {
            is_active = true;
            return is_active;
        }
        is_active = false;
        return false;
    }

    void del_inactive_block();

    void init_sm_to_l1() {
        int n_banks = m_l1_cache_config.m_n_banks;
        for (int i = 0; i < n_banks; i++) {
            m_queue_sm_to_l1[i] = {};
        }
    }

    block *get_block(int block_id) {
        for (auto &block: block_vec) {
            if (block->m_block_id == block_id) {
                return block;
            }
        }
        printf("ERROR %s:%d block:%d is not found!\n", __FILE__, __LINE__, block_id);
        exit(1);
    }

    void write_back();

    void write_back_request_l2(mem_fetch * mf);

    cache_config m_l1_cache_config;


    l1_data_cache *m_l1_data_cache;
//    l2_data_cache *m_l2_data_cache;


    gpu* m_gpu;
    std::map<std::string, std::vector<sm_unit*> > m_units;
    unsigned m_sm_id;
    int m_max_blocks;
    std::vector<block *> block_vec;
    bool is_active;
    gpu_config m_gpu_config;
    int cycles;
    kernel *m_active_kernel;
    int m_execute_inst;
    std::map<int, std::queue<mem_fetch *>> m_queue_sm_to_l1; // key: bank id value: mf queue
    std::bitset<4> m_queue_sm_to_l1_busy;


    std::vector<mem_fetch *> m_response_fifo;

//    std::map<int, std::queue<mem_fetch *>> &m_queue_l1_to_l2; //to l2
//    std::vector<std::pair<mem_fetch *, int>> &m_l2_to_sm;



//    int block_warp_size = int(kernel.block_size / hardware_info.cc_configs['warp_size']);
//    int max_blocks = max_blocks;
//    vector<block> block_list;  //1:block
//    vector<block> wait_block;  //正在launch的block key:block_id value:block
//    kernel kernel = kernel;

};

class sm_unit{
public:
    sm_unit(std::string name){
        unit_name = std::move(name);
        ready_time = 0;
        mem_fetch_point = 0;
        m_inst = nullptr;
        m_warp = nullptr;
        unit_latency = 0;
    }

    void set_sm(streaming_multiprocessor* sm){
        m_sm = sm;
    }

    void set_ldst_inst(mem_inst* inst, warp* warp){
        m_inst = inst;
        m_warp = warp;
        ready_time = INT_MAX;
        mem_fetch_point = 0; //指向聚合地址的第n个请求
        unit_latency = 4;
    }

    void unit_cycle(int cycles);

    std::string unit_name;
    int ready_time;
    mem_inst* m_inst;
    warp* m_warp;
    int mem_fetch_point;
    streaming_multiprocessor* m_sm;
    int unit_latency;

};

class LdStInst{
public:
    void process(gpu_config& gpu_config_t, sm_unit* unit, warp& warp, int cycles, long long int pc_t, int pc_index_t,
                 int active_thread_num_t, const string& opcode, unsigned sm_id_t,
                 std::map<int, std::queue<mem_fetch *>>& queue_sm_to_l1_t, cache_config& l1_cache_config_t, kernel* active_kernel_t);
    void process_LDG_STG(gpu_config& gpu_config_t, sm_unit* unit, warp& warp, int cycles, long long int pc_t, int pc_index_t,
                         int active_thread_num_t, const string& opcode, unsigned sm_id_t, std::map<int, std::queue<mem_fetch *>>& queue_sm_to_l1_t, cache_config& l1_cache_config_t, kernel*active_kernel_t);
    void process_LDS_STS_ATOMS(gpu_config& gpu_config_t, int active_thread_num_t, sm_unit* unit, warp& warp, int cycles);
    void process_ATOM_ATOMG(sm_unit* unit, warp& warp, long long pc_t, int pc_index_t, unsigned sm_id_t, int cycles, std::map<int, std::queue<mem_fetch *>>& queue_sm_to_l1_t, cache_config& l1_cache_config_t);

};




int ceil(float x, float s);


int floor(float x, float s);

#endif //FLEX_GPUSIM_GPU_H
