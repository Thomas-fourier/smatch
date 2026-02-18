#!/bin/bash

SPEC_DIR=$(dirname $(dirname $0))/api_spec


grep "Possible wrapper found" $1 | sort | uniq | \
while IFS= read -r line; do
    line=$(echo "$line" | sed 's/^[a-zA-Z0-9\_\-\.\ \/]*:[0-9]* [a-zA-Z0-9\_]*() warn: Possible wrapper found \([a-zA-Z0-9\_]*\) \([a-zA-Z0-9\_\-\., ()\/=]*\)/\1:\2/')
    wrapped=$(echo $line | cut -d: -f1)
    wrapper=$(echo $line | cut -d: -f2)

    for file in $(find $SPEC_DIR -type f -exec grep -wl "$wrapped" \{\} \;); do 
        echo "Adding $wrapper to $file"
        grep -qw "$wrapper" "$file" || $(echo $wrapper >> $file)
    done

done
