//
// Created by 徐向荣 on 2022/6/12.
//

#include "cache.h"
#include "gpu.h"
std::vector<int> l2_data_cache::g_l2_list;
unsigned int LOGB2(unsigned int v) {
    unsigned int shift;
    unsigned int r;

    r = 0;

    shift = ((v & 0xFFFF0000) != 0) << 4;
    v >>= shift;
    r |= shift;
    shift = ((v & 0xFF00) != 0) << 3;
    v >>= shift;
    r |= shift;
    shift = ((v & 0xF0) != 0) << 2;
    v >>= shift;
    r |= shift;
    shift = ((v & 0xC) != 0) << 1;
    v >>= shift;
    r |= shift;
    shift = ((v & 0x2) != 0) << 0;
    v >>= shift;
    r |= shift;

    return r;
}

unsigned cache_config::hash_function(new_addr_type addr, unsigned m_nset,
                                     unsigned m_line_sz_log2,
                                     unsigned m_n_set_log2) const {
    unsigned set_index = 0;
    set_index = (addr >> m_line_sz_log2) & (m_nset - 1); // >> 32
    return set_index;
}


bool mshr_table::probe(new_addr_type block_addr) const {
    auto a = m_data.find(block_addr);
    return a != m_data.end();
}

bool mshr_table::full(new_addr_type block_addr) const {
    auto i = m_data.find(block_addr);
    if (i != m_data.end())
        return i->second.m_list.size() >= m_max_merged;
    else
        return m_data.size() >= m_num_entries;
}


void mshr_table::add(new_addr_type block_addr, mem_fetch *mf) {
    m_data[block_addr].m_list.push_back(mf);
    if (mf->is_atomic()) {
        m_data[block_addr].m_has_atomic = true;
    }
}


mem_fetch *mshr_table::next_access(new_addr_type address) {
    //address only correct in sector_cache case
    mem_fetch *result = m_data[address].m_list.front();
    m_data[address].m_list.pop_front();
    if (m_data[address].m_list.empty()) {
        // release entry
        m_data.erase(address);
    }
    return result;
}

void
tag_array::tag_array_probe_idle(new_addr_type addr, unsigned int sector_mask, mem_fetch *mem_fetch,
                                int &idx) {
    unsigned set_index = m_config.set_index(addr);
    for (unsigned way = 0; way < m_config.m_assoc; way++) {
        unsigned index = set_index * m_config.m_assoc + way;
        sector_cache_block *line = m_lines[index];
        if (line->is_invalid_line()) {
            idx = (int)index;
            return;
        }
    }
    idx = -1;
}

unsigned
tag_array::tag_array_probe(new_addr_type addr, unsigned int sector_mask, mem_fetch *mem_fetch,
                           unsigned &idx) {

    unsigned set_index = m_config.set_index(addr);
    new_addr_type tag = m_config.tag(addr);

    auto invalid_line = (unsigned) -1;
    auto valid_line = (unsigned) -1;
    unsigned long long valid_timestamp = (unsigned) -1;
    bool all_reserved = true;

    for (unsigned way = 0; way < m_config.m_assoc; way++) {
        unsigned index = set_index * m_config.m_assoc + way;
        sector_cache_block *line = m_lines[index];
        if (line->m_tag == tag) {
            if (line->get_status(sector_mask) == RESERVED) {
                idx = index;
                return HIT_RESERVED;
            } else if (line->get_status(sector_mask) == VALID) {
                idx = index;
                return HIT;
            } else if (line->get_status(sector_mask) == MODIFIED) {
                if (line->is_readable(sector_mask)) {
                    idx = index;
                    return HIT;
                } else {
                    idx = index;
                    return SECTOR_MISS;
                }
            } else if (line->is_valid_line() && line->get_status(sector_mask) == INVALID) {
                idx = index;
                return SECTOR_MISS;
            } else { ;
            }
        }
        if (!line->is_reserved_line()) {
            all_reserved = false;
            if (line->is_invalid_line()) {
                invalid_line = index;
            } else {
                // valid line : keep track of most appropriate replacement candidate
                if (m_config.m_replacement_policy == "LRU") {
                    if (line->get_last_access_time() < valid_timestamp) {
                        valid_timestamp = line->get_last_access_time();
                        valid_line = index;
                    }
                } else if (m_config.m_replacement_policy == "FIFO") {
                    if (line->get_alloc_time() < valid_timestamp) {
                        valid_timestamp = line->get_alloc_time();
                        valid_line = index;
                    }
                }
            }
        }
    }
    if (all_reserved) {
        return RESERVATION_FAIL;  // miss and not enough space in cache to allocate
    }
    if (invalid_line != (unsigned) -1) {
        idx = invalid_line;
    } else if (valid_line != (unsigned) -1) {
        idx = valid_line;
    } else
        abort();  // if an unreserved block exists, it is either invalid or

    return MISS;
}


unsigned
tag_array::tag_array_access(new_addr_type addr, unsigned int time, mem_fetch *mem_fetch, unsigned int &idx, bool &wb) {
    unsigned status = tag_array_probe(addr, mem_fetch->m_sector_mask, mem_fetch, idx);
    switch (status) {
        case HIT_RESERVED:
        case HIT:
            m_lines[idx]->set_last_access_time(time, mem_fetch->m_sector_mask);
            break;
        case MISS:
            break;
        case SECTOR_MISS:
            break;
        case RESERVATION_FAIL:
            break;
        default:
            abort();
    }
    return status;
}

void tag_array::tag_array_fill(new_addr_type addr, unsigned int time, unsigned int mask) {
    unsigned idx;
    unsigned status = tag_array_probe(addr, mask, nullptr, idx);
    if (status == MISS)
        m_lines[idx]->allocate(m_config.tag(addr), m_config.block_addr(addr), time, mask);
    else if (status == SECTOR_MISS) {
        ((sector_cache_block *) m_lines[idx])->allocate_sector(time, mask);
    }
    m_lines[idx]->fill(time, mask);
}

unsigned tag_array::tag_array_access(new_addr_type addr, unsigned int time, mem_fetch *mem_fetch, unsigned int &idx) {
    bool wb = false;
    unsigned result = tag_array_access(addr, time, mem_fetch, idx, wb);
    return result;
}


unsigned l1_data_cache::cache_access(new_addr_type addr, mem_fetch *mf, unsigned int time) {
    bool wr = mf->is_write();
    bool wb = false;
    if (wr) {
        m_write_cache_access_num++;
//        std::cout <<"l1_access_num"<<m_read_cache_access_num<<std::endl;
    } else {
        m_read_cache_access_num++;
//        std::cout <<"l1_access_num"<<m_read_cache_access_num<<std::endl;
    }
    new_addr_type block_addr = m_cache_config.block_addr(addr);
    auto cache_index = (unsigned) -1; //if hit index miss next evict index

    unsigned probe_status = m_tag_array->tag_array_probe(addr, mf->m_sector_mask, mf, cache_index);
    if (wr) {

        if (probe_status == RESERVATION_FAIL) {
            return RESERVATION_FAIL;
        } else if (probe_status == HIT) {
            m_store_sector_hit++;
            if (m_cache_config.m_write_policy == "LOCAL_WB_GLOBAL_WT") {
                if (mf->m_space == GLOBAL) {
                    sector_cache_block *block = m_tag_array->get_block(cache_index);
                    mf->m_write_type = WRITE_THROUGH;
//                    m_miss_queue.push(mf);
                    block->set_status(MODIFIED, mf->m_sector_mask);
                } else if (mf->m_space == LOCAL) {
                    m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
                    sector_cache_block *block = m_tag_array->get_block(cache_index);
                    block->set_status(MODIFIED, mf->m_sector_mask);
                }
            } else if (m_cache_config.m_write_policy == "WRITE_BACK") {
                m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
                sector_cache_block *block = m_tag_array->get_block(cache_index);
                block->set_status(MODIFIED, mf->m_sector_mask);
            } else {
                printf("error");
            }
            mf->m_status = L1_HIT;
            if(m_gpu_config.m_gpu_config.find("l1_hit_latency") == m_gpu_config.m_gpu_config.end()){
                printf("l1_hit_latency does not exist!\n");
                exit(1);
            }
//            if(mf->m_sm_id>68){
//                printf("error");
//            }
            mf->m_l1_ready_time = std::stoi(m_gpu_config.m_gpu_config["l1_hit_latency"])+(int)time;
            l1_to_sm.push(mf);
            return HIT;
        } else {
            //L1 write miss
            //fetch sector and write
            new_addr_type mshr_addr = m_cache_config.mshr_addr(mf->m_sector_address);
//            bool mshr_hit = m_mshrs->probe(mshr_addr);
//            bool mshr_avail = !m_mshrs->full(mshr_addr);
//            if (!mshr_avail) {
//                return RESERVATION_FAIL;
//            }
//            if (mshr_hit) {
//                m_store_sector_hit++;
//                if (m_cache_config.m_write_policy == "LOCAL_WB_GLOBAL_WT") {
//                    if (mf->m_space == GLOBAL) {
//                        sector_cache_block *block = m_tag_array->get_block(cache_index);
//                        mf->m_write_type = WRITE_THROUGH;
//                        mf->m_tlb_hit = m_l1_tlb->is_hit(mf->m_address);
//                        m_miss_queue.push(mf);
//                        m_sector_store_to_next++;
//                        block->set_status(MODIFIED, mf->m_sector_mask);
//                    } else {
//                        m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
//                        sector_cache_block *block = m_tag_array->get_block(cache_index);
//                        block->set_status(MODIFIED, mf->m_sector_mask);
//                    }
//                } else if (m_cache_config.m_write_policy == "WRITE_BACK") {
//                    m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
//                    sector_cache_block *block = m_tag_array->get_block(cache_index);
//                    block->set_status(MODIFIED, mf->m_sector_mask);
//                } else {
//                    printf("error");
//                }
//                m_mshrs->add(mshr_addr, mf);
//            } else {
//                m_mshrs->add(mshr_addr, mf);
                m_store_sector_miss++;
                mf->m_tlb_hit = m_l1_tlb->is_hit(mf->m_address);
                m_miss_queue.push(mf);
                m_sector_store_to_next++;
//            }
            return MISS;
        }
    } else {
        if (probe_status == RESERVATION_FAIL) {
            return RESERVATION_FAIL;
        } else if (probe_status == HIT) {
            m_load_sector_hit++;
            m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
            mf->m_status = L1_HIT;
//            if(mf->m_sm_id>68){
//                printf("error");
//            }
            mf->m_l1_ready_time = std::stoi(m_gpu_config.m_gpu_config["l1_hit_latency"])+(int)time;
            l1_to_sm.push(mf);
            return HIT;
        } else {
            new_addr_type mshr_addr = m_cache_config.mshr_addr(mf->m_sector_address);
            bool mshr_hit = m_mshrs->probe(mshr_addr);
            bool mshr_avail = !m_mshrs->full(mshr_addr);
            if (!mshr_avail) {
                return RESERVATION_FAIL;
            }
            if (mshr_hit) {
                m_load_sector_hit++;
                m_mshrs->add(mshr_addr, mf);
                m_l1_to_l2_mshr++;
            } else {
                m_load_sector_miss++;
                m_mshrs->add(mshr_addr, mf);
                mf->m_tlb_hit = m_l1_tlb->is_hit(mf->m_address);
                m_miss_queue.push(mf);
                m_l1_to_l2_num++;
            }
            return MISS;
        }
    }
    printf("some status wrong\n");
    return MISS;
}

void l1_data_cache::l1_cache_cycle(int cycles) {
//    for (auto &mf: l1_to_sm) { //l1->sm first:mf second:remaining mem cycles
//        mf->m_l1_wait_time --;
//    }
    //TODO process fill request
    //process miss request
    m_miss_queue_cycle();
    //process l1 request
    for (int bank = 0; bank < m_cache_config.m_n_banks; bank++) {
        if (!m_sm_to_l1[bank].empty()) {
            mem_fetch *mf = m_sm_to_l1[bank].front();
            unsigned  status = cache_access(mf->m_address, mf, cycles);
            if(status == RESERVATION_FAIL)
                continue;
            else{
                m_sm_to_l1[bank].pop();
            }
        }
    }
}

void l1_data_cache::m_miss_queue_cycle() {
    if(m_miss_queue.empty())
        return;
    mem_fetch* mf_next = m_miss_queue.front();
    unsigned request_size = mf_next->m_packet_size;
    unsigned output_node = mf_next->get_mem_sub_partition_id();
    if(m_gpu->m_icnt->HasBuffer(sm_id,request_size)){
        m_gpu->m_icnt->Push(sm_id,output_node,mf_next,request_size);
        m_miss_queue.pop();
    }
}


void l1_data_cache::cache_fill(mem_fetch *mf, unsigned time) {
    m_tag_array->tag_array_fill(mf->m_address, time, mf->m_sector_mask);
}



void l2_data_cache::l2_cache_cycle(int cycles) {
    for(auto it = l2_to_dram.begin(); it != l2_to_dram.end(); it++){
        it->second--;
    }
    //L2 cache return
    auto it = l2_to_sm.begin();
    for (; it != l2_to_sm.end();) {
        it->second -= 1;
        if(it->second<=0){
            m_gpu->m_icnt->Push(m_device_id,it->first->m_sm_id,it->first,it->first->m_packet_size);
            if(it->first->m_status != L2_HIT){
                new_addr_type mshr_address = it->first->m_sector_address;
                while (m_mshrs->probe(mshr_address)){
                    mem_fetch *mf_fill = m_mshrs->next_access(mshr_address);
                    cache_fill(mf_fill, cycles); //L2 fill
                }
            }
            auto tmp = it;
            it = l2_to_sm.erase(tmp);
        } else{
            it++;
        }
    }


    auto *mf = (mem_fetch *)m_gpu->m_icnt->Pop(m_device_id);
    if(mf){
        m_l1_to_l2.push(mf);
        l2_data_cache::g_l2_list[m_device_id-68]++;
    }

    if(!m_l1_to_l2.empty()){
        mem_fetch *mf_next = m_l1_to_l2.front();

        unsigned  status = cache_access(mf_next->m_address, mf_next, cycles);
        if(status!=RESERVATION_FAIL)
            m_l1_to_l2.pop();
    }

}

unsigned l2_data_cache::cache_access(new_addr_type addr, mem_fetch *mf, unsigned int time) { // L2 not talk about write_back
    // TODO L2 delete write back request
    bool wr = mf->is_write();
    bool wb;
    if (wr) {
        m_write_cache_access_num++;
//        std::cout <<"l2_access_num_wr"<<m_read_cache_access_num<<std::endl;
    } else {
        m_read_cache_access_num++;
//        std::cout <<"l2_access_num"<<m_read_cache_access_num<<std::endl;

    }
    new_addr_type block_addr = m_cache_config.block_addr(addr);
    auto cache_index = (unsigned) -1; //if hit index miss next evict index

    unsigned probe_status = m_tag_array->tag_array_probe(addr, mf->m_sector_mask, mf, cache_index);
    if (wr) {

        if (probe_status == RESERVATION_FAIL) {
            return RESERVATION_FAIL;
        } else if (probe_status == HIT) {
            m_store_sector_hit++;
            m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
            sector_cache_block *block = m_tag_array->get_block(cache_index);
            block->set_status(MODIFIED, mf->m_sector_mask);
            //l2 cache 命中，则tlb一定命中
            if(mf->m_write_type == WRITE_THROUGH){
                delete mf;
            }else{
                if(mf->m_tlb_hit){
                    send_l2_to_sm(mf,std::stoi(m_gpu_config.m_gpu_config["tlb_hit_l2_hit_latency"]));
                } else{
                    send_l2_to_sm(mf,std::stoi(m_gpu_config.m_gpu_config["tlb_miss_l2_write_hit_latency"]));
                }
                mf->m_status = L2_HIT;
            }
            return HIT;
        } else {
            int m_cache_index;
            m_tag_array->tag_array_probe_idle(block_addr,mf->m_sector_mask, mf, m_cache_index);
            if(m_cache_index>=0){
                m_store_sector_hit++;
                sector_cache_block *block = m_tag_array->get_block(m_cache_index);
                if(probe_status==MISS){
                    block->allocate(m_tag_array->m_config.tag(addr), m_tag_array->m_config.block_addr(addr),
                                    time, mf->m_sector_mask);
                } else if(probe_status==SECTOR_MISS){
                    block->allocate_sector(time, mf->m_sector_mask);
                }
                block->set_status(MODIFIED, mf->m_sector_mask);
                block->set_last_access_time(time, mf->m_sector_mask);
                //由于l2 cache写设计，存在tlb miss但是l2 cache命中（被认为命中）的情况
                if(mf->m_write_type == WRITE_THROUGH){
                    delete mf;
                }else{
                    if(mf->m_tlb_hit){
                        send_l2_to_sm(mf,std::stoi(m_gpu_config.m_gpu_config["tlb_hit_l2_hit_latency"]));
                        send_l2_to_dram(mf, 427);
                    } else{
                        send_l2_to_sm(mf,std::stoi(m_gpu_config.m_gpu_config["tlb_miss_l2_write_hit_latency"]));
                    }
                    mf->m_status = L2_HIT;
                }
                return HIT;
            }else{
                //TODO fix dram bugs
                if(mf->m_write_type == WRITE_THROUGH){
                    delete mf;
                }else{
                    if(mf->m_tlb_hit){
                        send_l2_to_sm(mf,std::stoi(m_gpu_config.m_gpu_config["tlb_hit_l2_miss_latency"]));
                    } else{
                        send_l2_to_sm(mf,std::stoi(m_gpu_config.m_gpu_config["tlb_miss_l2_miss_latency"]));
                    }
//                    new_addr_type mshr_addr = m_cache_config.mshr_addr(mf->m_sector_address);
//                    m_mshrs->add(mshr_addr, mf);
                    mf->m_status = L2_MISS;
                }
                return MISS;
            }
        }
    } else {
        if (probe_status == RESERVATION_FAIL) {
            return RESERVATION_FAIL;
        } else if (probe_status == HIT) {
            m_load_sector_hit++;
            m_tag_array->tag_array_access(block_addr, time, mf, cache_index);

            send_l2_to_sm(mf,std::stoi(m_gpu_config.m_gpu_config["tlb_hit_l2_hit_latency"]));
//            printf("l2_hit\n");
            mf->m_status = L2_HIT;
            return HIT;
        } else {
            new_addr_type mshr_addr = m_cache_config.mshr_addr(mf->m_sector_address);
            bool mshr_hit = m_mshrs->probe(mshr_addr);
            bool mshr_avail = !m_mshrs->full(mshr_addr);
            if (!mshr_avail) {
                return RESERVATION_FAIL;
            }
            if (mshr_hit) {
                m_load_sector_hit++;
                m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
                m_mshrs->add(mshr_addr, mf);
                send_l2_to_sm(mf,std::stoi(m_gpu_config.m_gpu_config["tlb_hit_l2_miss_latency"]));
//                printf("l2_mshr_hit\n");
            } else {
                m_load_sector_miss++;
                m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
                m_mshrs->add(mshr_addr, mf);
                if(mf->m_tlb_hit){
                    send_l2_to_sm(mf,std::stoi(m_gpu_config.m_gpu_config["tlb_hit_l2_miss_latency"]));
//                    printf("l2_miss\n");
                } else{
                    send_l2_to_sm(mf,std::stoi(m_gpu_config.m_gpu_config["tlb_miss_l2_miss_latency"]));
//                    printf("l2_miss\n");
                }


                m_sector_load_to_next++;
                m_tag_array->tag_array_access(block_addr, time, mf, cache_index,wb);
            }
            mf->m_status = L2_MISS;
            return MISS;
        }
    }
}

void l2_data_cache::cache_fill(mem_fetch *mf, unsigned int time) {
    m_tag_array->tag_array_fill(mf->m_address, time, mf->m_sector_mask);
}


void memory_partition::init(gpu* gpu, int n) {
    m_gpu = gpu;
    int temp = std::stoi(m_gpu_config.m_gpu_config["mem_num"]);
    for (int i = 0;i < temp/GPM_NUM; i++) {
        l2_data_cache* l2 = new l2_data_cache(m_cache_config,m_gpu_config,i + n*temp/GPM_NUM,gpu);
        l2->init(this);
        m_l2_caches.push_back(l2);
        l2_data_cache::g_l2_list.emplace_back(0);
    }
}
