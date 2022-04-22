#! /bin/sh

# Tests for seg-suite

try () {
    echo "# TEST" "$@"
    eval "$@"
    echo
}

d=$(dirname "$0")
cd "$d"

PATH=../bin:$PATH

{
    try seg-import -h

    try seg-import bed demo.bed
    try seg-import -i bed demo.bed
    try seg-import -i -f2 bed demo.bed

    try seg-import chain hg19-hg38-1k.chain
    try seg-import -f2 chain hg19-hg38-1k.chain

    try seg-import genePred hg19refGene.txt
    try seg-import -f2 genePred hg19refGene.txt
    try seg-import -c genepred hg19refGene.txt
    try seg-import -c -f2 genepred hg19refGene.txt
    try seg-import -i genepred hg19refGene.txt
    try seg-import -p genepred hg19refGene.txt
    try seg-import -p -f2 genepred hg19refGene.txt

    try seg-import gff sp.gtf
    try seg-import -f2 gff sp.gtf
    try seg-import -f1 gff genomic.gff

    try seg-import gtf sp.gtf
    try seg-import -c gtf sp.gtf
    try seg-import -i gtf sp.gtf
    try seg-import -5 -3 gtf sp.gtf
    try seg-import -5 -3 -f2 gtf sp.gtf
    try seg-import gtf bad.gtf

    try seg-import lasttab a-top.tab
    try seg-import -a lasttab a-top.tab
    try seg-import -a -f2 lasttab a-top.tab

    try seg-import maf a-top.maf
    try seg-import -a maf a-top.maf
    try seg-import -a -f2 maf a-top.maf
    try seg-import maf hg38Y-prot.maf

    try seg-import psl hg19-refSeqAli100.psl
    try seg-import -f2 psl hg19-refSeqAli100.psl
    try seg-import psl te.psl
    try seg-import -f1 psl te.psl

    try seg-import rmsk rmsk.out
    try seg-import -f2 rmsk rmsk.out
    try seg-import rmsk rmsk.txt

    try seg-import sam a-top.sam
    try seg-import -f2 sam a-top.sam

    try seg-join hg38Yrg.seg hg38Yaln3.seg
    try seg-join -c1 hg38Ycgi.seg hg38Yrg.seg
    try seg-join -c2 hg38Ycgi.seg hg38Yrg.seg
    try seg-join -v1 hg38Ycgi.seg hg38Yrg.seg
    try seg-join -v1 -c1 hg38Ycgi.seg hg38Yrg.seg
    try seg-join -v2 hg38Yaln3.seg hg38Ycgi.seg
    try seg-join -w hg38Yrg.seg hg38Yrg2.seg
    try seg-join -w -v2 hg38Yrg.seg hg38Yrg2.seg
    try seg-join -w -c1 -c2 hg38Yrg.seg hg38Yrg2.seg
    try seg-join -w -v2 -c2 hg38Yrg.seg hg38Yrg2.seg
    try seg-join -v1 cutqry.seg cutref.seg
    try seg-join -f1 hg38Ycgi.seg hg38Yrg.seg
    try seg-join -n30 hg38Yrg.seg hg38Ycgi.seg
    try seg-join -n1/3 hg38Yrg.seg hg38Ycgi.seg
    try seg-join -x10 hg38Yrg.seg hg38Ycgi.seg

    try seg-mask chrM.seg chrM.fa
    try seg-mask -c chrM.seg chrM.fa
    try seg-mask -xn chrM.seg chrM.fa

    try seg-seq chrM.seg chrM.fa

    try seg-swap hg38Yaln3.seg
    try seg-swap -n3 hg38Yaln3.seg
    try seg-swap -s hg38Yaln3.seg

    try "cut -f-3 hg38Yrg.seg | seg-merge"
} | diff -u seg-test.txt -
