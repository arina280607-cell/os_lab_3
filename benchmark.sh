#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

mkdir -p checks

printf 'size\tK\tthreads\trun\tseconds\n' > checks/timings.tsv

for n in 128 512 2000
do
    # Один прогревочный запуск для каждой конфигурации.
    for t in 1 2 4 8 12
    do
        ./lab3 -r "$n" -c "$n" -k 10 -t "$t" > /dev/null
    done

    for run in 1 2 3
    do
        for t in 1 2 4 8 12
        do
            output=$(./lab3 -r "$n" -c "$n" -k 10 -t "$t")
            elapsed=$(printf '%s\n' "$output" |
                awk '/^Elapsed:/ {print $2}')

            if [ -z "$elapsed" ]; then
                printf 'Unable to read elapsed time\n' >&2
                exit 1
            fi

            printf '%s\t%s\t%s\t%s\t%s\n' \
                "$n" 10 "$t" "$run" "$elapsed" |
                tee -a checks/timings.tsv
        done
    done
done
