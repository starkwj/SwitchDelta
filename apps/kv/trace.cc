
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <algorithm>

#define FilePath "../third_party/cluster001"

std::map<std::string, int> M;
std::vector<int> timestamp;
std::vector<std::string> key;
std::vector<int> key_size;
std::vector<int> value_size;
std::vector<int> client_id;
std::vector<std::string> op;
std::vector<int> ttl;

const int kKeySize=1000;

typedef std::pair<std::string, int> trace_count_t;

bool cmp(trace_count_t a, trace_count_t b) {
    return a.second > b.second;
}

int main() {
    FILE* ret = freopen(FilePath, "r", stdin);
    if (ret == NULL) {
        puts("open fail");
        return 0;
    }

    M.clear();
    std::string s;
    int t_in;
    int count = 0;

    while (std::cin >> s) {
        std::stringstream ss(s);
        std::string s_in;

        

        for (int i = 0; i < 2 ;i++) {
            getline(ss, s_in, ',');

            if (i == 0) {
                t_in = stoi(s_in);
                timestamp.push_back(t_in);
            }

            if (i == 1 && t_in == 0) {
                M[s_in]++;
            }
        }

        if (t_in != 0) 
            break;
        count++;
    }
    
    std::vector<trace_count_t> m_vec(M.begin(), M.end());

    sort(m_vec.begin(), m_vec.end(), cmp);

    printf("count=%d\n", count);

    int old = dup(1);
    FILE* fd = freopen("out", "w", stdout);
    int cache = 0;


    for (uint i = 0; i < m_vec.size(); i++) {
        if (m_vec[i].second > 1) {
            cache += m_vec[i].second - 1;
        }
        printf("%s,%d,%lf\n", m_vec[i].first.c_str(), m_vec[i].second, m_vec[i].second * 1.0 / count);
    }
    fflush(fd);
    // fclose(fd);
    dup2(old, 1);
    
    printf("cache ratio = %lf\n", cache * 1.0 / count);

    return 0;
}

// 0,z44uy84y444444zkuMdF44i444444svfX484u84444CF44Cgv_-CyLli48d4y84444wIVo44,72,455,1,get,0