export memcached_address=10.70.20.185

if [[ $(hostname) == colonelthink ]]; then
    memcached_address=127.0.0.1
fi

if [[ $ID == 0 ]]; then
    killall -9 slope
    killall -9 memcached
    memcached -l 0.0.0.0 &
fi

export peers="--peer=0 --peer=1"

sleep 1

if [[ x$DEBUG == x ]] ; then
    ./slope --self=$ID $SLOPE_CMDLINE_OPTIONS --memcached_confstr=--SERVER=$memcached_address $peers
else
    gdb --args ./slope --self=$ID $SLOPE_CMDLINE_OPTIONS --memcached_confstr=--SERVER=$memcached_address $peers
fi
