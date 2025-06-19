#include "fpga_def.h"
#include "perf.h"
#include <sys/mman.h>
#include <algorithm>
#include <gflags/gflags.h>

DEFINE_int32(test_size, 4096, "FPGA MMIO test size");
DEFINE_bool(offset4k, true, "4k offset");

void read_write_lat(int size, int is_write)
{
    assert(size <= 4096);
    perftool::Timer timer;
    timer.begin();

    int fd_ = open((std::string(FPGABUF) + "0").c_str(), O_RDWR);
    char* src = (char*)mmap((void*)0x400000000, FPGASIZE, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_FIXED, fd_, 0);
    char* dest = (char*)malloc(FPGASIZE);
    char* read_buf = NULL;

    if (is_write) {
        std::swap(src, dest);
        if (is_write == 2)
            read_buf = (char*)malloc(FPGASIZE);
    }

    int test_count = FPGASIZE / (4 * KB) * 1024;
    timer.begin();
    for (int i = 0; i < test_count; i++) {
        int offset;
        if (FLAGS_offset4k)
            offset = (i * 4 * KB) % FPGASIZE;
        else
            offset = (i * size) % FPGASIZE;

        memcpy(dest + offset, src + offset, size);
        if (is_write == 2) {
            memcpy(read_buf, dest + offset, size);
        }
    }

    uint64_t ans = timer.end();
    printf("%s%s %d bytes: lat = %f (us) bw = %f (MB/s)\n", is_write>=1 ? "write" : "read", is_write==2 ? "(+r)" : " ", size,
        ans / 1000.0 / test_count,
        (test_count * size) / (ans / (1e9 / 1024 / 1024)));

    munmap(src, FPGASIZE);
    munmap(dest, FPGASIZE);
    if (is_write) {
        munmap(read_buf, FPGASIZE);
    }

    close(fd_);

    return;
}

int main(int argc, char* argv[]) {

    gflags::SetUsageMessage("Usage ./fpga_rw --help");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    read_write_lat(FLAGS_test_size, false);
    read_write_lat(FLAGS_test_size, true);
    read_write_lat(FLAGS_test_size, 2);

    return 0;
}