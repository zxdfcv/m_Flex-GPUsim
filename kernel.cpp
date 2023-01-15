//
// Created by 徐向荣 on 2022/6/19.
//

#include "kernel.h"
#include <algorithm>

void kernel::init_blocks(unsigned mode) {
    if (mode == 0) {
        for (auto &item: trace_insts) {
            inst inst_t = inst(item);
            int warp_id = inst_t.m_warp_id;
            if (static_warp_list.find(warp_id) != static_warp_list.end()) {
                static_warp_list[warp_id]->m_insts.push_back(inst_t);
            } else {
                static_warp_list[warp_id] = new static_warp(warp_id);
                static_warp_list[warp_id]->m_insts.push_back(inst_t);
            }
        }
        for (auto &static_warp_t: static_warp_list) {
            static_warp_t.second->get_inst_dependency();
        }
        for (int i = 0; i < m_kernel_info.m_grid_size; i++) {
            m_blocks[i] = (new block(m_kernel_id, i, m_kernel_info, static_warp_list));
        }
    } else if (mode == 1) {
        for (int i = 0; i < m_kernel_info.m_grid_size; i++) {
            m_blocks[i] = new block(m_kernel_id, i); //init all block
        }
        for (auto &item: trace_insts) {
            inst inst_t = inst(item);
            int block_id = inst_t.m_block_id;
            int warp_id = inst_t.m_warp_id;
            if (m_blocks[block_id]->warp_vec.find(warp_id) != m_blocks[block_id]->warp_vec.end()) {
                m_blocks[block_id]->warp_vec[warp_id]->m_insts.push_back(inst_t);
            } else {
                m_blocks[block_id]->warp_vec[warp_id] = new warp(block_id, warp_id, m_blocks[block_id]);
                m_blocks[block_id]->warp_vec[warp_id]->m_insts.push_back(inst_t);
            }
        }
        for (int block_id = 0; block_id < m_kernel_info.m_grid_size; block_id++) {
            for (auto &warp: m_blocks[block_id]->warp_vec) {
                warp.second->get_inst_dependency();
            }
        }
    }
}

void static_warp::get_inst_dependency() {
    std::map<string, int> dependency_map;
    for (int index = 0; index < m_insts.size(); index++) {
        for (auto &dest_reg: m_insts[index].m_dest_regs) {
            if (dependency_map.find(dest_reg) != dependency_map.end()) {
                m_insts[index].m_dependency.push_back(dependency_map[dest_reg]);
            }
        }
        for (auto &src_reg: m_insts[index].m_src_regs) {
            if (dependency_map.find(src_reg) != dependency_map.end()) {
                m_insts[index].m_dependency.push_back(dependency_map[src_reg]);
            }
        }
        if (m_insts[index].m_dest_num != 0) {
            for (auto &dest_reg: m_insts[index].m_dest_regs) {
                dependency_map[dest_reg] = index;
            }
        }
        if(m_insts[index].m_opcode=="EXIT"||m_insts[index].m_opcode.find("BAR") != string::npos){
            for (int i = 0; i < index; i++) {
                m_insts[index].m_dependency.push_back(i);
            }
        }

        // todo EXIT,BAR依赖所有指令
    }
}

void warp::get_inst_dependency() {
    std::map<string, int> dependency_map;
    for (int index = 0; index < m_insts.size(); index++) {
        for (auto &dest_reg: m_insts[index].m_dest_regs) {
            if (dependency_map.find(dest_reg) != dependency_map.end()) {
                m_insts[index].m_dependency.push_back(dependency_map[dest_reg]);
            }
        }
        for (auto &src_reg: m_insts[index].m_src_regs) {
            if (dependency_map.find(src_reg) != dependency_map.end()) {
                m_insts[index].m_dependency.push_back(dependency_map[src_reg]);
            }
        }
        if (m_insts[index].m_dest_num != 0) {
            for (auto &dest_reg: m_insts[index].m_dest_regs) {
                dependency_map[dest_reg] = index;
            }
        }
        if(m_insts[index].m_opcode=="EXIT"||m_insts[index].m_opcode.find("BAR") != string::npos){
            for (int i = 0; i < index; i++) {
                m_insts[index].m_dependency.push_back(i);
            }
        }
    }
}


void block::read_mem(const string &trace_path, int l1_cache_line_size, int block_id) {
    std::ifstream mem_trace(
            trace_path + "/kernel-" + std::to_string(m_kernel_id) + "-block-" + std::to_string(block_id) + ".mem",
            std::ios::in);
    string line;
    unsigned sector_num = 0;
    std::map<int, std::map<long long, int> > block_pc_num;  // warp_id:{pc:pc_num}
    while (std::getline(mem_trace, line)) {
        if (line != "\n") {
            if (line != "====") {
                std::istringstream is(line);
                std::string str;
                std::queue<std::string> tmp_str;
                while (is >> str) {
                    tmp_str.push(str);
                }
                int warp_id = std::stoi(tmp_str.front());
                tmp_str.pop();

                char *stop;
                long long pc = std::strtoll(tmp_str.front().c_str(), &stop, 16);
                tmp_str.pop();

                string opcode = tmp_str.front();
                tmp_str.pop();

                int pc_index = 0;
                if (block_pc_num.find(warp_id) != block_pc_num.end()) {
                    if (block_pc_num[warp_id].find(pc) != block_pc_num[warp_id].end()) {
                        pc_index = block_pc_num[warp_id][pc] + 1;
                        block_pc_num[warp_id][pc] = pc_index;
                    } else {
                        block_pc_num[warp_id][pc] = 0;
                    }
                } else {
                    block_pc_num[warp_id][pc] = 0;
                }

                std::vector<std::pair<long long, int> > coalesced_address;
                while (!tmp_str.empty()) {//coalescing the addresses of the warp
                    string warp_address = tmp_str.front();
                    tmp_str.pop();

                    long long i_warp_address = std::strtoll(warp_address.c_str(), &stop, 16);

                    auto cache_line = (long long) (i_warp_address >> (int) log2(l1_cache_line_size));
                    long long offset = i_warp_address - (cache_line << (int) log2(l1_cache_line_size));
                    long long cache_line_addr = cache_line << (int) log2(l1_cache_line_size);
                    int sector_mask = (int) (offset / SECTOR_SIZE);
                    if (std::find(coalesced_address.begin(), coalesced_address.end(),
                                  std::pair<long long, int>(cache_line_addr, sector_mask)) == coalesced_address.end()) {
                        coalesced_address.emplace_back(std::pair<long long, int>(cache_line_addr, sector_mask));
                    }

                }
                sector_num += coalesced_address.size();
                mem_inst_map[warp_id].push_back(new mem_inst(opcode, coalesced_address, pc, pc_index));
            }
        }
    }
}

void block::load_mem_request(const string &trace_path, int l1_cache_line_size, int block_id) {
    read_mem(trace_path, l1_cache_line_size, block_id);
    for (auto &warp: warp_vec) {
        warp.second->m_mem_inst = mem_inst_map[warp.second->m_warp_id];
    }
}

bool block::is_active(int cycles) {
    int actual_end = 0;
    int max_warp_completions = 0;

    for (auto &warp: warp_vec) {
        if (!warp.second->completions.empty()) {
            max_warp_completions = *max_element(warp.second->completions.begin(), warp.second->completions.end());
            actual_end = std::max(actual_end, max_warp_completions);
        }
    }
    for (auto &warp: warp_vec) {
        if (warp.second->active || warp.second->m_pending_mem_request_num > 0 ||warp.second->pending_inst>0) {
            return true;
        }
    }
//    if (cycles <= actual_end) {
//        return true;
//    }
    m_active = false;
    m_actual_end = actual_end;
    return false;
}

unsigned l1_bank_hash(mem_fetch* mf) {
    return mf->m_sector_mask;
}


//void control_inst::process(gpu_config &gpuconfig, int cycles, warp &warp, block *block, const string &m_opcode,
//                           int m_warp_id, sm_unit *unit, inst &inst_t) {
//    if(std::find(m_opcode_list.begin(), m_opcode_list.end(), m_opcode) != m_opcode_list.end() && m_opcode != "BSYNC"){
//        int latency = process_common_inst(gpuconfig, m_opcode);
//        warp.completions.push_back(cycles + latency);
//        unit->ready_time += latency;
//    }
//    else{
//        if(m_opcode == "BSYNC"){
//            process_bsync(cycles, warp, block, m_warp_id, inst_t);
//        }
//        else{
//            printf("%s does not exist\n", m_opcode.c_str());
//        }
//    }
//}



