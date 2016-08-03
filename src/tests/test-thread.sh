#!/bin/sh

cc test-thread.c ../afb-thread.c ../verbose.c ../afb-sig-handler.c -o test-thread -lrt -lpthread -I../../include
./test-thread
