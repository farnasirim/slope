#!/bin/bash
./show.py <(memcached-tool localhost:11211 dump)
