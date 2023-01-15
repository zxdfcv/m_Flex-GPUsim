//
// Created by 刘浩 on 2022/7/7.
//

#ifndef FLEX_GPUSIM_TLB_H
#define FLEX_GPUSIM_TLB_H

#include <string>


class tlb{
public:
    tlb(unsigned long long size,unsigned long long start_addr){
        m_size = size;
        m_start_addr = start_addr;
    }
    unsigned long long  m_size;
    unsigned long long  m_start_addr;

    bool is_hit(unsigned long long);
};
#endif //FLEX_GPUSIM_TLB_H
