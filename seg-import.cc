// Author: Martin C. Frith 2016
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mcf_string_view.hh"

#include <getopt.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mcf;

struct SegImportOptions {
  bool isCds;
  bool is5utr;
  bool is3utr;
  bool isIntrons;
  bool isPrimaryTranscripts;
  const char *formatName;
  char **fileNames;
};

static void makeLowercase(std::string &s) {
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = s[i];
    s[i] = std::tolower(c);
  }
}

static void err(const std::string& s) {
  throw std::runtime_error(s);
}

static std::istream &openIn(const char *fileName, std::ifstream &ifs) {
  if (isChar(fileName, '-')) return std::cin;
  ifs.open(fileName);
  if (!ifs) err("can't open file: " + std::string(fileName));
  return ifs;
}

static bool isStrand(char c) {
  return c == '+' || c == '-';
}

static void importChain(std::istream &in) {
  StringView word, tName, tStrand, qName, qStrand;
  long tPos = 0;
  long qPos = 0;
  std::string line, chainLine;
  while (getline(in, line)) {
    StringView s(line);
    s >> word;
    if (!s || word[0] == '#') continue;
    if (word == "chain") {
      swap(line, chainLine);
      StringView t(chainLine);
      long tSize, qSize;
      t >> word >> word >> tName >> tSize >> tStrand >> tPos
	>> word >> qName >> qSize >> qStrand >> qPos;
      if (!t) err("bad CHAIN line: " + chainLine);
      if (tStrand == '-') tPos -= tSize;
      if (qStrand == '-') qPos -= qSize;
    } else {
      StringView t(line);
      long size, tInc, qInc;
      t >> size;
      if (!t) err("bad CHAIN line: " + line);
      std::cout << word << '\t' << tName << '\t' << tPos << '\t'
		<< qName << '\t' << qPos << '\n';
      if (t >> tInc >> qInc) {
	tPos += size + tInc;
	qPos += size + qInc;
      }
    }
  }
}

static void importGff(std::istream &in) {
  StringView seqname, junk, strand;
  std::string line;
  while (getline(in, line)) {
    StringView s(line);
    s >> seqname;
    if (!s || seqname[0] == '#') continue;
    long beg, end;
    s >> junk >> junk >> beg >> end >> junk >> strand;
    if (!s) err("bad GFF line: " + line);
    beg -= 1;  // convert from 1-based to 0-based coordinate
    long size = end - beg;
    if (strand == '-') beg = -end;
    std::cout << size << '\t' << seqname << '\t' << beg << '\n';
  }
}

static void importLastTab(std::istream &in, size_t &alnNum) {
  StringView junk, rName, rStrand, qName, qStrand, blocks;
  std::string line;
  while (getline(in, line)) {
    StringView s(line);
    s >> junk;
    if (!s || junk[0] == '#') continue;
    long rBeg, rSpan, rSeqLength, qBeg, qSpan, qSeqLength;
    s >> rName >> rBeg >> rSpan >> rStrand >> rSeqLength
      >> qName >> qBeg >> qSpan >> qStrand >> qSeqLength >> blocks;
    if (!s) err("bad lastTab line: " + line);
    if (rStrand == '-') rBeg -= rSeqLength;
    long rEnd = rBeg + rSpan;
    if (qStrand == '-') qBeg -= qSeqLength;
    long qEnd = qBeg + qSpan;
    long alnPos = 0;
    do {
      long x, y;
      blocks >> x;
      if (!blocks) err("bad lastTab line: " + line);
      char c = 0;
      blocks >> c;
      if (c == ':') {
	blocks >> y;
	if (!blocks) err("bad lastTab line: " + line);
	rBeg += x;
	qBeg += y;
	alnPos += x + y;
	blocks >> c;
      } else {
	std::cout << x << '\t'
		  << rName << '\t' << rBeg << '\t'
		  << qName << '\t' << qBeg << '\t'
		  << alnNum << '\t' << alnPos << '\n';
	rBeg += x;
	qBeg += x;
	alnPos += x;
      }
    } while (blocks);
    if (rBeg != rEnd || qBeg != qEnd)  // catches translated alignments
      err("failed on this line:\n" + line);
    ++alnNum;
  }
}

struct MafRow {
  std::string line;
  StringView name;
  long start;
  StringView seq;
  int letterLength;
};

static bool isGapless(const MafRow *rows, size_t numOfRows, size_t alnPos) {
  for (size_t i = 0; i < numOfRows; ++i) {
    if (rows[i].seq[alnPos] == '-') return false;
  }
  return true;
}

static bool isTranslatedMaf(StringView seq, size_t span) {
  size_t seqlen = seq.size();
  size_t gapCount = 0;
  for (size_t i = 0; i < seqlen; ++i) {
    if (seq[i] == '\\' || seq[i] == '/') return true;
    if (seq[i] == '-') ++gapCount;
  }
  return seqlen - gapCount < span;
}

static void printOneMafSegment(long length, MafRow *rows, size_t numOfRows,
			       size_t alnNum, size_t alnPos) {
  std::cout << length;
  for (size_t i = 0; i < numOfRows; ++i) {
    const MafRow &r = rows[i];
    std::cout << '\t' << r.name << '\t' << (r.start - length * r.letterLength);
  }
  std::cout << '\t' << alnNum << '\t' << (alnPos - length);
  std::cout << '\n';
}

static void doOneMaf(MafRow *rows, size_t numOfRows, size_t alnNum) {
  size_t alnLen = 0;
  StringView junk, strand;
  for (size_t i = 0; i < numOfRows; ++i) {
    MafRow &r = rows[i];
    StringView s(r.line);
    long span, seqLength;
    s >> junk >> r.name >> r.start >> span >> strand >> seqLength >> r.seq;
    if (!s) err("bad MAF line: " + r.line);
    size_t seqLen = r.seq.size();
    if (i == 0) alnLen = seqLen;
    else if (seqLen != alnLen) err("unequal alignment length:\n" + r.line);
    r.letterLength = isTranslatedMaf(r.seq, span) ? 3 : 1;
    if (strand == '-') r.start -= seqLength;
  }

  long length = 0;
  for (size_t alnPos = 0; alnPos < alnLen; ++alnPos) {
    if (isGapless(rows, numOfRows, alnPos)) {
      ++length;
    } else {
      if (length) printOneMafSegment(length, rows, numOfRows, alnNum, alnPos);
      length = 0;
    }
    for (size_t i = 0; i < numOfRows; ++i) {
      MafRow &r = rows[i];
      char symbol = r.seq[alnPos];
      /**/ if (symbol == '/' ) r.start -= 1;
      else if (symbol == '\\') r.start += 1;
      else if (symbol != '-' ) r.start += r.letterLength;
    }
  }
  if (length) printOneMafSegment(length, rows, numOfRows, alnNum, alnLen);
}

static void importMaf(std::istream &in, size_t &alnNum) {
  std::vector<MafRow> rows;
  size_t numOfRows = 0;
  std::string line;
  while (getline(in, line)) {
    const char *s = line.c_str();
    if (*s == 's') {
      ++numOfRows;
      if (rows.size() < numOfRows) rows.resize(numOfRows);
      MafRow &r = rows[numOfRows - 1];
      line.swap(r.line);
    } else if (!isGraph(*s)) {
      if (numOfRows) doOneMaf(&rows[0], numOfRows, alnNum++);
      numOfRows = 0;
    }
  }
  if (numOfRows) doOneMaf(&rows[0], numOfRows, alnNum++);
}

static void skipOne(StringView &s) {
  if (!s.empty()) s.remove_prefix(1);
}

static void importPsl(std::istream &in) {
  std::string line;
  StringView junk, strand, qName, tName, blockSizes, qStarts, tStarts;
  while (getline(in, line)) {
    StringView s(line);
    s >> junk;
    if (!s || !isDigit(junk)) continue;
    long qSize, tSize;
    for (int i = 0; i < 7; ++i) s >> junk;
    s >> strand >> qName >> qSize >> junk >> junk >> tName >> tSize
      >> junk >> junk >> junk >> blockSizes >> qStarts >> tStarts;
    if (!s) err("bad PSL line: " + line);
    char qStrand = strand[0];
    char tStrand = strand.size() > 1 ? strand[1] : '+';
    if (strand.size() > 2 || !isStrand(qStrand) || !isStrand(tStrand)) {
      err("unrecognized strand:\n" + line);
    }
    while(true) {
      long i, j, k;  // xxx needless string-to-num conversions
      blockSizes >> i;
      tStarts >> j;
      qStarts >> k;
      if (!blockSizes || !tStarts || !qStarts) break;
      if (tStrand == '-') j -= tSize;
      if (qStrand == '-') k -= qSize;
      std::cout << i << '\t'
		<< tName << '\t' << j << '\t' << qName << '\t' << k << '\n';
      skipOne(blockSizes);
      skipOne(tStarts);
      skipOne(qStarts);
    }
  }
}

struct ExonRange {
  long beg;
  long end;
};

static void printPrimaryTranscript(StringView chrom, StringView name,
				   bool isForwardStrand,
				   const std::vector<ExonRange> &exons) {
  long beg = exons.front().beg;
  long end = exons.back().end;
  long size = end - beg;
  long pos = isForwardStrand ? 0 : -size;
  std::cout << size << '\t'
	    << chrom << '\t' << beg << '\t' << name << '\t' << pos << '\n';
}

static void printIntrons(StringView chrom, StringView name,
			 bool isForwardStrand,
			 const std::vector<ExonRange> &exons) {
  long origin = isForwardStrand ? exons.front().beg : exons.back().end;
  for (size_t x = 1; x < exons.size(); ++x) {
    long i = exons[x - 1].end;
    long j = exons[x].beg;
    std::cout << (j - i) << '\t'
	      << chrom << '\t' << i << '\t'
	      << name << '\t' << (i - origin) << '\n';
  }
}

static void printExons(StringView chrom, StringView name, bool isForwardStrand,
		       const std::vector<ExonRange> &exons,
		       long printBeg, long printEnd) {
  long pos = 0;
  if (!isForwardStrand) {
    for (size_t i = 0; i < exons.size(); ++i) {
      const ExonRange &r = exons[i];
      pos -= r.end - r.beg;
    }
  }
  for(size_t i = 0; i < exons.size(); ++i) {
    const ExonRange &r = exons[i];
    long beg = std::max(r.beg, printBeg);
    long end = std::min(r.end, printEnd);
    if (beg < end) {
      std::cout << (end - beg) << '\t' << chrom << '\t' << beg << '\t'
		<< name << '\t' << (pos + beg - r.beg) << '\n';
    }
    pos += r.end - r.beg;
  }
}

static void getExons(StringView chrom, StringView name, bool isForwardStrand,
		     const std::vector<ExonRange> &exons,
		     long cdsBeg, long cdsEnd, const SegImportOptions &opts) {
  if (cdsBeg >= cdsEnd && (opts.is5utr || opts.is3utr)) return;
  bool isBegUtr = isForwardStrand ? opts.is5utr : opts.is3utr;
  bool isEndUtr = isForwardStrand ? opts.is3utr : opts.is5utr;
  long minBeg = exons.front().beg;
  long maxEnd = exons.back().end;
  if (opts.isCds) {
    if (isBegUtr && isEndUtr) {
      printExons(chrom, name, isForwardStrand, exons, minBeg, maxEnd);
    } else if (isBegUtr) {
      printExons(chrom, name, isForwardStrand, exons, minBeg, cdsEnd);
    } else if (isEndUtr) {
      printExons(chrom, name, isForwardStrand, exons, cdsBeg, maxEnd);
    } else {
      printExons(chrom, name, isForwardStrand, exons, cdsBeg, cdsEnd);
    }
  } else {
    if (isBegUtr && isEndUtr) {
      printExons(chrom, name, isForwardStrand, exons, minBeg, cdsBeg);
      printExons(chrom, name, isForwardStrand, exons, cdsEnd, maxEnd);
    } else if (isBegUtr) {
      printExons(chrom, name, isForwardStrand, exons, minBeg, cdsBeg);
    } else if (isEndUtr) {
      printExons(chrom, name, isForwardStrand, exons, cdsEnd, maxEnd);
    } else {
      printExons(chrom, name, isForwardStrand, exons, minBeg, maxEnd);
    }
  }
}

static void getGene(StringView chrom, StringView name, bool isForwardStrand,
		    const std::vector<ExonRange> &exons,
		    long cdsBeg, long cdsEnd, const SegImportOptions &opts) {
  if (opts.isPrimaryTranscripts)
    printPrimaryTranscript(chrom, name, isForwardStrand, exons);
  else if (opts.isIntrons)
    printIntrons(chrom, name, isForwardStrand, exons);
  else
    getExons(chrom, name, isForwardStrand, exons, cdsBeg, cdsEnd, opts);
}

static void importBed(std::istream &in, const SegImportOptions &opts) {
  StringView chrom, name, junk, strand, exonLens, exonBegs;
  std::vector<ExonRange> exons;
  std::string line;
  while (getline(in, line)) {
    StringView s(line);
    s >> chrom;
    if (!s) continue;  // xxx allow for "track" lines or "#" comments?
    long beg, end;
    s >> beg >> end;
    if (!s) err("bad BED line: " + line);
    s >> name;
    if (!s) {
      std::cout << (end - beg) << '\t' << chrom << '\t' << beg << '\n';
      continue;
    }
    s >> junk >> strand;
    bool isReverseStrand = (s && strand == '-');
    long cdsBeg = beg;
    long cdsEnd = beg;
    s >> cdsBeg >> cdsEnd >> junk >> junk >> exonLens >> exonBegs;
    if (s) {
      while (true) {
	long elen, ebeg;
	exonLens >> elen;
	exonBegs >> ebeg;
	if (!exonLens || !exonBegs) break;
	ExonRange r;
	r.beg = beg + ebeg;
	r.end = beg + ebeg + elen;
	exons.push_back(r);
	skipOne(exonLens);
	skipOne(exonBegs);
      }
    } else {
      ExonRange r;
      r.beg = beg;
      r.end = end;
      exons.push_back(r);
    }
    getGene(chrom, name, !isReverseStrand, exons, cdsBeg, cdsEnd, opts);
    exons.clear();
  }
}

static void importGenePred(std::istream &in, const SegImportOptions &opts) {
  StringView name, chrom, strand, junk, exonBegs, exonEnds;
  std::vector<ExonRange> exons;
  std::string line;
  while (getline(in, line)) {
    StringView s(line);
    s >> name;
    if (!s) continue;
    s >> chrom >> strand;
    if (strand != '+' && strand != '-') {
      name = chrom;
      chrom = strand;
      s >> strand;
    }
    long cdsBeg, cdsEnd;
    s >> junk >> junk >> cdsBeg >> cdsEnd >> junk >> exonBegs >> exonEnds;
    if (!s) err("bad genePred line: " + line);
    while (true) {
      ExonRange r;
      exonBegs >> r.beg;
      exonEnds >> r.end;
      if (!exonBegs || !exonEnds) break;
      exons.push_back(r);
      skipOne(exonBegs);
      skipOne(exonEnds);
    }
    getGene(chrom, name, strand == '+', exons, cdsBeg, cdsEnd, opts);
    exons.clear();
  }
}

struct Gtf {
  StringView name;
  StringView chrom;
  StringView strand;
  StringView feature;
  long beg;
  long end;

  bool operator<(const Gtf &right) const {
    int c = name.compare(right.name);
    if (c) return c < 0;
    c = chrom.compare(right.chrom);
    if (c) return c < 0;
    c = strand.compare(right.strand);
    if (c) return c < 0;
    return beg < right.beg;
  }
};

static StringView &readGtfTranscriptId(StringView &in, StringView &out) {
  StringView t, v;
  while (in >> t >> v) {
    if (t == "transcript_id") {
      if (v.back() == ';') v.remove_suffix(1);
      if (!v.empty() && v.front() == '"') v.remove_prefix(1);
      if (!v.empty() && v.back()  == '"') v.remove_suffix(1);
      out = v;
      break;
    }
  }
  return in;
}

static void importGtf(std::istream &in, const SegImportOptions &opts) {
  std::vector<std::string> lines;
  StringView junk;
  std::string line;
  while (getline(in, line)) {
    StringView s(line);
    s >> junk;
    if (!s || junk[0] == '#') continue;
    s >> junk >> junk;
    if (!s || junk == "exon" || junk == "start_codon" || junk == "stop_codon")
      lines.push_back(line);
  }
  size_t size = lines.size();
  std::vector<Gtf> records(size);
  for (size_t i = 0; i < size; ++i) {
    Gtf &r = records[i];
    StringView s(lines[i]);
    const char *end = std::find(s.begin(), s.end(), '#');
    s.remove_suffix(s.end() - end);
    s >> r.chrom >> junk >> r.feature >> r.beg >> r.end >> junk >> r.strand
      >> junk;
    if (!s) err("bad GTF line: " + lines[i]);
    readGtfTranscriptId(s, r.name);
    if (!s) err("missing transcript_id:\n" + lines[i]);
    --r.beg;
  }
  sort(records.begin(), records.end());
  std::vector<ExonRange> exons;
  long cdsBeg = 0;
  long cdsEnd = 0;
  for (size_t i = 0; i < size; ++i) {
    const Gtf &r = records[i];
    if (r.feature == "exon") {
      ExonRange e;
      e.beg = r.beg;
      e.end = r.end;
      exons.push_back(e);
    } else {
      if (cdsEnd == 0) cdsBeg = r.beg;
      cdsEnd = r.end;
    }
    size_t j = i + 1;
    if (j == size || r.name < records[j].name ||
	r.chrom < records[j].chrom || r.strand < records[j].strand) {
      getGene(r.chrom, r.name, r.strand == '+', exons, cdsBeg, cdsEnd, opts);
      exons.clear();
      cdsBeg = 0;
      cdsEnd = 0;
    }
  }
}

struct SegmentPair {
  long rStart;
  long qStart;
  long length;
};

static void addBlock(std::vector<SegmentPair> &blocks,
		     long rpos, long qpos, long length) {
  SegmentPair x;
  x.rStart = rpos;
  x.qStart = qpos;
  x.length = length;
  blocks.push_back(x);
}

static void parseCigar(std::vector<SegmentPair> &blocks, StringView &cigar,
		       long &rpos, long &qpos) {
  long length = 0;
  long size;
  char type;
  while (cigar >> size >> type) {
    switch (type) {
    case 'M': case '=': case 'X':
      length += size;
      break;
    case 'D': case 'N':
      if (length) addBlock(blocks, rpos, qpos, length);
      rpos += length + size;
      qpos += length;
      length = 0;
      break;
    case 'I': case 'S': case 'H':
      if (length) addBlock(blocks, rpos, qpos, length);
      rpos += length;
      qpos += length + size;
      length = 0;
      break;
    default:
      break;  // xxx ???
    }
  }
  if (length) addBlock(blocks, rpos, qpos, length);
  qpos += length;
  rpos += length;
}

static void importSam(std::istream &in) {
  StringView qname, rname, junk, cigar;
  std::vector<SegmentPair> blocks;
  std::string line;
  while (getline(in, line)) {
    StringView s(line);
    if (s[0] == '@') continue;
    s >> qname;
    if (!s) continue;
    unsigned flag;
    long rpos;
    s >> flag >> rname >> rpos >> junk >> cigar;
    if (!s) err("bad SAM line: " + line);
    if (flag & 4) continue;
    bool isReverseStrand = (flag & 16);
    const char *suffix = (flag & 64) ? "/1" : (flag & 128) ? "/2" : "";
    rpos -= 1;
    long qpos = 0;
    parseCigar(blocks, cigar, rpos, qpos);
    for (size_t i = 0; i < blocks.size(); ++i) {
      const SegmentPair &x = blocks[i];
      long qStartReal = isReverseStrand ? x.qStart - qpos : x.qStart;
      std::cout << x.length << '\t' << rname << '\t' << x.rStart << '\t'
		<< qname << suffix << '\t' << qStartReal << '\n';
    }
    blocks.clear();
  }
}

static void importRmsk(std::istream &in) {
  std::string line;
  StringView junk, qName, rName, rType, rType2;
  while (getline(in, line)) {
    StringView s(line);
    long beg, end;
    char strand;
    s >> junk >> junk >> junk >> junk >> qName >> beg >> end
      >> junk >> strand >> rName >> rType;
    if (s) {
      --beg;
    } else {
      StringView t(line);
      t >> junk >> junk >> junk >> junk >> junk >> qName >> beg >> end
	>> junk >> strand >> rName >> rType >> rType2;
      if (!t) continue;
    }
    std::cout << end - beg << '\t' << qName << '\t' << beg << '\t'
	      << rName << '#' << rType;
    if (!s && rType2 != rType) std::cout << '/' << rType2;
    std::cout << '\t' << (strand == '+' ? 0 : beg - end) << '\n';
  }
}

static void importOneFile(std::istream &in, const SegImportOptions &opts,
			  size_t &alnNum) {
  std::string n = opts.formatName;
  makeLowercase(n);
  if      (n == "bed") importBed(in, opts);
  else if (n == "chain") importChain(in);
  else if (n == "genepred") importGenePred(in, opts);
  else if (n == "gff") importGff(in);
  else if (n == "gtf") importGtf(in, opts);
  else if (n == "lasttab") importLastTab(in, alnNum);
  else if (n == "maf") importMaf(in, alnNum);
  else if (n == "psl") importPsl(in);
  else if (n == "rmsk") importRmsk(in);
  else if (n == "sam") importSam(in);
  else err("unknown format: " + std::string(opts.formatName));
}

static void segImport(const SegImportOptions &opts) {
  size_t alnNum = 0;  // xxx start from 0 or 1?
  if (*opts.fileNames) {
    for (char **i = opts.fileNames; *i; ++i) {
      std::ifstream ifs;
      std::istream &in = openIn(*i, ifs);
      importOneFile(in, opts, alnNum);
    }
  } else {
    importOneFile(std::cin, opts, alnNum);
  }
}

static void run(int argc, char **argv) {
  SegImportOptions opts;
  opts.isCds = false;
  opts.is5utr = false;
  opts.is3utr = false;
  opts.isIntrons = false;
  opts.isPrimaryTranscripts = false;

  std::string prog = argv[0];
  std::string help = "\
Usage:\n\
  " + prog + " [options] bed inputFile(s)\n\
  " + prog + " chain inputFile(s)\n\
  " + prog + " [options] genePred inputFile(s)\n\
  " + prog + " gff inputFile(s)\n\
  " + prog + " [options] gtf inputFile(s)\n\
  " + prog + " lastTab inputFile(s)\n\
  " + prog + " maf inputFile(s)\n\
  " + prog + " psl inputFile(s)\n\
  " + prog + " rmsk inputFile(s)\n\
  " + prog + " sam inputFile(s)\n\
\n\
Read segments or alignments in various formats, and write them in SEG format.\n\
\n\
Options:\n\
  -h, --help     show this help message and exit\n\
  -c             get CDS (coding regions)\n\
  -5             get 5' untranslated regions (UTRs)\n\
  -3             get 3' untranslated regions (UTRs)\n\
  -i             get introns\n\
  -p             get primary transcripts (exons plus introns)\n\
  -V, --version  show version number and exit\n\
";

  const char sOpts[] = "hc53ipV";

  static struct option lOpts[] = {
    { "help",    no_argument, 0, 'h' },
    { "version", no_argument, 0, 'V' },
    { 0, 0, 0, 0}
  };

  int c;
  while ((c = getopt_long(argc, argv, sOpts, lOpts, &c)) != -1) {
    switch (c) {
    case 'h':
      std::cout << help;
      return;
    case 'c':
      opts.isCds = true;
      break;
    case '5':
      opts.is5utr = true;
      break;
    case '3':
      opts.is3utr = true;
      break;
    case 'i':
      opts.isIntrons = true;
      break;
    case 'p':
      opts.isPrimaryTranscripts = true;
      break;
    case 'V':
      std::cout << "seg-import "
#include "version.hh"
        "\n";
      return;
    case '?':
      std::cerr << help;
      err("");
    }
  }

  if (opts.isIntrons)
    if(opts.isCds || opts.is5utr || opts.is3utr || opts.isPrimaryTranscripts)
      err("can't combine option -i or -p with any other option");
  if (opts.isPrimaryTranscripts)
    if(opts.isCds || opts.is5utr || opts.is3utr || opts.isIntrons)
      err("can't combine option -i or -p with any other option");

  if (optind > argc - 1) {
    std::cerr << help;
    err("");
  }

  opts.formatName = argv[optind++];
  opts.fileNames = argv + optind;

  std::ios_base::sync_with_stdio(false);  // makes it faster!

  segImport(opts);
}

int main(int argc, char **argv) {
  try {
    run(argc, argv);
    if (!std::cout.flush()) err("write error");
    return EXIT_SUCCESS;
  } catch (const std::exception &e) {
    const char *s = e.what();
    if (*s) std::cerr << argv[0] << ": " << s << '\n';
    return EXIT_FAILURE;
  }
}
