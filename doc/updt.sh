#!/bin/sh

subst() {
  awk -v pat="$1" -v rep="$(sed 's:\\:\\\\:g' $2)" '{gsub(pat,rep);gsub(pat,"\\&");print}'
}

main='<html>
<head>
  <link rel="stylesheet" type="text/css" href="doc.css">
  <meta charset="UTF-8">
</head>
<body>
GENERATED-MARKDOWN-HERE
</body>
</html>'

for x in *.md; do
  t=$(git log -n 1 --format=%ct $x)
  [[ -n "$t" ]] || t=$(stat -c %Y $x)
  d=$(LANG= date -d @$t +"%d %B %Y")
  sed -i "s/^\(    Date: *\).*/\1$d/" $x
  h=${x%%.md}.html
  markdown -f toc,autolink $x > $h.toc.no
  markdown -Tf toc,autolink $x > $h.toc.yes
  head --bytes=-$(stat -c %s $h.toc.no) $h.toc.yes > $h.toc
  echo "$main" |
  subst GENERATED-MARKDOWN-HERE $h.toc.no |
  subst TABLE-OF-CONTENT-HERE $h.toc > $h
#  rm $h.toc*
done

