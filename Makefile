# ugh, got to keep these up to date:
scripts = seg-import seg-merge seg-sort seg-swap
binaries = seg-join

CXX = g++
CXXFLAGS = -O3 -Wall -g

all: ${binaries}

seg-join: seg-join.cc version.hh
	${CXX} ${CPPFLAGS} ${CXXFLAGS} ${LDFLAGS} -o $@ seg-join.cc

VERSION = \"`hg id -n`\"
UNKNOWN = \"UNKNOWN\"

version.hh: FORCE
	if test -e .hg ; \
	then echo ${VERSION} | cmp -s $@ - || echo $(VERSION) > $@ ; \
	else test -e $@ || echo ${UNKNOWN} > $@ ; \
	fi

FORCE:

clean:
	rm -f ${binaries}

# Ugh!  Is there a better way?
RST_CSS = `locate html4css1.css | tail -n1`
README.html: README.txt
	rst2html --stylesheet=${RST_CSS},seg-suite.css README.txt > $@

log:
	hg log --style changelog > ChangeLog.txt

distdir = seg-suite-`hg id -n`
dist: README.html log version.hh
	mkdir ${distdir}
	cp ${scripts} *.cc *.hh Makefile *.txt README.html ${distdir}
	zip -qrm ${distdir} ${distdir}

prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
install: all
	mkdir -p ${bindir}
	cp ${scripts} ${binaries} ${bindir}
