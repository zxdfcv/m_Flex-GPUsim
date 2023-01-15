#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <utility>

#include "gpu.h"
#include "debug.h"

bool is_ld_st_inst(const string &opcode);

bool is_ctrl_inst(const string &opcode);


void gpu::gpu_cycle() {
    bool sm_active = true;
    while (sm_active) {
        int total_l1_to_l2_num = 0;
        int total_l1_to_l2_mshr = 0;
        sm_active = false;
        gpu_sim_cycles += 1;
        printf("gpu_sim_cycle:%d\n", gpu_sim_cycles);
        if(g_mod) {
            for (int i = 0; i < GPM_NUM; i++) {
                g_memory_partition[i]->memory_partition_cycle(gpu_sim_cycles);
            }
        }
        else {
            m_memory_partition->memory_partition_cycle(gpu_sim_cycles);
        }
        m_icnt->Advance(); //icnt cycle
        if (g_mod) {
            for (int i = 0; i < GPM_NUM; i++) {
                for (auto &sm: g_sm[i]) {
                    sm->sm_cycle();
                    total_l1_to_l2_num += sm->m_l1_data_cache->m_l1_to_l2_num;
                    total_l1_to_l2_mshr += sm->m_l1_data_cache->m_l1_to_l2_mshr;
                    if (sm->sm_active()) {
                        sm_active = true;// there is active sm
                    }
                }
            }
        }
        else {
            for (auto &sm: m_sm) {
                sm->sm_cycle();
                total_l1_to_l2_num += sm->m_l1_data_cache->m_l1_to_l2_num;
                total_l1_to_l2_mshr += sm->m_l1_data_cache->m_l1_to_l2_mshr;
                if (sm->sm_active()) {
                    sm_active = true;// there is active sm
                }
            }
        }
//        std::cout<<"l1_to_l2_num :"<<total_l1_to_l2_num<<std::endl;
//        std::cout<<"l1_to_l2_mshr :"<<total_l1_to_l2_mshr<<std::endl;
    }

    int stg_cycles = 0;
    int l2_rd_sector = 0, l2_wr_sector = 0, l2_rd_hit = 0, l2_wr_hit = 0;
    if(g_mod) {
        for (int i = 0; i < GPM_NUM; i++) {
            for (auto &l2_cache: g_memory_partition[i]->m_l2_caches) {
                if (!l2_cache->l2_to_dram.empty()) {
                    for (auto &remaining: l2_cache->l2_to_dram) {
                        if (stg_cycles < remaining.second) {
                            stg_cycles = remaining.second;
                        }
                    }
                }

                l2_rd_sector += l2_cache->m_read_cache_access_num;
                l2_wr_sector += l2_cache->m_write_cache_access_num;
                l2_rd_hit += l2_cache->m_load_sector_hit;
                l2_wr_hit += l2_cache->m_store_sector_hit;
            }
        }
    }
    else {
        for (auto &l2_cache: m_memory_partition->m_l2_caches) {
            if (!l2_cache->l2_to_dram.empty()) {
                for (auto &remaining: l2_cache->l2_to_dram) {
                    if (stg_cycles < remaining.second) {
                        stg_cycles = remaining.second;
                    }
                }
            }

            l2_rd_sector += l2_cache->m_read_cache_access_num;
            l2_wr_sector += l2_cache->m_write_cache_access_num;
            l2_rd_hit += l2_cache->m_load_sector_hit;
            l2_wr_hit += l2_cache->m_store_sector_hit;
        }
    }
    gpu_sim_cycles += stg_cycles;
    printf("gpu_sim_cycle:%d\n", gpu_sim_cycles);

    int total_sm_cycles = 0, l1_rd_sector = 0, l1_rd_hit = 0, l1_wr_sector = 0, l1_wr_hit = 0;

    if (g_mod) {
        for (int i = 0; i < GPM_NUM; i++) {
            for (auto &sm: g_sm[i]) {
                if (sm->cycles > 0) {
                    total_sm_cycles += sm->cycles + stg_cycles;
                    total_sm_cycles += stoi(m_gpu_configs.m_gpu_config["kernel_launch_cycles"]);
                }
                l1_rd_sector += sm->m_l1_data_cache->m_read_cache_access_num;
                l1_rd_hit += sm->m_l1_data_cache->m_load_sector_hit;
                l1_wr_sector += sm->m_l1_data_cache->m_write_cache_access_num;
                l1_wr_hit += sm->m_l1_data_cache->m_store_sector_hit;
            }
        }
    }
    else {
        for (auto &sm: m_sm) {
            if (sm->cycles > 0) {
                total_sm_cycles += sm->cycles + stg_cycles;
                total_sm_cycles += stoi(m_gpu_configs.m_gpu_config["kernel_launch_cycles"]);
            }
            l1_rd_sector += sm->m_l1_data_cache->m_read_cache_access_num;
            l1_rd_hit += sm->m_l1_data_cache->m_load_sector_hit;
            l1_wr_sector += sm->m_l1_data_cache->m_write_cache_access_num;
            l1_wr_hit += sm->m_l1_data_cache->m_store_sector_hit;
        }
    }
    total_sm_cycles +=
            stoi(m_gpu_configs.m_gpu_config["block_launch_cycles"]) * m_active_kernel->m_kernel_info.m_grid_size;
//    float l1_hit_rate = (float)(l1_rd_hit + l1_wr_hit) / (float)(l1_rd_sector + l1_wr_sector);

    printf("total_sm_cycles:%d\n", total_sm_cycles);
    printf("access_time %ld us\n", access_time);
    printf("####################################\n");
    printf("l1_rd_sector:%d\n", l1_rd_sector);
    printf("l1_rd_hit:%d\n", l1_rd_hit);
//    printf("L1 rd rate:%f\n", (float)l1_rd_hit / (float)l1_rd_sector);
    printf("l1_wr_sector:%d\n", l1_wr_sector);
    printf("l1_wr_hit:%d\n", l1_wr_hit);
//    printf("L1 wr rate:%f\n", (float)l1_wr_hit / (float)l1_wr_sector);
//    printf("l1_hit_rate:%f\n", l1_hit_rate);


    printf("l2_rd_sector:%d\n", l2_rd_sector);
    printf("l2_rd_hit:%d\n", l2_rd_hit);
//    printf("L2 rd rate:%f\n", (float)l2_rd_hit / (float)l2_rd_sector);
    printf("l2_wr_sector:%d\n", l2_wr_sector);
    printf("l2_wr_hit:%d\n", l2_wr_hit);
//    printf("l2_hit_rate:%f\n", (float)(l2_rd_hit + l2_wr_hit) / (float)(l2_wr_sector + l2_rd_sector));
    printf("####################################\n");
    for (int i = 0; i < std::stoi(m_gpu_configs.m_gpu_config["mem_num"]); i++) {
        printf("l2_use:%d : %d\n", i, l2_data_cache::g_l2_list[i]);
    }

}


void streaming_multiprocessor::sm_cycle() {
    if (sm_active()) {
        cycles += 1;
    } else {
        return;
    }
    m_queue_sm_to_l1_busy.reset();

    auto *mf = (mem_fetch *) m_gpu->m_icnt->Pop(m_sm_id);
    // TODO think about L1 cache fill bandwidth
    // TODO write back mf

    write_back();
    write_back_request_l2(mf);
    auto t1 = Clock::now();
    m_l1_data_cache->l1_cache_cycle(cycles);
    auto t2 = Clock::now();
    std::chrono::nanoseconds t21 = t2 - t1;
    access_time += std::chrono::duration_cast<std::chrono::microseconds>(t21).count();


    del_inactive_block();
    for(auto &unit:m_units){
        if(unit.first=="LDS_units"){
            for(auto &j : unit.second){
                j->unit_cycle(cycles);
            }
        }
    }
    while (block_vec.size() < m_max_blocks && m_active_kernel->block_pointer < m_active_kernel->m_blocks.size()) {
        block_vec.push_back(m_active_kernel->m_blocks[m_active_kernel->block_pointer]);
        m_active_kernel->block_pointer++;
    }
    std::vector<warp *> current_active_warps;
    for (auto &block: block_vec) {
        if (!(block->m_active)) {
            printf("error 1");
            exit(1);
            continue;
        }
        if (block->is_active(cycles)) {
            for (auto &warp: block->warp_vec) {
                if (warp.second->active) {
                    current_active_warps.push_back(warp.second);
                }
            }
        }
    }

    m_execute_inst += schedule_warp(current_active_warps);
//    if(m_sm_id == 0){
//        printf("inst %d\n", m_execute_inst);
//    }
}


int streaming_multiprocessor::issue_inst(warp &warp) {
    inst inst;
    int inst_issued = 0;
    for (int i = 0; i < std::stoi(m_gpu_config.m_gpu_config["num_inst_dispatch_units_per_warp"]); ++i) {
        if(stoi(m_gpu_config.m_gpu_config["block_sim_mode"])){ // fetch inst
            inst = warp.m_insts[warp.warp_point];
        }else{
            inst = warp.m_static_warp->m_insts[warp.warp_point];
        }
        if (m_gpu_config.gpu_isa_latency.count(inst.m_opcode) == 0) {
            printf("ISA ERROR: ");
            printf("%s\n", inst.m_opcode.c_str());
            exit(1);
        }
        auto& unit_t = std::get<1>(m_gpu_config.gpu_isa_latency[inst.m_opcode]);
        if(unit_t.empty() || unit_t == "-"){
            printf("ERROR: inst %s does not exist!\n", inst.m_opcode.c_str());
            exit(1);
        }
        for (auto &unit: m_units[std::get<1>(m_gpu_config.gpu_isa_latency[inst.m_opcode])]) {
            if (unit->ready_time <= cycles) {
                if (is_ld_st_inst(inst.m_opcode)) {
                    LdStInst lsi;
                    lsi.process(m_gpu_config, unit, warp, cycles, inst.m_pc, inst.m_pc_index, inst.m_active_thread_num,
                                inst.m_opcode, m_sm_id, m_queue_sm_to_l1, m_l1_cache_config, m_active_kernel);
                }
//                    else if(is_ctrl_inst(inst.m_opcode)){// controrl instructions
//                        control_inst ctrl_inst;
//                        ctrl_inst.process(m_gpu_config, cycles, warp, warp.m_block, inst.m_opcode, inst.m_warp_id, unit, inst);
//                    }
                else {
                    int latency = std::get<0>(m_gpu_config.gpu_isa_latency[inst.m_opcode]);
                    unit->ready_time += latency;
                    warp.completions.push_back(cycles + latency);
                }
                warp.warp_point += 1;
                inst_issued++;
                if(warp.m_pending_mem_request_num < 0){
                    printf("%s(%d)-<%s>: m_pending_mem_request_num < 0!", __FILE__, __LINE__, __FUNCTION__);
                }
                break;
            }
        }
    }
    return inst_issued;
}

void streaming_multiprocessor::write_back() {
    bool flag = true;
    while (flag) {
        if (!m_l1_data_cache->l1_to_sm.empty()) {
            mem_fetch *mf_next = m_l1_data_cache->l1_to_sm.front();
          //  std::cout<<mf_next->m_src_block_id<<" "<<mf_next->m_src_warp_id<<" "<<mf_next->m_address<<" "<<mf_next->m_sector_mask<<std::endl;
            if (mf_next->m_l1_ready_time <= cycles) {
                int block_id = mf_next->m_src_block_id;
                int warp_id = mf_next->m_src_warp_id;
                int completion_index = mf_next->m_completion_index;
                block *m_block = get_block(block_id);
                warp *m_warp = m_block->get_warp(warp_id);
                if (m_warp->pending_request_can_decrease(completion_index)) {
                    bool request_completion = m_warp->decrease_pending_request(completion_index);
                    if (request_completion) {
                        m_warp->completions[completion_index] = cycles;
                    }
                    m_warp->m_pending_mem_request_num--;
                }
                delete mf_next;
                m_l1_data_cache->l1_to_sm.pop();
            } else {
                flag = false;
            }
        } else {
            flag = false;
        }
    }
}

void streaming_multiprocessor::write_back_request_l2(mem_fetch *mf) {
    if (!mf) {
        return;
    }
   // std::cout<<mf->m_src_block_id<<" "<<mf->m_src_warp_id<<" "<<mf->m_address<<" "<<mf->m_sector_mask<<std::endl;
//    printf("write_back_l2\n");
    new_addr_type mshr_address = mf->m_sector_address;
//    int block_id = mf->m_src_block_id;
//    int warp_id = mf->m_src_warp_id;
//    int completion_index = mf->m_completion_index;
//    block *m_block = get_block(block_id);
//    warp *m_warp = m_block->get_warp(warp_id);
    if (!mf->is_write()) {
        while (m_l1_data_cache->m_mshrs->probe(mshr_address)) {
            mem_fetch *mf_next = m_l1_data_cache->m_mshrs->next_access(mshr_address);
//            printf("complete_mshr_l2\n");
            int block_id_t = mf_next->m_src_block_id;
            int warp_id_t = mf_next->m_src_warp_id;
            int completion_index_t = mf_next->m_completion_index;
            block *m_block_t = get_block(block_id_t);
            warp *m_warp_t = m_block_t->get_warp(warp_id_t);
            if (m_warp_t->pending_request_can_decrease(completion_index_t)) {
//                printf("complete_decrease\n");
                bool request_completion = m_warp_t->decrease_pending_request(completion_index_t);
                if (request_completion) {
                    m_warp_t->completions[completion_index_t] = cycles;
                }
                m_warp_t->m_pending_mem_request_num--;
//                std::ofstream outfile;
//                outfile.open("../written_back.txt", std::ios::app);
//                if(!mf->is_write()){
//                    outfile << "warp_id "<<warp_id_t <<"sector address:" << mf->m_sector_address << std::endl;
//                }
//                outfile.close();
            } else {
                printf("error!\n");
            }
            m_l1_data_cache->cache_fill(mf, cycles);
        }
    } else {
        int block_id = mf->m_src_block_id;
        int warp_id = mf->m_src_warp_id;
        int completion_index = mf->m_completion_index;
        block *m_block = get_block(block_id);
        warp *m_warp = m_block->get_warp(warp_id);
        if (m_warp->pending_request_can_decrease(completion_index)) {
            bool request_completion = m_warp->decrease_pending_request(completion_index);
            if (request_completion) {
                m_warp->completions[completion_index] = cycles;
            }
            m_warp->m_pending_mem_request_num--;
        } else {
            printf("error");
            exit(3);
        }
    }

}

int streaming_multiprocessor::schedule_warp(std::vector<warp *> &warp_vec) {
    int warp_executed = 0, inst_executed = 0;
    for (auto &warp: warp_vec) {
        if (warp_executed >= std::stoi(m_gpu_config.m_gpu_config["num_warp_schedulers_per_SM"])) {
            break;
        }

        bool flag = true;
        if (stoi(m_gpu_config.m_gpu_config["block_sim_mode"])) {
            if (warp->warp_point >= warp->m_insts.size()) {
                flag = false;
            }
        } else {
            if (warp->warp_point >= warp->m_static_warp->m_insts.size()) {
                flag = false;
            }
        }

        if (flag && check_unit(*warp) && check_dependency(*warp) && check_warp_active(*warp)) {
            int current_inst_executed = issue_inst(*warp);
            inst_executed += current_inst_executed;
            warp_executed += 1;
        }

        if (stoi(m_gpu_config.m_gpu_config["block_sim_mode"])) {
            if (warp->warp_point >= warp->m_insts.size() && warp->m_pending_mem_request_num == 0) {
                warp->active = false;
            }
        } else {
            if (warp->warp_point >= warp->m_static_warp->m_insts.size() && warp->m_pending_mem_request_num == 0) {
                warp->active = false;
            }
        }
    }
    return inst_executed;
}


bool streaming_multiprocessor::check_unit(warp &warp) {
    inst inst;
    if (stoi(m_gpu_config.m_gpu_config["block_sim_mode"])) {
        inst = warp.m_insts[warp.warp_point];
    } else {
        inst = warp.m_static_warp->m_insts[warp.warp_point];
    }
    if (inst.m_opcode.find("LDG") != string::npos || inst.m_opcode.find("STG") != string::npos) {
        return check_unit(4, "LDS_units");
    } else if (inst.m_opcode.find("BAR") != string::npos) {
        return check_unit(4, "INT_units");
    } else if (inst.m_opcode.find("LDS") != string::npos || inst.m_opcode.find("STS") != string::npos ||
               inst.m_opcode.find("LDL") != string::npos || inst.m_opcode.find("STL") != string::npos ||
               inst.m_opcode.find("LDC") != string::npos || inst.m_opcode.find("STC") != string::npos ||
               inst.m_opcode.find("ATOM") != string::npos || inst.m_opcode.find("RED") != string::npos ||
               inst.m_opcode.find("MEMBAR") != string::npos) {
        if (inst.m_opcode.find("RED") != string::npos) {
            printf("RED INSTRUCTION!\n");
        }
        return true;
    } else {
        int latency = std::get<0>(m_gpu_config.gpu_isa_latency[inst.m_opcode]);
        if (latency == 0) {// stoi
            printf("ISA ERROR ");
            printf("%s inst latency is not sure\n", inst.m_opcode.c_str());
            exit(1);
        }
        return check_unit(latency, std::get<1>(m_gpu_config.gpu_isa_latency[inst.m_opcode]));
    }

}


bool streaming_multiprocessor::check_unit(int delay, const string &unit_name) {
    for (auto &unit: m_units[unit_name])
        if (unit->ready_time <= cycles) {
            return true;
        }
    return false;
}


bool streaming_multiprocessor::check_dependency(warp &warp) {
    inst inst;

    if (stoi(m_gpu_config.m_gpu_config["block_sim_mode"])) {
        inst = warp.m_insts[warp.warp_point];
    } else {
        inst = warp.m_static_warp->m_insts[warp.warp_point];
    }
    int max_dep = 0;
    for (auto &dependency: inst.m_dependency) {
        if (dependency >= warp.completions.size()) {
            printf("Error: with instruction %lld dependency %d", inst.m_pc, dependency);
        }
        max_dep = std::max(max_dep, warp.completions[dependency]);
    }

    if (cycles <= max_dep) {
        return false;
    } else {
        return true;
    }
}

bool streaming_multiprocessor::check_warp_active(warp &warp) {
    if (stoi(m_gpu_config.m_gpu_config["block_sim_mode"])) {
        if (warp.warp_point >= warp.m_insts.size()) {
            warp.active = false;
            return false;
        }
        return true;
    }
    if (warp.warp_point >= warp.m_static_warp->m_insts.size()) {
        warp.active = false;
        return false;
    }
    return true;
}

bool streaming_multiprocessor::check_at_barrier(warp &warp) {
    return !warp.m_block->m_warp_at_barrier[warp.m_warp_id];
}

void streaming_multiprocessor::del_inactive_block() {
    auto it = block_vec.begin();
    for (; it != block_vec.end();) {
        if (!((*it)->is_active(cycles))) {
            auto tmp = it;
            it = block_vec.erase(tmp);
        } else {
            it++;
        }
    }
}

void streaming_multiprocessor::load_units() {
    for (auto &unit: m_gpu_config.m_sm_pipeline_units) {
        for (int i = 0; i < unit.second; ++i) {
            sm_unit* unit_t = new sm_unit(unit.first);
            unit_t->set_sm(this);
            m_units[unit.first].push_back(unit_t);
        }
    }
}

gpu::gpu(std::string benchmark) : gpu_sim_cycles(0) {
    active_benchmark = std::move(benchmark);
    init_config();
    build_gpu();
}

void gpu::init_config() {
    m_gpu_configs = gpu_config();
    m_gpu_configs.read_m_config();
}

void gpu::build_gpu() { //这里新建sm，需要递归向下建立各个模块
    int sm_num = std::stoi(m_gpu_configs.m_gpu_config["sm_num"]);
    cache_config m_l1_cache_config = cache_config(true, std::stoi(m_gpu_configs.l1_cache_config["l1_cache_n_banks"]),
                                                  std::stoi(m_gpu_configs.l1_cache_config["l1_cache_n_sets"]),
                                                  std::stoi(m_gpu_configs.l1_cache_config["l1_cache_line_size"]),
                                                  std::stoi(m_gpu_configs.l1_cache_config["l1_cache_m_assoc"]),
                                                  m_gpu_configs.l1_cache_config["l1_cache_m_replacement_policy"],
                                                  m_gpu_configs.l1_cache_config["l1_cache_m_write_policy"],
                                                  m_gpu_configs.l1_cache_config["l1_cache_m_alloc_policy"],
                                                  m_gpu_configs.l1_cache_config["l1_cache_m_write_alloc_policy"],
                                                  m_gpu_configs.l1_cache_config["l1_cache_m_set_index_function"],
                                                  m_gpu_configs.l1_cache_config["l1_cache_mshr_type"],
                                                  std::stoi(m_gpu_configs.l1_cache_config["l1_m_mshr_entries"]),
                                                  std::stoi(m_gpu_configs.l1_cache_config["l1_m_mshr_max_merge"]),
                                                  true);

    cache_config m_l2_cache_config = cache_config(true,
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_cache_sub_partitions"]),
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_cache_n_sets"]),
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_cache_line_size"]),
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_cache_m_assoc"]),
                                                  m_gpu_configs.l2_cache_config["l2_cache_m_replacement_policy"],
                                                  m_gpu_configs.l2_cache_config["l2_cache_m_write_policy"],
                                                  m_gpu_configs.l2_cache_config["l2_cache_m_alloc_policy"],
                                                  m_gpu_configs.l2_cache_config["l2_cache_m_write_alloc_policy"],
                                                  m_gpu_configs.l2_cache_config["l2_cache_m_set_index_function"],
                                                  m_gpu_configs.l2_cache_config["l2_cache_mshr_type"],
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_m_mshr_entries"]),
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_m_mshr_max_merge"]),
                                                  true);
    if (g_mod) {
        int part_num = GPM_NUM;
        int sm_per_part_num = sm_num / part_num;
        g_memory_partition.resize(GPM_NUM);
//        l2_data_cache::g_l2_list = new std::vector<int>;
        for (int i = 0; i < GPM_NUM; i++) {
            g_memory_partition[i] = new memory_partition(m_l2_cache_config, m_gpu_configs);
            g_memory_partition[i]->init(this, i);
        }
        creat_icnt();
        g_sm.resize(GPM_NUM);
        for (int sm_id = 0; sm_id < sm_num; sm_id++) {
            auto *sm = new streaming_multiprocessor(sm_id, m_gpu_configs, m_l1_cache_config);
            sm->init(this);
            //m_sm.push_back(sm);
            g_sm[sm_id / sm_per_part_num].push_back(sm);
        }
    }
    else {
        m_memory_partition = new memory_partition(m_l2_cache_config, m_gpu_configs);
        m_memory_partition->init(this, 0);
        creat_icnt();
        for (int sm_id = 0; sm_id < sm_num; sm_id++) {
            auto *sm = new streaming_multiprocessor(sm_id, m_gpu_configs, m_l1_cache_config);
            sm->init(this);
            m_sm.push_back(sm);
        }
    }
}

void gpu::read_trace(int kernel_id, kernel_info &kernelInfo,
                     std::vector<trace_inst> &trace_insts) {
    traceReader = new trace_reader(trace_reader(kernel_id));
    kernelInfo = kernel_info();
    if (stoi(m_gpu_configs.m_gpu_config["block_sim_mode"])) {
        traceReader->read_all_block_sass("../benchmarks/" + active_benchmark + "/traces/", kernelInfo, trace_insts);
    } else {
        traceReader->read_sass("../benchmarks/" + active_benchmark + "/traces/", kernelInfo, trace_insts);
    }
}


int gpu::max_block_per_sm(kernel_info kernel_info) {
    return std::min(std::min(max_block_limit_by_smem(kernel_info), max_block_limit_by_regs(kernel_info)),
                    std::min(max_block_limit_by_warps(kernel_info), max_block_limit_by_others()));
}

int gpu::max_block_limit_by_regs(kernel_info kernel_info) {
    int blocks_per_sm_limit_regs;
    int allocated_active_warps_per_block = (int) (
            ceil(((float) (kernel_info.m_block_size) / (float) m_gpu_configs.m_compute_capability["warp_size"]), 1));

    if (kernel_info.m_num_registers == 0) {
        blocks_per_sm_limit_regs = m_gpu_configs.m_compute_capability["max_active_blocks_per_SM"];
    } else {
        int allocated_regs_per_warp = ceil(
                ((float) (kernel_info.m_num_registers) * (float) (m_gpu_configs.m_compute_capability["warp_size"])),
                (float) m_gpu_configs.m_compute_capability["register_allocation_size"]);
        int allocated_regs_per_sm = (int) (
                floor((float) (m_gpu_configs.m_compute_capability["max_registers_per_block"]) /
                      (float) allocated_regs_per_warp,
                      std::stof(m_gpu_configs.m_gpu_config["num_warp_schedulers_per_SM"])));
        blocks_per_sm_limit_regs = (int) (
                floor(((float) allocated_regs_per_sm / (float) allocated_active_warps_per_block), 1)
                * floor(((float) m_gpu_configs.m_compute_capability["max_registers_per_SM"] /
                         (float) m_gpu_configs.m_compute_capability["max_registers_per_block"]), 1));
    }
    return blocks_per_sm_limit_regs;
}

int gpu::max_block_limit_by_smem(kernel_info kernel_info) {
    int blocks_per_sm_limit_smem;
    if (kernel_info.m_shared_mem_bytes == 0) {
        blocks_per_sm_limit_smem = m_gpu_configs.m_compute_capability["max_active_blocks_per_SM"];
    } else {
        float smem_per_block = ceil((float) kernel_info.m_shared_mem_bytes,
                                    (float) m_gpu_configs.m_compute_capability["smem_allocation_size"]);
        blocks_per_sm_limit_smem = (int) (floor(
                (std::stof(m_gpu_configs.m_gpu_config["shared_mem_size"]) / smem_per_block),
                1));
    }

    return blocks_per_sm_limit_smem;
}

int gpu::max_block_limit_by_warps(kernel_info kernel_info) {
    int allocated_active_warps_per_block = (int) (
            ceil(((float) (kernel_info.m_block_size) / (float) (m_gpu_configs.m_compute_capability["warp_size"])),
                 1));
    int blocks_per_sm_limit_warps = (int) (std::min(m_gpu_configs.m_compute_capability["max_active_blocks_per_SM"],
                                                    (int) (floor(
                                                            ((float) m_gpu_configs.m_compute_capability["max_active_threads_per_SM"] /
                                                             (float) m_gpu_configs.m_compute_capability["warp_size"] /
                                                             (float) allocated_active_warps_per_block), 1))));
    return blocks_per_sm_limit_warps;


}

int gpu::max_block_limit_by_others() {
    return INT_MAX;
}

void gpu::launch_kernel(int kernel_id) {
    m_active_kernel = new kernel(kernel_id);
    read_trace(kernel_id, m_active_kernel->m_kernel_info, m_active_kernel->trace_insts);
    m_active_kernel->init_blocks(stoi(m_gpu_configs.m_gpu_config["block_sim_mode"]));

    if (g_mod) {
        for (int i = 0; i < GPM_NUM; i++) {
            for (auto &sm: g_sm[i]) {
                sm->m_active_kernel = m_active_kernel;
            }
        }
    }
    else {
        for (auto &sm: m_sm) {
            sm->m_active_kernel = m_active_kernel;
        }
    }
}


void gpu::execute_kernel(int kernel_id) {
    first_spawn_block();
}

bool is_ld_st_inst(const string &opcode) {
    const std::vector<string> m_LDST_opcode_list{
            "LD", "LDC", "LDG", "LDL", "LDS", "LDSM", "ST", "STG", "STL", "STS", "MATCH", "QSPC", "ATOM",
            "ATOMS", "ATOMG", "RED", "CCTL", "CCTLL", "ERRBAR", "MEMBAR", "CCTLT"
    };

    for (const auto &item: m_LDST_opcode_list) {
        if (opcode.find(item) != string::npos) {
            return true;
        }
    }
    return false;
}

bool is_ctrl_inst(const string &opcode) {
    const std::vector<string> m_ctrl_opcode_list{"BMOV", "BPT", "BRA", "BREAK", "BRX", "BRXU", "BSSY", "BSYNC", "CALL",
                                                 "EXIT",
                                                 "JMP", "JMX", "JMXU", "KILL", "NANOSLEEP", "RET", "RPCMOV", "RTT",
                                                 "WARPSYNC", "YIELD"};
    for (const auto &item: m_ctrl_opcode_list) {
        if (opcode.find(item) != string::npos) {
            return true;
        }
    }
    return false;
}

int ceil(float x, float s) {
    return (int) (s * std::ceil((float) x / s));
}


int floor(float x, float s) {
    return (int) (s * std::floor((float) x / s));
}


void gpu::first_spawn_block() {
    int max_blocks = max_block_per_sm(m_active_kernel->m_kernel_info);


//    int active_sms = std::min((int) ceil((float) m_active_kernel->m_kernel_info.m_grid_size / (float) max_blocks),
//                              std::stoi(m_gpu_configs.m_gpu_config["sm_num"]));
    int active_sms = std::min((int) m_active_kernel->m_kernel_info.m_grid_size,
                              std::stoi(m_gpu_configs.m_gpu_config["sm_num"]));
    if (g_mod) {
        for (auto &g_sm_part: g_sm) {
            for (auto &sm: g_sm_part) {
                sm->m_max_blocks = max_blocks;
            }
        }
    }
    else {
        for (auto &sm: m_sm) {
            sm->m_max_blocks = max_blocks;
        }
    }

    int part_num = GPM_NUM;
    int sm_num = std::stoi(m_gpu_configs.m_gpu_config["sm_num"]);

    int index = 0;
    for (auto &block: m_active_kernel->m_blocks) {
        if (g_mod) {
            unsigned sm_per_part_num = sm_num / part_num;
            unsigned part_id = index / sm_per_part_num;
            unsigned sm_per_part_id = index % sm_per_part_num;
            if (g_sm[part_id][sm_per_part_id]->block_vec.size() < max_blocks) {
                block.second->load_mem_request("../benchmarks/" + active_benchmark + "/traces/",
                                               std::stoi(m_gpu_configs.l1_cache_config["l1_cache_line_size"]),
                                               block.second->m_block_id);
                g_sm[part_id][sm_per_part_id]->block_vec.push_back(block.second);
                g_sm[part_id][sm_per_part_id]->is_active = true;

                m_active_kernel->block_pointer++;
                index++;
                index %= active_sms;
            }
        }
        else {
            if (m_sm[index]->block_vec.size() < max_blocks) {
                block.second->load_mem_request("../benchmarks/" + active_benchmark + "/traces/",
                                               std::stoi(m_gpu_configs.l1_cache_config["l1_cache_line_size"]),
                                               block.second->m_block_id);
                m_sm[index]->block_vec.push_back(block.second);
                m_sm[index]->is_active = true;
                m_active_kernel->block_pointer++;
                index++;
                index %= active_sms;
            }
        }
    }
}

void sm_unit::unit_cycle(int cycles) {
    if(unit_latency>0){
        unit_latency--;
        return;
    }
    if(unit_name!="LDS_units")
        return;
    if(m_inst == nullptr||m_warp== nullptr)
        return;
    if(mem_fetch_point>=m_inst->m_coalesced_address.size())
        return;
    auto address = m_inst->m_coalesced_address[mem_fetch_point];
    int m_bank_id = address.second;
    if(m_sm->m_queue_sm_to_l1_busy[m_bank_id]) //当前忙，无法处理该请求
        return;
    unsigned int sm_id = m_sm->m_sm_id;
    int m_access_type = m_inst->m_opcode.find("LDG") != string::npos ? 0 : 1; // 0: read 1: write
    unsigned long long m_address = address.first;
    unsigned m_sector_mask = address.second;
    unsigned m_space = GLOBAL;
    bool m_is_atomic = false;
    long long m_gen_time = cycles;
    int m_src_block_id = m_warp->m_block_id;
    int m_src_warp_id = m_warp->m_warp_id;
    int m_completion_index = m_inst->completions_index;
    auto *mf = new mem_fetch(sm_id, m_access_type, m_address, m_sector_mask, m_space,
                             m_is_atomic, m_gen_time, m_src_block_id, m_src_warp_id,
                             m_completion_index);
    mem_fetch_point++;
//    std::cout<<m_src_block_id<<" "<<m_src_warp_id<<" "<<m_address<<" "<<m_sector_mask<<std::endl;
    int bank_id = (int)l1_bank_hash(mf);
    m_sm->m_queue_sm_to_l1[bank_id].push(mf);
    m_sm->m_queue_sm_to_l1_busy.set(bank_id);
    m_warp->increase_pending_request(m_completion_index);
    m_warp->m_pending_mem_request_num++;
    m_sm->m_active_kernel->request_nums++;
    if(mem_fetch_point==m_inst->m_coalesced_address.size()){
        mem_fetch_point = -1;
        m_warp->pending_inst --;
        m_inst = nullptr;
        m_warp = nullptr;
        ready_time = cycles;
        unit_latency = 0;
    }
}

void
LdStInst::process(gpu_config &gpu_config_t, sm_unit *unit, warp &warp, int cycles, long long int pc_t, int pc_index_t,
                  int active_thread_num_t, const string &opcode, unsigned int sm_id_t,
                  std::map<int, std::queue<mem_fetch *>> &queue_sm_to_l1_t, cache_config &l1_cache_config_t,
                  kernel *active_kernel_t) {
    if(opcode.find("LDG") != string::npos || opcode.find("STG") != string::npos){
        process_LDG_STG(gpu_config_t, unit, warp, cycles, pc_t, pc_index_t, active_thread_num_t,
                        opcode, sm_id_t, queue_sm_to_l1_t, l1_cache_config_t, active_kernel_t);
    }
    else if(opcode.find("LDS") != string::npos || opcode.find("STS") != string::npos ||
            opcode.find("ATOMS") != string::npos){
        process_LDS_STS_ATOMS(gpu_config_t, active_thread_num_t, unit, warp, cycles);
    }
    else if(opcode.find("ATOM") != string::npos || opcode.find("ATOMG") != string::npos){
        process_ATOM_ATOMG(unit, warp, pc_t, pc_index_t, sm_id_t, cycles, queue_sm_to_l1_t, l1_cache_config_t);
    }
    else{
        int latency_t = std::get<0>(gpu_config_t.gpu_isa_latency[opcode]);
        unit->ready_time += latency_t;
        warp.completions.push_back(cycles + latency_t);
    }
}

void LdStInst::process_LDS_STS_ATOMS(gpu_config &gpu_config_t, int active_thread_num_t, sm_unit *unit, warp &warp,
                                     int cycles) {
    int latency_t;
    if (active_thread_num_t < 2) {
        latency_t = 8;
    } else if (active_thread_num_t < 4) {
        latency_t = 10;
    } else if (active_thread_num_t < 8) {
        latency_t = 14;
    } else if (active_thread_num_t < 16) {
        latency_t = 22;
    } else if (active_thread_num_t < 32) {
        latency_t = 37;
    } else {
        latency_t = 69;
    }
    unit->ready_time += latency_t;
    warp.completions.push_back(cycles + latency_t);
}

void LdStInst::process_ATOM_ATOMG(sm_unit *unit, warp &warp, long long int pc_t, int pc_index_t, unsigned int sm_id_t,
                                  int cycles, std::map<int, std::queue<mem_fetch *>> &queue_sm_to_l1_t,
                                  cache_config &l1_cache_config_t) {
    unit->ready_time += 4;
    warp.completions.push_back(INT_MAX);
    mem_inst *m_mem_inst = warp.get_mem_inst(pc_t, pc_index_t);
    for (auto &address: m_mem_inst->m_coalesced_address) {
        unsigned int sm_id = sm_id_t;
        int m_access_type = 1;  //read
        unsigned long long m_address = address.first;
        unsigned m_sector_mask = address.second;
        unsigned m_space = GLOBAL;
        bool m_is_atomic = true;
        long long m_gen_time = cycles;
        int m_src_block_id = warp.m_block_id;
        int m_src_warp_id = warp.m_warp_id;
        int m_completion_index = (int) warp.completions.size() - 1;
        auto *mf = new mem_fetch(sm_id, m_access_type, m_address, m_sector_mask, m_space,
                                 m_is_atomic, m_gen_time, m_src_block_id, m_src_warp_id,
                                 m_completion_index);
        queue_sm_to_l1_t[l1_bank_hash(mf)].push(mf);
        warp.increase_pending_request(m_completion_index);
        warp.m_pending_mem_request_num++;
    }
}

void LdStInst::process_LDG_STG(gpu_config &gpu_config_t, sm_unit *unit, warp &warp, int cycles, long long int pc_t,
                               int pc_index_t, int active_thread_num_t, const string &opcode, unsigned int sm_id_t,
                               std::map<int, std::queue<mem_fetch *>> &queue_sm_to_l1_t,
                               cache_config &l1_cache_config_t, kernel *active_kernel_t) {
    mem_inst *m_mem_inst = warp.get_mem_inst(pc_t, pc_index_t);
    if (m_mem_inst == nullptr || active_thread_num_t == 0|| m_mem_inst->m_coalesced_address.empty()) {
        int latency = 1;
        unit->ready_time += latency;
        warp.completions.push_back(cycles + latency);
    } else {
        warp.completions.push_back(INT_MAX);
        unit->ready_time = INT_MAX;
        m_mem_inst->completions_index=warp.completions.size()-1;
        unit->set_ldst_inst(m_mem_inst,&warp);
        warp.pending_inst ++;
    }
}

