#!/bin/sh

for file in data/*; do
    if [ "${file##*.}" != "c" ]; then
        if [ -f "$file" ]; then
            filename=$(basename "$file")
            echo -e "#include \"../include/resource.h\"\n" > $file.c
            xxd -i -c 8 -n $filename $file >> $file.c
            sed -i "s/unsigned/const unsigned/g" "$file.c"
        fi
    fi
done
