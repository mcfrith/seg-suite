# ugh, got to keep these up to date:
scripts = seg-merge seg-sort seg-swap
binaries = seg-import seg-join

CXX = g++
CXXFLAGS = -O3 -Wall -g

all: ${binaries}

seg-import: seg-import.cc mcf_string_view.hh version.hh
	${CXX} ${CPPFLAGS} ${CXXFLAGS} ${LDFLAGS} -o $@ seg-import.cc

seg-join: seg-join.cc version.hh
	${CXX} ${CPPFLAGS} ${CXXFLAGS} ${LDFLAGS} -o $@ seg-join.cc

VERSION = \"`git log --oneline | wc -l``git diff --quiet HEAD || echo +`\"
UNKNOWN = \"UNKNOWN\"

version.hh: FORCE
	if test -e .git ; \
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
	git2cl > ChangeLog.txt

distdir = seg-suite-`hg id -n`
dist: README.html log version.hh
	mkdir ${distdir}
	cp ${scripts} *.cc *.hh Makefile *.txt README.html *.css ${distdir}
	zip -qrm ${distdir} ${distdir}

prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
install: all
	mkdir -p ${bindir}
	cp ${scripts} ${binaries} ${bindir}
