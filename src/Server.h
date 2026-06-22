#ifndef SERVER_H
#define SERVER_H

#include<vector>
struct Server{
    int id;
    int G,VG,C,R;
    int free_cpu;
    int free_mem;
    std::vector<int> gpu_free_mem;
};
#endif