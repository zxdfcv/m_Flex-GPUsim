//
// Created by root on 22-10-25. //from gpusim
//

#ifndef FLEX_GPUSIM_CROSSBAR_H
#define FLEX_GPUSIM_CROSSBAR_H
#define GPM_NUM 1
#define g_mod 1

#include <iostream>
#include <map>
#include <queue>
#include <vector>


enum Interconnect_type { REQ_NET = 0, REPLY_NET = 1 };

enum Arbiteration_type { NAIVE_RR = 0 };

struct inct_config {
    // config for local interconnect
    unsigned in_buffer_limit;
    unsigned out_buffer_limit;
    unsigned subnets;
    Arbiteration_type arbiter_algo;
};

class xbar_router {
public:
    void creat_new_xbar_router(unsigned router_id, enum Interconnect_type m_type,
                unsigned n_shader, unsigned n_mem,
                const struct inct_config& m_localinct_config, unsigned GPM_num, int xbar_connect_width);
    xbar_router(unsigned router_id, enum Interconnect_type m_type,
                unsigned n_shader, unsigned n_mem,
                const struct inct_config& m_localinct_config);
    ~xbar_router();
    void Push(unsigned input_deviceID, unsigned output_deviceID, void* data,
              unsigned int size);
    void* Pop(unsigned ouput_deviceID);
    void Advance();

    bool Busy() const;
    bool Has_Buffer_In(unsigned input_deviceID, unsigned size);
    bool Has_Buffer_Out(unsigned output_deviceID, unsigned size);
    //
    void new_Push(unsigned input_deviceID, unsigned output_deviceID,
                  void* data, unsigned int size, int n);

    int get_dst_GPM(unsigned output_deviceID); // -> int Dst
    int get_next_GPM(int Dst); //2->last; 3->next;
    //
private:
    void RR_Advance();
    void GG_Advance();

    struct Packet {
        Packet(void* m_data, unsigned m_output_deviceID) {
            data = m_data;
            output_deviceID = m_output_deviceID;
        }
        void* data;
        unsigned output_deviceID;
    };
    std::vector<std::queue<Packet> > in_buffers;
    std::vector<std::queue<Packet> > out_buffers;
    unsigned _n_shader, _n_mem, total_nodes;
    unsigned in_buffer_limit, out_buffer_limit;
    unsigned next_node_id;       // used for RR arbit
    unsigned m_id;
    enum Interconnect_type router_type;
    unsigned active_in_buffers, active_out_buffers;
    Arbiteration_type arbit_type;

    friend class LocalInterconnect;
    //
    unsigned m_GPM_num;
    unsigned new_total_nodes;
    unsigned m_xbar_connect_width;
    unsigned active_in_buffers_0, active_out_buffers_0;
    unsigned active_in_buffers_1, active_out_buffers_1;
    unsigned active_in_buffers_2, active_out_buffers_2;
    unsigned active_in_buffers_3, active_out_buffers_3;
//        0
//    1  [0]  3
//        2
//    std::vector<std::queue<Packet> > in_buffers_0;
//    std::vector<std::queue<Packet> > out_buffers_0;
//
//    std::vector<std::queue<Packet> > in_buffers_1;
//    std::vector<std::queue<Packet> > out_buffers_1;
//
//    std::vector<std::queue<Packet> > in_buffers_2; //last
//    std::vector<std::queue<Packet> > out_buffers_2;
//
//    std::vector<std::queue<Packet> > in_buffers_3; //next
//    std::vector<std::queue<Packet> > out_buffers_3;
    std::vector<std::vector<std::queue<Packet>>> in_buffers_list;
    std::vector<std::vector<std::queue<Packet>>> out_buffers_list;
//    std::vector<std::queue<Packet> > in_buffers_from_sm;
//    std::vector<std::queue<Packet> > out_buffers_to_l2;
//
//    std::vector<std::queue<Packet> > in_buffers_from_l2;
//    std::vector<std::queue<Packet> > out_buffers_to_sm;
//
//    std::vector<std::queue<Packet> > in_buffers_from_last;
//    std::vector<std::queue<Packet> > out_buffers_to_last;
//
//    std::vector<std::queue<Packet> > in_buffers_from_next;
//    std::vector<std::queue<Packet> > out_buffers_to_next;
    //
};

class LocalInterconnect {
public:
    LocalInterconnect(const struct inct_config& m_localinct_config);
    ~LocalInterconnect();
    static LocalInterconnect* New(const struct inct_config& m_inct_config);
    void CreateInterconnect(unsigned n_shader, unsigned n_mem);

    // node side functions
    void Init();
    void Push(unsigned input_deviceID, unsigned output_deviceID, void* data,
              unsigned int size);
    void* Pop(unsigned ouput_deviceID);
    void Advance();
    bool Busy() const;
    bool HasBuffer(unsigned deviceID, unsigned int size) const;


protected:
    const inct_config& m_inct_config;
    unsigned n_shader, n_mem;
    unsigned n_subnets;
    std::vector<xbar_router*> net;

};

//
class MCM_icnt {
    std::vector<LocalInterconnect> icnts;
    void init() {
//        for()
    }

    void icnt_cycle() {
//        for () icnts[i]->Advance();
//        icnt_connect_cycle();
    }

    void icnt_connect_cycle() {

    }
};
//

#endif //FLEX_GPUSIM_CROSSBAR_H
