binaries = bin/seg-import bin/seg-join

CXXFLAGS = -O3 -Wall -g

all: ${binaries}

bin/seg-import: seg-import.cc mcf_string_view.hh version.hh
	${CXX} ${CPPFLAGS} ${CXXFLAGS} ${LDFLAGS} -o $@ seg-import.cc

bin/seg-join: seg-join.cc version.hh
	${CXX} ${CPPFLAGS} ${CXXFLAGS} ${LDFLAGS} -o $@ seg-join.cc

# zero-based version number:
# use "grep -c ." because "wc -l" sometimes writes extra spaces
tag:
	git tag -m "" `git rev-list HEAD^ | grep -c .`

VERSION1 = git describe --dirty
VERSION2 = echo '$Format:%d$ ' | sed -e 's/.*tag: *//' -e 's/[,) ].*//'

VERSION = \"`test -e .git && ${VERSION1} || ${VERSION2}`\"

version.hh: FORCE
	echo ${VERSION} | cmp -s $@ - || echo ${VERSION} > $@

FORCE:

clean:
	rm -f ${binaries}

prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
install: all
	mkdir -p ${bindir}
	cp bin/*[!~] ${bindir}
