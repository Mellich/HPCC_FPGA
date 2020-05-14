//
// Created by Marius Meyer on 04.12.19.
//

#include "stream_functionality.hpp"

/**
The program entry point
*/
int
main(int argc, char *argv[]) {
    // Setup benchmark
    auto bm = StreamBenchmark(argc, argv);
    bool success = bm.executeBenchmark();
    if (success) {
        return 0;
    }
    else {
        return 1;
    }
}

