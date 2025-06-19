#pragma once

#include <libmemcached/memcached.h>
#include <string>
#include <iostream>
#include <cstring>
#include <cassert>
#include <lock.h>

class Memcached
{
private:
    // Memcached(/* args */);
    Memcached() {
        connectMemcached();
    }
    ~Memcached() {}
    SpinLock lock;
    SpinLock& get_lock() {
        return lock;
    }
    memcached_st* memc;
    inline void connectMemcached();

    /* data */
public:
    inline bool disconnectMemcached();
    static Memcached& get_instance() {
        static Memcached memcached;
        return memcached;
    }
    static void initMemcached(int server_id = -1) {
        if (server_id != 0) {
            sleep(1);
            return;
        }
        int old = dup(0);
        std::string memcached_ip;
        in_port_t port;

        FILE* fd = freopen("../scripts/memcached.conf", "r", stdin);
        std::cin >> memcached_ip >> port;
        std::string cmd1 = "ssh " + memcached_ip + " \"cat /tmp/memcached.pid | xargs kill\"";
        std::string cmd2 = "ssh " + memcached_ip + " \"memcached -u root -l " + memcached_ip + " -p " + std::to_string(port) + " -c 10000 -d -P /tmp/memcached.pid\"";

        std::cout << system(cmd1.c_str()) << " " << cmd1 << std::endl;
        std::cout << system(cmd2.c_str()) << " " << cmd2 << std::endl;

        std::cin.clear();
        fclose(fd);
        dup2(old, 0);

        sleep(1);
        return;
    }

    inline bool memcachedSet(std::string key, char* value, int value_size, int sleep_time = 1);
    inline bool memcachedGet(std::string key, char* value, int value_size, int sleep_time = 1);
};




void Memcached::connectMemcached()
{
    memcached_server_st* servers = NULL;
    memcached_return rc;
    std::string memcached_ip;
    in_port_t port;

    sleep(1);

    int old = dup(0);
    FILE* fd = freopen("../scripts/memcached.conf", "r", stdin);
    std::cin >> memcached_ip >> port;
    std::cout << memcached_ip << ":" << port << std::endl;
    std::cin.clear();
    fclose(fd);
    dup2(old, 0);

    // freopen("CON", "r", stdin);
    memc = memcached_create(NULL);
    servers = memcached_server_list_append(servers, memcached_ip.c_str(), port, &rc);
    rc = memcached_server_push(memc, servers);

    if (rc != MEMCACHED_SUCCESS) {
        fprintf(stderr, "!Counld't add server:%s\n", memcached_strerror(memc, rc));
        exit(0);
    }
    return;
}

inline bool Memcached::memcachedSet(std::string key, char* value, int value_size, int sleep_time) {

    while (true) {
        get_lock().lock();
        // printf("set %s %x\n", key.c_str(), memc);
        auto rc = memcached_set(memc, key.c_str(), key.size(), value, value_size, (time_t)0, (uint32_t)0);
        if (rc == MEMCACHED_SUCCESS) {
            get_lock().unlock();
            break;
        }
        // continue;
        fprintf(stderr, "Coul dn't register my(%s, %s) info: %s, retry...\n",
            key.c_str(), value,
            memcached_strerror(memc, rc));
        get_lock().unlock();
        usleep(rand() % 1000 * (sleep_time % 1000));
    }
    return true;
}

inline bool Memcached::memcachedGet(std::string key, char* value, int value_size, int sleep_time) {
    // printf("get %s %x\n", key.c_str(), memc);
    while (true) {
        get_lock().lock();
        // puts("???");
        memcached_return rc;
        size_t l;
        uint32_t flags;
        auto res = memcached_get(memc, key.c_str(), key.size(), &l, &flags, &rc);
        if (rc == MEMCACHED_SUCCESS) {
            
            if (l != (size_t)value_size) {
                assert(l == (uint32_t)value_size);
            }
            memcpy(value, res, value_size);
            get_lock().unlock();
            // free(res);
            break;
        }
        get_lock().unlock();
        usleep(rand() % 1000 * (sleep_time % 1000));
    }
    return true;
}

inline bool Memcached::disconnectMemcached()
{
    if (memc) {
        memcached_quit(memc);
        memcached_free(memc);
        memc = NULL;
    }
    return true;
}