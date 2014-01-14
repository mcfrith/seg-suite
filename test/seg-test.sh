#! /bin/sh

# Tests for seg-suite

try () {
    echo "# TEST" $@
    $@
    echo
}

cd $(dirname $0)

PATH=..:$PATH

{
    try seg-import -h

    try seg-import genePred hg19refGene.txt
    try seg-import -c genepred hg19refGene.txt
    try seg-import -i genepred hg19refGene.txt

    try seg-import gtf sp.gtf
    try seg-import -c gtf sp.gtf
    try seg-import -i gtf sp.gtf
} |
diff -u seg-test.txt -
