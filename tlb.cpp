//
// Created by 刘浩 on 2022/7/6.
//

#include "tlb.h"

bool tlb::is_hit(unsigned long long addr) {
    if(m_start_addr - m_size <= addr && addr < m_start_addr + m_size){
        return true;
    }
    else{
        m_start_addr = addr;
        return false;
        //访问页表
    }
}