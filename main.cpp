#include <iostream>
#include <pthread.h>
#include "gpu.h"

#include <chrono>

typedef std::chrono::high_resolution_clock Clock;


void *sim_gpu(void *thread_arg);

int main() {
    pthread_t threads[1];
    int kernel_id = 1;

//    printf("Please input kernel id:");
//    scanf("%d", &kernel_id);
    auto t1 = Clock::now();

    pthread_create(&threads[0], nullptr,
                   sim_gpu, (void *) &(kernel_id));


    pthread_exit(nullptr);
    auto t2 = Clock::now();
    std::chrono::nanoseconds t21 = t2 - t1;
    std::cout << std::chrono::duration_cast<std::chrono::microseconds>(t21).count() << std:: endl;

}

void *sim_gpu(void *thread_arg) {
    int kernel_id = *((int *) thread_arg);
//    std::string benchmark = "b+tree";
    std::string benchmark = "vectorAdd";
//    std::string benchmark = "dwt2d";
    std::string trace_path = "../benchmarks/" + benchmark + "/traces";

    std::ifstream file_sass(trace_path + "/kernel-" + std::to_string(kernel_id) + ".sass");
    std::ifstream file_mem(trace_path + "/kernel-" + std::to_string(kernel_id) + ".mem");
    std::ifstream file_allsass(trace_path + "/kernel-" + std::to_string(kernel_id) + ".allsass");
//    if(!file_mem.is_open() || !file_sass.is_open() || !file_allsass.is_open()){
//        printf("trace missing error\n");
//        pthread_exit(nullptr);
//    }
    gpu m_gpu = gpu(benchmark);
    std::ifstream file(trace_path + "/kernel-" + std::to_string(kernel_id) + "-block-0.mem");
    if (!file.is_open()) {
        m_gpu.traceReader->gen_block_mem_trace(trace_path, kernel_id);
    }
    m_gpu.launch_kernel(kernel_id);
    m_gpu.execute_kernel(kernel_id);
    m_gpu.gpu_cycle();
    pthread_exit(nullptr);
}
