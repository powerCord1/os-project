#!/bin/bash

for f in $(find . -name "*.o"); do
    rm $f
done

rm os.bin