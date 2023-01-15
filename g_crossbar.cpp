//
// Created by root on 22-10-25. //from gpu-sim
//

#include "g_crossbar.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#include "mem_fetch.h"
//
void xbar_router::creat_new_xbar_router(unsigned router_id, enum Interconnect_type m_type,
                         unsigned n_shader, unsigned n_mem,
                         const struct inct_config& m_localinct_config, unsigned GPM_num, int xbar_connect_width) {
    m_id = router_id;
    router_type = m_type;
    _n_mem = n_mem;
    _n_shader = n_shader;
    total_nodes = n_shader + n_mem;
    int GPM_shader = _n_shader / GPM_NUM;
    int GPM_mem = _n_mem / GPM_NUM;
    new_total_nodes = GPM_shader * GPM_mem * xbar_connect_width;
    in_buffers.resize(total_nodes);
    out_buffers.resize(total_nodes);
    in_buffer_limit = m_localinct_config.in_buffer_limit;
    out_buffer_limit = m_localinct_config.out_buffer_limit;
    arbit_type = m_localinct_config.arbiter_algo;
    next_node_id = 0;
    if (m_type == REQ_NET) {
        active_in_buffers = n_shader;
        active_out_buffers = n_mem;
    } else if (m_type == REPLY_NET) {
        active_in_buffers = n_mem;
        active_out_buffers = n_shader;
    }
    m_GPM_num = GPM_num;
    m_xbar_connect_width = xbar_connect_width;
    active_in_buffers_0 = GPM_shader; active_out_buffers_0 = GPM_mem;
    active_in_buffers_1 = GPM_mem; active_out_buffers_1 = GPM_shader;
    active_in_buffers_2 = xbar_connect_width; active_out_buffers_2 = xbar_connect_width;
    active_in_buffers_3 = xbar_connect_width; active_out_buffers_3 = xbar_connect_width;
}
//

xbar_router::xbar_router(unsigned router_id, enum Interconnect_type m_type,
                         unsigned n_shader, unsigned n_mem,
                         const struct inct_config& m_localinct_config) {
    m_id = router_id;
    router_type = m_type;
    _n_mem = n_mem;
    _n_shader = n_shader;
    total_nodes = n_shader + n_mem;
    in_buffers.resize(total_nodes);
    out_buffers.resize(total_nodes);
    in_buffer_limit = m_localinct_config.in_buffer_limit;
    out_buffer_limit = m_localinct_config.out_buffer_limit;
    arbit_type = m_localinct_config.arbiter_algo;
    next_node_id = 0;
    if (m_type == REQ_NET) {
        active_in_buffers = n_shader;
        active_out_buffers = n_mem;
    } else if (m_type == REPLY_NET) {
        active_in_buffers = n_mem;
        active_out_buffers = n_shader;
    }

}

xbar_router::~xbar_router() {}

void xbar_router::Push(unsigned input_deviceID, unsigned output_deviceID,
                       void* data, unsigned int size) {
    in_buffers[input_deviceID].push(Packet(data, output_deviceID));
}

//
void xbar_router::new_Push(unsigned int input_deviceID, unsigned int output_deviceID,
                           void *data, unsigned int size, int n) {
    in_buffers[input_deviceID].push(Packet(data, output_deviceID));

}
//
void* xbar_router::Pop(unsigned ouput_deviceID) {
    void* data = NULL;
    if (!out_buffers[ouput_deviceID].empty()) {
        data = out_buffers[ouput_deviceID].front().data;
        out_buffers[ouput_deviceID].pop();
    }
    return data;
}

bool xbar_router::Has_Buffer_In(unsigned input_deviceID, unsigned size) {
    bool has_buffer =
            (in_buffers[input_deviceID].size() + size <= in_buffer_limit);
    return has_buffer;
}

bool xbar_router::Has_Buffer_Out(unsigned output_deviceID, unsigned size) {
    return (out_buffers[output_deviceID].size() + size <= out_buffer_limit);
}

void xbar_router::Advance() {
    if (arbit_type == NAIVE_RR)
        RR_Advance();
    else
       abort();
}

void xbar_router::RR_Advance() {
    bool active = false;
    std::vector<bool> issued(total_nodes, false);
    unsigned conflict_sub = 0;
    unsigned reqs = 0;

    for (unsigned i = 0; i < total_nodes; ++i) {
        unsigned node_id = (i + next_node_id) % total_nodes;

        if (!in_buffers[node_id].empty()) {
            active = true;
            Packet _packet = in_buffers[node_id].front();

            if (Has_Buffer_Out(_packet.output_deviceID, 1)) {
                if (!issued[_packet.output_deviceID]) {
                    out_buffers[_packet.output_deviceID].push(_packet);
                    in_buffers[node_id].pop();
                    issued[_packet.output_deviceID] = true;
                    reqs++;
                } else
                    conflict_sub++;
            } else {
                if (issued[_packet.output_deviceID]) conflict_sub++;
            }
        }
    }
    next_node_id = (++next_node_id % total_nodes);
}
//
void xbar_router::GG_Advance() {
//    bool active = false;
//    std::vector<bool> issued(total_nodes, false);

    std::vector<std::vector<bool>> issued_list;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned m_total_nodes = (i == 0 || i == 1) ? total_nodes : m_xbar_connect_width;
        std::vector<bool> (m_issued)(m_total_nodes, false);
        issued_list.push_back(m_issued);
    }

    unsigned conflict_sub = 0;
    unsigned reqs = 0;
    for (unsigned buffer_num = 0; buffer_num < 4; ++buffer_num) {

        unsigned m_total_nodes = (buffer_num == 0) ? _n_shader :
                                 (buffer_num == 1) ? _n_mem :
                                 m_xbar_connect_width;

        for (unsigned i = 0; i < m_total_nodes; ++i) {
            unsigned node_id = (i + next_node_id) % m_total_nodes;

            if (!in_buffers_list[buffer_num][node_id].empty()) {
//                active = true;
                Packet _packet = in_buffers_list[buffer_num][node_id].front();

                if (Has_Buffer_Out(_packet.output_deviceID, 1)) {
                    if (!issued_list[buffer_num][_packet.output_deviceID]) {
                        int Dst = get_dst_GPM(_packet.output_deviceID);
                        int next_GPM_num = get_next_GPM(Dst);
                        out_buffers_list[next_GPM_num][_packet.output_deviceID].push(_packet);
                        in_buffers_list[buffer_num][node_id].pop();
                        issued_list[buffer_num][_packet.output_deviceID] = true;
                        reqs++;
                    } else
                        conflict_sub++;
                } else {
                    if (issued_list[buffer_num][_packet.output_deviceID]) conflict_sub++;
                }
            }
        }
    }
    next_node_id = (++next_node_id % new_total_nodes);
}
//

bool xbar_router::Busy() const {
    for (unsigned i = 0; i < total_nodes; ++i) {
        if (!in_buffers[i].empty()) return true;

        if (!out_buffers[i].empty()) return true;
    }
    return false;
}
//
int xbar_router::get_dst_GPM(unsigned output_deviceID) {
    int GPM_shader = _n_shader / GPM_NUM;
    int GPM_mem = _n_mem / GPM_NUM;
    if (output_deviceID >= 0+GPM_shader*0 && output_deviceID < GPM_shader*0+GPM_shader ||
        output_deviceID >= _n_shader+GPM_mem*0+0 && output_deviceID < _n_shader+GPM_mem*0+GPM_mem) {
        return 0;
    }
    if (output_deviceID >= 0+GPM_shader*1 && output_deviceID < GPM_shader*1+GPM_shader ||
        output_deviceID >= _n_shader+GPM_mem*1+0 && output_deviceID < _n_shader+GPM_mem*1+GPM_mem) {
        return 1;
    }
    if (output_deviceID >= 0+GPM_shader*2 && output_deviceID < GPM_shader*2+GPM_shader ||
        output_deviceID >= _n_shader+GPM_mem*2+0 && output_deviceID < _n_shader+GPM_mem*2+GPM_mem) {
        return 2;
    }
    if (output_deviceID >= 0+GPM_shader*3 && output_deviceID < GPM_shader*3+GPM_shader ||
        output_deviceID >= _n_shader+GPM_mem*3+0 && output_deviceID < _n_shader+GPM_mem*3+GPM_mem) {
        return 3;
    }
}

int xbar_router::get_next_GPM(int Dst) { //2->last; 3->next;
    if (m_GPM_num == 0) {
        if (Dst == 0)  return 0;
        if (Dst == 1)  return 3;
        if (Dst == 2)  return 3;
        if (Dst == 3)  return 2;
    }
    else if (m_GPM_num == 1) {
        if (Dst == 0)  return 2;
        if (Dst == 1)  return 0;
        if (Dst == 2)  return 3;
        if (Dst == 3)  return 3;
    }
    else if (m_GPM_num == 2) {
        if (Dst == 0)  return 3;
        if (Dst == 1)  return 2;
        if (Dst == 2)  return 0;
        if (Dst == 3)  return 3;
    }
    else if (m_GPM_num == 3) {
        if (Dst == 0)  return 3;
        if (Dst == 1)  return 3;
        if (Dst == 2)  return 2;
        if (Dst == 3)  return 0;
    }
}
//

// assume all the packets are one flit
#define LOCAL_INCT_FLIT_SIZE 40

LocalInterconnect* LocalInterconnect::New(
        const struct inct_config& m_localinct_config) {
    auto* icnt_interface = new LocalInterconnect(m_localinct_config);

    return icnt_interface;
}

LocalInterconnect::LocalInterconnect(
        const struct inct_config& m_localinct_config)
        : m_inct_config(m_localinct_config) {
    n_shader = 0;
    n_mem = 0;
    n_subnets = m_localinct_config.subnets;
}

LocalInterconnect::~LocalInterconnect() {
    for (unsigned i = 0; i < m_inct_config.subnets; ++i) {
        delete net[i];
    }
}

void LocalInterconnect::CreateInterconnect(unsigned m_n_shader,
                                           unsigned m_n_mem) {
    n_shader = m_n_shader;
    n_mem = m_n_mem;

    net.resize(n_subnets);
    for (unsigned i = 0; i < n_subnets; ++i) {
        net[i] = new xbar_router(i, static_cast<Interconnect_type>(i), m_n_shader,
                                 m_n_mem, m_inct_config);
    }
}

void LocalInterconnect::Init() {
    // empty
    // there is nothing to do
}

void LocalInterconnect::Push(unsigned input_deviceID, unsigned output_deviceID,
                             void* data, unsigned int size) {
    unsigned subnet;
    if (n_subnets == 1) {
        subnet = 0;
    } else {
        if (input_deviceID < n_shader) {
            subnet = 0;
        } else {
            subnet = 1;
        }
    }
    net[subnet]->Push(input_deviceID, output_deviceID, data, size); // 仅一个了！
}

void* LocalInterconnect::Pop(unsigned ouput_deviceID) {
    // 0-_n_shader-1 indicates reply(network 1), otherwise request(network 0)
    int subnet = 0;
    if (ouput_deviceID < n_shader) subnet = 1;

    return net[subnet]->Pop(ouput_deviceID);
}

void LocalInterconnect::Advance() {
    for (unsigned i = 0; i < n_subnets; ++i) {
        net[i]->Advance();
    }
}

bool LocalInterconnect::Busy() const {
    for (unsigned i = 0; i < n_subnets; ++i) {
        if (net[i]->Busy()) return true;
    }
    return false;
}

bool LocalInterconnect::HasBuffer(unsigned deviceID, unsigned int size) const {
    bool has_buffer = false;
    if ((n_subnets > 1) && deviceID >= n_shader)  // deviceID is memory node
        has_buffer = net[REPLY_NET]->Has_Buffer_In(deviceID, 1);
    else
        has_buffer = net[REQ_NET]->Has_Buffer_In(deviceID, 1);
    return has_buffer;
}




