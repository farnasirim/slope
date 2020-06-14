#!/bin/bash
memcached-tool localhost:11211 dump 1>/tmp/fl-memc 2>/dev/null
./show.py /tmp/fl-memc
