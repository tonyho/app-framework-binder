#!/bin/bash

files="
locales/fr/titi
locales/fr-FR/titi
locales/fr-BE/titi
locales/fr-CA/titi
locales/en/titi
locales/en-GB/titi
locales/en-US/titi
locales/zh-hans-cn/a.gif
locales/zh-hans-cn/f.gif
locales/zh-hans/a.gif
locales/zh-hans/b.gif
locales/zh/a.gif
locales/zh/b.gif
locales/zh/c.gif
a.gif
b.gif
c.gif
d.gif
index.html
config.xml
"


testdir=testdir$$

rm -rf $testdir
for f in $files
do
	mkdir -p $testdir/$(dirname $f)
	echo $f > $testdir/$f
done

cc -o $testdir/lr ../locale-root.c -D TEST_locale_root -g
valgrind $testdir/lr @$testdir +zh-hans-cn a.gif b.gif c.gif d.gif +en-US a.gif b.gif c.gif d.gif +zh-hans-cn a.gif b.gif c.gif d.gif


rm -rf $testdir

