
#include "cmake_example.h"
int main(int argc, char **argv)
{
    FLAGS_logtostderr = 1;
    gflags::SetUsageMessage("Usage ./gflag_example");
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);

    LOG_IF(INFO, cmakeexample::FLAGS_first_gflags >= 20220604) << "I am INFO! Gflags=" << cmakeexample::FLAGS_first_gflags;

    for (int i = 1; i <= 20; i++)
    {
        LOG_EVERY_N(INFO, 10) << "LOG_EVERY_N(INFO, 10) when i==" << i;
        LOG_IF_EVERY_N(INFO, (i % 20 == 0), 10) << "LOG_IF_EVERY_N(INFO, (i % 20), 10) i==" << i;
        LOG_FIRST_N(INFO, 5) << "LOG_FIRST_N(INFO, 5) i==" << i;
        CHECK_LE(i, 20) << "CHECK_LE(i,20)" << i;
    }

    LOG(INFO) << "My Happiness Is All:)\n";

#if (SUBDIRECTORY==1)
    LOG(INFO) << "add_subdirectory ok\n" << SUBDIRECTORY;
#else
    LOG(INFO) << "add_subdirectory not ok\n" << SUBDIRECTORY;
#endif

        return 0;
}