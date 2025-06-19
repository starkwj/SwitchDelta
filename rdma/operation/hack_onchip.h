#pragma once
// #include <stdint.h>
// #include <stddef.h>
#include <infiniband/verbs.h>

#ifndef ONCHIP_OK // ONCHIP is only for 4.x driver.
struct ibv_exp_dm{
    int nothing;
};
#endif

struct verbs_dm
{   
    // struct ibv_exp_dm dm;
    int x;
};

struct mlx5_dm
{
    struct verbs_dm verbs_dm;
    size_t length;
    void *mmap_va;
    void *start_va;
    uint64_t remote_va;
};