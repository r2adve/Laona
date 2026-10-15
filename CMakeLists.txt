cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "v1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# main 이 필요로 하는 컴포넌트만 빌드
idf_build_set_property(MINIMAL_BUILD ON)
project(laona)
