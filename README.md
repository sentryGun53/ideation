# ideation

main logic is in histogram.c



RUN:
    make
    ./h

ADJUST LEVELS:
    alsamixer -c 1
    sudo alsactl store

SPEAKER TEST:
    speaker-test -c2 -D plughw:1,0
