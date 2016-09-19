#!/bin/bash

title() {
	sed '/^[ \t]*$/d' "$1" |
	sed '/^===/Q' |
	sed '/^---/Q' |
	sed 's/^# //;T;Q' |
	sed 's/^## //;T;Q' |
	sed '/^---/Q'
}

authors() {
	git log --numstat --format='A %aN' -- "$1" |
	awk '$1=="A"{sub(/^A /,"");a=$0; s[a]+=0; next}NF==3{s[a]+=($1+0)}END{for(a in s)print s[a]" : "a}' |
	sort -nr |
	sed 's/[^:]* : //' |
	sed '1!s/^/; /' |
	tr -d '\n'
}

dateof() {
	local file="$1"
	local t=$(git log -n 1 --format=%ct "$file")
	[[ -n "$t" ]] || t=$(stat -c %Y "$file")
	LANG= date -d @$t +"%d %B %Y"
}

meta() {
	local file="$1"
	local t=$(title "$file")
	local a=$(authors "$file")
	local d=$(dateof "$file")
	echo "% $t"
	echo "% $a"
	echo "% $d"
	cat "$file"
}


# make the html file for $1
mkhtml() {
  local x=$1
  local h=${x%%.md}.html
  echo updating $h from $x
  meta "$x" |
  pandoc --css doc.css -f markdown -t html5 --toc > "$h"
}

# apply
for x in *.md; do
  mkhtml $x
done

