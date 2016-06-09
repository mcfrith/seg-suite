seg-suite
=========

The seg suite provides tools for manipulating segments and alignments.
It uses a format called "seg".

Installation
------------

You need to have a C++ compiler. On Linux, you might need to install a
package called "g++". On Mac, you might need to install command-line
developer tools. On Windows, you might need to install Cygwin.

Using the command line, go into the seg-suite directory, and type::

  make

Optionally, you can copy the programs to a standard "bin" directory::

  sudo make install

Or copy them to your personal ~/bin directory::

  make install prefix=~

Description of seg format
-------------------------

The simplest version of seg format looks like this::

  7       chrX    12
  5       chrY    1761

Each line describes a segment.  The columns indicate: the segment
length, the name of the sequence containing the segment, and the start
coordinate.  The next-simplest version looks like this::

  7       chrM    123     chr1    862
  99      chrY    9988    myseq   0

Each line describes a segment-pair, i.e. a gapless alignment.  The
columns indicate: the length, first sequence name, first sequence
start, second sequence name, and second sequence start.  In general,
seg format describes segment-tuples.

Gapped alignments are described simply by listing their gapless parts,
one per line.

The start coordinates are zero-based, which means that a segment
starting right at the beginning of a sequence has coordinate zero.

Reverse strands are indicated by negative start coordinates.  For
example, the Os in this sketch show a reverse-strand segment::

          0 1 2 3 4 5 6 7 8 9
          | | | | | | | | | |
  (5'-end) x x x x x x x x x (3'-end)
  (3'-end) x O O O O O O x x (5'-end)

This segment would be described as follows::

  6       myseq   -7

Notice that the start coordinate always indicates the 5'-end of the
segment.

The columns may be separated by any run of whitespace, but the
seg-suite programs use single tabs in their output (except for
seg-sort, which keeps whatever was in the input).

seg-import
----------

This program converts segments or alignments from various formats to
seg.  To see what formats it can read, run it without any input.  To
perform a conversion, run it like this::

  seg-import maf myAlignments.maf > myAlignments.seg

Many of the input formats are described at
http://genome.ucsc.edu/FAQ/FAQformat.html.

Details:

* The format name (first argument to seg-import) is not case
  sensitive.

* For genePred format: versions of this format with or without an
  extra first column are both OK.  The extended version is also OK
  (and the extended information is not used).

These options are available, which affect genePred and gtf formats
only.  For these formats, the default is to get the exons.

-c  Get coding regions (CDS).  For gtf format, this includes start and
    stop codons.

-5  Get 5' untranslated regions (UTRs).

-3  Get 3' untranslated regions (UTRs).

-i  Get introns.

-p  Get primary transcripts (exons plus introns).

seg-join
--------

This program joins two files of segment-tuples, based on overlap in
the same first sequence.  For example, if you have two segment-pair
files, ab.seg and ac.seg, you can compose them to get
segment-triples::

  seg-join ab.seg ac.seg > abc.seg

If you have two segment files, x.seg and y.seg, this will find all
intersections between them::

  seg-join x.seg y.seg > intersections.seg

Both files must be in the order produced by seg-sort, else it will
complain.

The following options are available.

-c FILENUM  This option tells seg-join to only output joins that
            include whole segment-tuples from one of the input files.
            FILENUM should be either 1 or 2, indicating the first or
            second file.  For example, this will find all segments in
            x.seg that are wholly contained in any segment of y.seg::

              seg-join -c1 x.seg y.seg > inside.seg

            It is possible to specify both files, by using this option
            twice.

-v FILENUM  This option makes seg-join output unjoinable parts of one
            of the input files.  For example, this will get the parts
            of segments in x.seg that do not overlap any segment in
            y.seg::

              seg-join -v1 x.seg y.seg > difference.seg

            And this will find whole segments in x.seg that do not
            overlap anything in y.seg::

              seg-join -v1 -c1 x.seg y.seg > outside.seg

-w  This option makes it join based on identical coordinates in all
    sequences, not just the first sequence.  For example, this will
    find all intersections between segment-pairs in ab.seg and
    cd.seg::

      seg-join -w ab.seg cd.seg > ef.seg

seg-merge
---------

This program merges overlapping and touching segment-tuples.  It will
merge two segment-tuples only if all their start coordinates are
offset by the same amount.  The input must be in the order produced by
seg-sort, else it will complain.  Run it like this::

  seg-merge original.seg > merged.seg

seg-sort
--------

This program sorts segment-tuples, in ASCII-betical order of the first
sequence name, and then in numeric order of the first start
coordinate.  Use it like this::

  seg-sort original.seg > sorted.seg

It uses your system's sort utility, and you can pass options through
to it.  Here are some options that might be useful.

-c  Instead of sorting, check whether the input is sorted.

-m  Merge already-sorted files.

-S SIZE  Use a memory buffer of size SIZE.  For example, "-S 2G"
         indicates 2 gibibytes. You can possibly make large sorts
         faster by increasing the buffer.

seg-swap
--------

This program swaps the first two segments in each segment-tuple.  In
other words, it swaps columns 2-3 with columns 4-5.  Run it like
this::

  seg-swap original.seg > swapped.seg

After swapping, seg-swap canonicalizes strands.  In other words, if
the first segment in a tuple is reverse-stranded, it flips the strands
of all segments in that tuple.

These options may be used:

-n N  Swap the Nth segment with the first segment.

-s  Do not canonicalize strands.

Example: evaluating pairwise alignments
---------------------------------------

Suppose we have some true alignments in true.seg, and some predicted
alignments in pred.seg.  Each file has query sequences in columns 4-5
aligned to reference sequences in columns 2-3.  We wish to learn how
many queries are correctly aligned, in whole or part.  We can do that
as follows::

  seg-join -w true.seg pred.seg |
  cut -f4 |
  sort -u |
  wc -l

This command: (1) intersects the alignments, (2) cuts out the query
name, (3) sorts and merges identical names, and (4) counts them.

(In)completeness
----------------

The seg suite aims to be complete but elegantly minimal.  Right now
it's probably too minimal.

Miscellaneous
-------------

The seg suite is distributed under the GNU General Public License,
either version 3 of the License, or (at your option) any later
version.  For details, see COPYING.txt.
