#!/bin/bash
g++ tetris_drm.cpp -o tetris_drm -I/usr/include/libdrm -ldrm -pthread -std=c++17
