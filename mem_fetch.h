//
// Created by 徐向荣 on 2022/6/13.
//

#ifndef FLEX_GPUSIM_MEM_FETCH_H
#define FLEX_GPUSIM_MEM_FETCH_H
#define SECTOR_SIZE  32
typedef unsigned long long new_addr_type;
enum mem_fetch_space {
    GLOBAL = 0,
    LOCAL,
    SHARE
};

enum mem_fetch_status {
    L1_HIT = 0,
    L1_MISS,
    L2_HIT,
    L2_MISS,
    INIT
};

enum mem_fetch_write_type {
    NORMAL = 0,
    WRITE_THROUGH,
    WRITE_BACK
};

class mem_fetch {
public:
    mem_fetch(unsigned sm_id, bool access_type,
              new_addr_type address, unsigned sector_mask, unsigned space, bool is_atomic,
              long long gen_time, int src_block_id, int src_warp_id, int completion_index) {
        m_sm_id = sm_id;
        m_access_type = access_type;
        m_address = address; //cache line address
        m_sector_mask = sector_mask;
        m_space = space;
        m_is_atomic = is_atomic;
        m_gen_time = gen_time;
        m_src_block_id = src_block_id;
        m_src_warp_id = src_warp_id;
        m_completion_index = completion_index;
        m_status = INIT;
        m_write_type = NORMAL;
        m_sector_address = m_address + sector_mask * SECTOR_SIZE;
        m_packet_size = 0; // TODO give a size @Xu
        m_l1_ready_time = 0;
    }


    bool is_atomic() const {
        return m_is_atomic;
    }

    bool is_write() const {
        return m_access_type; //write true
    }

    int get_mem_sub_partition_id(){
        //m_sector_address
        //chip
        //bank
        //channel 22
        //partition per channel 2
        new_addr_type addr = m_sector_address;
        int ADDR_CHIP_S = 10, m_n_channel = 22, BANk_MASK = 0x300, ADDR_BANK_S = 8;
        unsigned long long int addr_for_chip;
        unsigned long long int PARTITION_PER_CHANNEL = 2;
        addr_for_chip = (addr >> ADDR_CHIP_S) % m_n_channel;

        chip = addr_for_chip;
        bk = (addr & BANk_MASK) >> ADDR_BANK_S;
        return (int)(chip * PARTITION_PER_CHANNEL + (bk & (PARTITION_PER_CHANNEL - 1))) + 68;
    }

    unsigned m_sm_id;
    bool m_access_type;
    new_addr_type m_address;
    unsigned m_sector_mask;
    unsigned m_space;
    bool m_is_atomic;
    long long m_gen_time;
    int m_status;
    int m_write_type;
    int m_src_block_id;
    int m_src_warp_id;
    int m_completion_index;
    new_addr_type m_sector_address;
    bool m_tlb_hit;
    unsigned m_packet_size;
    int m_l1_ready_time;

    enum { CHIP = 0, BK = 1, ROW = 2, COL = 3, BURST = 4, N_ADDRDEC };
    new_addr_type chip;
    new_addr_type bk;
    unsigned char addrdec_mklow[N_ADDRDEC];
    unsigned char addrdec_mkhigh[N_ADDRDEC];
    new_addr_type addrdec_mask[N_ADDRDEC];
};

#endif //FLEX_GPUSIM_MEM_FETCH_H
