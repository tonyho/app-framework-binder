#!/bin/bash

# the HTML template
main='<html>
<head>
  <link rel="stylesheet" type="text/css" href="doc.css">
  <meta charset="UTF-8">
</head>
<body>
GENERATED-MARKDOWN-HERE
</body>
</html>'

# substitute the pattern $1 by the content of the file $2
subst() {
  awk -v pat="$1" -v rep="$(sed 's:\\:\\\\:g' $2)" '{gsub(pat,rep);gsub(pat,"\\&");print}'
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
  expand -i $x | sed 's:^        :    :' > $h.pre
  markdown -f toc,autolink $h.pre > $h.toc.no
  markdown -Tf toc,autolink $h.pre > $h.toc.yes
  head --bytes=-$(stat -c %s $h.toc.no) $h.toc.yes > $h.toc
  echo "$main" |
  subst GENERATED-MARKDOWN-HERE $h.toc.no |
  subst TABLE-OF-CONTENT-HERE $h.toc > $h
  rm $h.*
}

# apply
for x in *.md; do
  updadate $x
  mkhtml $x
done

