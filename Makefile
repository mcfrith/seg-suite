# ugh, got to keep these up to date:
progs = seg-import seg-join seg-merge seg-sort seg-swap

README.html: README.txt
	rst2html README.txt > $@

log:
	hg log --style changelog > ChangeLog.txt

distdir = seg-suite-`hg id -n`
dist: README.html log
	mkdir ${distdir}
	cp ${progs} Makefile *.txt *.html ${distdir}
	zip -qrm ${distdir} ${distdir}

prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
install:
	mkdir -p ${bindir}
	cp ${progs} ${bindir}
