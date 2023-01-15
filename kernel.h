//
// Created by 徐向荣 on 2022/6/19.
//

#ifndef FLEX_GPUSIM_KERNEL_H
#define FLEX_GPUSIM_KERNEL_H

#include <string>
#include <utility>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <map>
#include <cmath>
#include <queue>
#include <bitset>
#include <fstream>
#include <iostream>
#include "cache.h"



#define SECTOR_SIZE 32

using std::string;

class static_warp;
class warp;
class streaming_multiprocessor;


const unsigned WARP_PER_CTA_MAX = 64;


class trace_inst {
public:
    trace_inst(int block_id,int warp_id, long long pc, std::string active_mask, int dest_num, std::vector<std::string> dest_regs,
               std::string opcode,
               int src_num, std::vector<std::string> src_regs, int pc_index) {
        m_block_id = block_id;
        m_warp_id = warp_id;
        m_pc = pc;
        m_active_mask = std::move(active_mask);
        m_dest_num = dest_num;
        m_dest_regs = std::move(dest_regs);
        m_opcode = std::move(opcode);
        m_src_num = src_num;
        m_src_regs = std::move(src_regs);
        m_pc_index = pc_index;
        m_active_thread_num = 0;
        count_active_thread();
    }

    int m_block_id;
    int m_warp_id;
    long long m_pc;
    int m_pc_index;
    std::string m_active_mask;
    int m_active_thread_num;
    int m_dest_num;
    std::vector<std::string> m_dest_regs;
    std::string m_opcode;
    int m_src_num;
    std::vector<std::string> m_src_regs;

    void count_active_thread() {
        std::map<char, int> h2b = {
                {'0', 0}, //0000
                {'1', 1}, //0001
                {'2', 1}, //0010
                {'3', 2}, //0011
                {'4', 1}, //0100
                {'5', 2}, //0101
                {'6', 2}, //0110
                {'7', 3}, //0111
                {'8', 1}, //1000
                {'9', 2}, //1001
                {'a', 2}, //1010
                {'b', 3}, //1011
                {'c', 2}, //1100
                {'d', 3}, //1101
                {'e', 3}, //1110
                {'f', 4}  //1111
        };
        for (char &i: m_active_mask) {
            m_active_thread_num += h2b[i];
        }
    }

};

class mem_inst {
public:
    mem_inst(std::string opcode, std::vector<std::pair<long long, int>> coalesced_address, long long pc, int pc_index) :
            m_opcode(std::move(opcode)), m_coalesced_address(std::move(coalesced_address)), m_pc(pc),
            m_pc_index(pc_index) {
        completions_index = 0;
    }

    std::string m_opcode;
    std::vector<std::pair<long long, int>> m_coalesced_address;
    long long m_pc;
    int m_pc_index;
    int completions_index;
};

class inst {
public:
    inst(const trace_inst &m_trace_inst) {
        m_block_id = m_trace_inst.m_block_id;
        m_warp_id = m_trace_inst.m_warp_id;
        m_pc = m_trace_inst.m_pc;
        m_active_mask = m_trace_inst.m_active_mask;
        m_active_thread_num = m_trace_inst.m_active_thread_num;
        m_dest_num = m_trace_inst.m_dest_num;
        m_dest_regs = m_trace_inst.m_dest_regs;
        if (m_trace_inst.m_opcode.find('.') != string::npos) {
            m_opcode = m_trace_inst.m_opcode.substr(0, m_trace_inst.m_opcode.find('.'));
        } else {
            m_opcode = m_trace_inst.m_opcode;
        }
        m_src_num = m_trace_inst.m_src_num;
        m_src_regs = m_trace_inst.m_src_regs;
        m_pc_index = m_trace_inst.m_pc_index;
    }

    inst(){

    }

    int m_block_id;
    int m_warp_id;
    long long m_pc;
    std::string m_active_mask;
    int m_active_thread_num;
    int m_dest_num;
    std::vector<std::string> m_dest_regs;
    std::string m_opcode;
    int m_src_num;
    std::vector<std::string> m_src_regs;
    int m_pc_index;
    std::vector<int> m_dependency;

    bool is_atomic() {
        return m_opcode == "ATOM" || m_opcode == "ATOMS" || m_opcode == "ATOMG";
    }
};

class block;
class warp {
public:
    warp(int block_id, int warp_id, static_warp *static_warp, block*block) {
        m_block_id = block_id;
        m_warp_id = warp_id;
        warp_point = 0;
        active = true;
        syncing = false;
        max_dep = 0;
        m_static_warp = static_warp;
        m_at_barrier = false;
        m_block = block;
        thread_mask = 0;
        m_pending_mem_request_num = 0;
        pending_inst = 0;
    }

    warp(int block_id, int warp_id, block* block) {
        m_block_id = block_id;
        m_warp_id = warp_id;
        warp_point = 0;
        active = true;
        syncing = false;
        max_dep = 0;
        thread_mask = 0;
        m_block = block;
        m_pending_mem_request_num = 0;
        pending_inst = 0;
    }

    mem_inst *get_mem_inst(long long pc, int pc_index) {
        for (auto &i: m_mem_inst) {
            if (i->m_pc == pc && i->m_pc_index == pc_index) {
                return i;
            }
        }
        return nullptr;
    }

    void increase_pending_request(int completion_index) {
        if (pending_request.count(completion_index) == 0) {
            pending_request[completion_index] = 1;
        } else {
            pending_request[completion_index] += 1;
        }
    }

    bool decrease_pending_request(int completion_index) { // true if all request release
        pending_request[completion_index] -= 1;
        if (pending_request[completion_index] == 0) {
            return true;
        }
        return false;
    }

    bool pending_request_can_decrease(int completion_index) {
        return pending_request[completion_index] > 0;
    }

    void get_inst_dependency();

    int m_block_id;
    int m_warp_id;
    std::vector<mem_inst *> m_mem_inst;
    int warp_point;
    std::vector<int> completions;
    bool active;
    bool syncing;
    int max_dep;
    static_warp *m_static_warp;
    std::vector<inst> m_insts; // all sass

    std::map<int, int> pending_request; // key:completion_index value:request_num
    int pending_inst;

    bool m_at_barrier;
    block*m_block;
    unsigned thread_mask;
    int m_pending_mem_request_num;
};

class static_warp {
public:
    explicit static_warp(int warp_id) {
        m_warp_id = warp_id;
        is_active = false;
        inst_pointer = 0;
    }

    void get_inst_dependency();

    int m_warp_id;
    std::vector<inst> m_insts;
    bool is_active;
    int inst_pointer;


};

class kernel_info {
public:
    unsigned m_kernel_id;
    unsigned m_grid_size;
    unsigned m_block_size;
    unsigned m_num_registers;
    unsigned m_shared_mem_bytes;
};

class block {
public:
    block(int kernel_id, int block_id, kernel_info kernel_info_t, std::map<int, static_warp *> &static_warp_list) {
        m_kernel_id = kernel_id;
        m_block_id = block_id;
        m_active = true;
        m_actual_end = 0;

        m_warp_at_barrier.reset();
        for (int i = 0; i < static_warp_list.size(); i++) {
            warp_vec[i]=(new warp(m_block_id, i, static_warp_list[i], this));
        }
    }

    block(int kernel_id, int block_id){
        m_kernel_id = kernel_id;
        m_block_id = block_id;
        m_active = true;
        m_actual_end = 0;
        m_warp_at_barrier.reset();
    }

    bool is_active(int);

    void read_mem(const string &trace_path, int l1_cache_line_size, int block_id);

    void load_mem_request(const string &trace_path, int l1_cache_line_size, int block_id);

    warp *get_warp(int warp_id) {
        if(warp_vec.find(warp_id)!=warp_vec.end()){
            return warp_vec[warp_id];
        }
        return nullptr;
    }


    int m_kernel_id;
    int m_block_id;

    std::map<int, std::vector<mem_inst *>> mem_inst_map;
    std::map<int, warp *> warp_vec;
    bool m_active;
    int m_actual_end;
    std::bitset<WARP_PER_CTA_MAX> m_warp_at_barrier;
};


class kernel {
public:
    kernel(int kernel_id) {
        m_kernel_id = kernel_id;
        block_pointer = 0;
        request_nums = 0;
    }

    void init_blocks(unsigned mode);

    int m_kernel_id;
    std::map<int, static_warp *> static_warp_list;
    kernel_info m_kernel_info;
    std::vector<trace_inst> trace_insts;
    std::map<int,block *> m_blocks;
    int block_pointer;
    int request_nums;
};



//class barrier {
//public:
//// individual warp hits barrier
//    void warp_reaches_barrier(int warp_id, int cycles, warp& warp, block*block, inst&inst_t) {
//        warp.thread_mask += std::stoll(inst_t.m_active_mask, nullptr, 16);
//        if(warp.thread_mask == 0xffffffff){ // TODO:掩码判断参与barrier线程
//            block->m_warp_at_barrier.set(warp_id);
//            warp.thread_mask = 0;
//        }
//
//        if (block->m_warp_at_barrier.count() == block->warp_vec.size()) {
//            // all warps have reached barrier, so release waiting warps...
//            for(auto & warp_t : block->warp_vec){
//                warp_t.second->completions.push_back(cycles);
//                block->m_warp_at_barrier.reset(warp_t.second->m_warp_id);
//
//            }
//
//        }
//    }
//};

//class control_inst{
//public:
//    void process(gpu_config& gpuconfig, int cycles, warp& warp, block*block, const string& m_opcode, int m_warp_id, sm_unit* unit, inst& inst_t);
//
//    static int process_common_inst(gpu_config& gpuconfig, const string& m_opcode){
//        return std::get<0>(gpuconfig.gpu_isa_latency[m_opcode]);
//    }
//
//    void process_bsync(int cycles, warp& warp, block*block, int m_warp_id, inst& inst_t){
//
//        m_barrier.warp_reaches_barrier(m_warp_id, cycles, warp, block, inst_t);
//    }
//
//    barrier m_barrier;
//    const std::vector<string> m_opcode_list{"BMOV", "BPT", "BRA", "BREAK", "BRX", "BRXU", "BSSY", "BSYNC", "CALL", "EXIT",
//                                            "JMP", "JMX", "JMXU", "KILL", "NANOSLEEP", "RET", "RPCMOV", "RTT", "WARPSYNC", "YIELD" };
//
//};

//class FloInst{
//    int process(gpu_config& gpu_config_t, string opcode){
//        return std::get<0>(gpu_config_t.gpu_isa_latency[opcode]);
//    }
////    const std::vector<string> m_opcode_list{"FADD", "FADD32I", "FCHK", "FFMA32I", "FFMA", "FMNMX", "FMUL",
////    "FMUL32I", "FSEL", "FSET", "FSETP", "FSWZADD", "MUFU", "HADD2", "HADD2_32I", "HFMA2", "HFMA2_32I", "HMMA",
////    "HMUL2", "HMUL2_32I", "HSET2", "HSETP2", "DADD", "DFMA", "DMUL", "DSETP"};
//};
//
//class IntInst{
//    int process(gpu_config& gpu_config_t, string opcode){
//        return std::get<0>(gpu_config_t.gpu_isa_latency[opcode]);
//    }
//
////    const std::vector<string> m_opcode_list{"BMMA", "BMSK", "BREV", "FLO", "IABS", "IADD", "IADD3", "IADD32I",
////        "IDP", "IDP4A", "IMAD", "IMMA", "IMNMX", "IMUL", "IMUL32I", "ISCADD", "ISCADD32I", "ISETP", "LEA", "LOP",
////        "LOP3", "LOP32I", "POPC", "SHF", "SHL", "SHR", "VABSDIFF", "VABSDIFF4"};
//};

class ConvInst{
    int process(gpu_config& gpu_config_t, string opcode){
        return std::get<0>(gpu_config_t.gpu_isa_latency[opcode]);
    }
};

class MovInst{
    int process(gpu_config& gpu_config_t, string opcode){
        return std::get<0>(gpu_config_t.gpu_isa_latency[opcode]);
    }
};

class PredInst{
    int process(gpu_config& gpu_config_t, string opcode){
        return std::get<0>(gpu_config_t.gpu_isa_latency[opcode]);
    }
};

unsigned l1_bank_hash(mem_fetch* mf);





//
//class UniformDatapath{
//    int process(gpu_config& gpu_config_t, string opcode){
//        return std::get<0>(gpu_config_t.gpu_isa_latency[opcode]);
//    }
//};
//
//class TexInst{
//    int process(gpu_config& gpu_config_t, string opcode){
//        return std::get<0>(gpu_config_t.gpu_isa_latency[opcode]);
//    }
//};
//
//class SurInst{
//    int process(gpu_config& gpu_config_t, string opcode){
//        return std::get<0>(gpu_config_t.gpu_isa_latency[opcode]);
//    }
//};
//
//class MiscInst{
//    int process(gpu_config& gpu_config_t, string opcode){
//        return std::get<0>(gpu_config_t.gpu_isa_latency[opcode]);
//    }
//};



#endif //FLEX_GPUSIM_KERNEL_H
