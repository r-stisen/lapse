# CMake generated Testfile for 
# Source directory: /home/cowmuncher/Projects/LAPSE/lapse
# Build directory: /home/cowmuncher/Projects/LAPSE/lapse/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("formats" "/home/cowmuncher/Projects/LAPSE/lapse/build/lapse" "--formats")
set_tests_properties("formats" PROPERTIES  _BACKTRACE_TRIPLES "/home/cowmuncher/Projects/LAPSE/lapse/CMakeLists.txt;300;add_test;/home/cowmuncher/Projects/LAPSE/lapse/CMakeLists.txt;0;")
add_test("smoke" "/usr/bin/bash" "/home/cowmuncher/Projects/LAPSE/lapse/.github/scripts/smoke-engine.sh" "/home/cowmuncher/Projects/LAPSE/lapse/build/lapse")
set_tests_properties("smoke" PROPERTIES  _BACKTRACE_TRIPLES "/home/cowmuncher/Projects/LAPSE/lapse/CMakeLists.txt;301;add_test;/home/cowmuncher/Projects/LAPSE/lapse/CMakeLists.txt;0;")
