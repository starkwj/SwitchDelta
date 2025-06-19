#pragma once
#include <string>

namespace rdma {

const std::string from_tag = "FROM";
const std::string to_tag = "TO";

inline std::string setKey(int my_node_id, int remote_node_id, std::string info) {
    return from_tag + std::to_string(my_node_id)
        + to_tag + std::to_string(remote_node_id)
        + info;
}

inline std::string getKey(int my_node_id, int remote_node_id, std::string info) {
    return from_tag + std::to_string(remote_node_id) +
        to_tag + std::to_string(my_node_id)
        + info;
}

}