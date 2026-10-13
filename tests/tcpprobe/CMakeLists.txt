cmake_minimum_required(VERSION 3.20)
project(tcpprobe C)
include(JornadaCE)

jornada_add_executable(tcpprobe tcpprobe.c)
target_link_libraries(tcpprobe PRIVATE jornada::winsock)
