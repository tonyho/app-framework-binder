#!/bin/bash

meta() {
	awk '
		NR == 1 { t = $0; next }
		NR == 2 && $1 ~ "======" { next }
		NR == 2 { exit }
		$1 == "Date:" { d = $2; for(i = 3 ; i <= NF ; i++) d = d " " $i; next }
		$1 == "Author:" { a = $2; for(i = 3 ; i <= NF ; i++) a = a " " $i; next }
		$1 == "version" || $1 == "Version" {next}
		/^[ \t]*$/ { printf "%% %s\n%% %s\n%% %s\n", t, a, d; exit }
	' "$1"
}

# update the date field of file $1
updadate() {
  local x=$1
  local t=$(git log -n 1 --format=%ct $x)
  [[ -n "$t" ]] || t=$(stat -c %Y $x)
  local d=$(LANG= date -d @$t +"%d %B %Y")
  sed -i "s/^\(    Date: *\).*/\1$d/" $x
}

# make the html file for $1
mkhtml() {
  local x=$1
  local h=${x%%.md}.html
  { meta "$x"; sed 's/TABLE-OF-CONTENT-HERE//' "$x"; } |
  pandoc --css doc.css -f markdown -t html5 --toc > "$h"
}

# apply
for x in *.md; do
  updadate $x
  mkhtml $x
done

