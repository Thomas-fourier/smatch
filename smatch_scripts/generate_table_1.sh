#! /bin/bash

README=$(dirname $(dirname $(which $0)))/README.md

cat $README | grep "|" | grep -v -e "| Patch | Commit header | Interface | Wrong argument |" -e "--------" | cut -d"|" -f 4,5 | sort | uniq -c
