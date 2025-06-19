#include <bits/stdc++.h>
#include <iostream>
#include <gflags/gflags.h>
#include <glog/logging.h>


namespace cmakeexample{

DEFINE_int32(first_gflags, 20220604, "The date of learning gflags");
DEFINE_bool(bool_gflags, true, "This is a bool_gflags");
struct CMakeExample{
    int gflags_bool;
    int gflags_int32;
};

}

extern cmakeexample::CMakeExample cmake_test;