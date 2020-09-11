#!/bin/bash

if [[ "x$SUFFIX" == "x" ]] ; then
    echo "suffix?"
    exit
fi

./sync

./bench.sh bloomfilter-$SUFFIX --workload=bloomfilter --param=bloomfilter_size:200000000
./analyze.py plot --dir=bench-results --tag=bloomfilter-$SUFFIX --workload=bloomfilter &

./bench.sh map-$SUFFIX --workload=map --param=map_size:100000
wait
./analyze.py plot --dir=bench-results --tag=map-$SUFFIX --workload=map &

# suffix=$SUFFIX ;  step=40000 ; for workload in readonly writeall ; do for num_pages in $(python -c "for i in range(0, 100001, 40000): print(i if i else 1, end=' ') ;"); do ./bench.sh $workload-$step-$suffix --workload=$workload --param=num_pages:$num_pages ; done ; done
suffix=$SUFFIX ;  step=1000; for workload in readonly writeall ; do for num_pages in $(python -c "for i in range(0, 5001, 1000): print(i if i else 1, end=' ') ;"); do ./bench.sh $workload-$suffix --workload=$workload --param=num_pages:$num_pages ; done ; done

wait
./analyze.py plot --dir=bench-results --tag=readonly-$SUFFIX --workload=readonly
./analyze.py plot --dir=bench-results --tag=writeall-$SUFFIX --workload=writeall
