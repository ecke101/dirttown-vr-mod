#include "game_math.hpp"
#include "controller_mapping.hpp"
#include <cstdio>
int main() {
    try {dtvr::verifyCameraMath();dtvr::verifyControllerMapping();
        std::puts("PASS: eye projection/culling, view routing, controller mapping and input isolation");return 0;}
    catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
