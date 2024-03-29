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
  unsigned forwardSegNum;
  bool isAddAlignmentNum;
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

static bool isGraphOrSpace(char c) {
  return c >= ' ';
}

static StringView &getWordWithSpaces(StringView &in, StringView &out) {
  const char *b = in.begin();
  const char *e = in.end();
  while (true) {
    if (b == e) return in = StringView();
    if (isGraphOrSpace(*b)) break;
    ++b;
  }
  const char *m = b;
  do { ++m; } while (m < e && isGraphOrSpace(*m));
  out = StringView(b, m);
  return in = StringView(m, e);
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

static void importChain(std::istream &in, const SegImportOptions &opts) {
  StringView word, tName, tStrand, qName, qStrand;
  long tPos = 0;
  long qPos = 0;
  bool isFlip = false;
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
      isFlip = ((opts.forwardSegNum == 1 && tPos < 0) ||
		(opts.forwardSegNum == 2 && qPos < 0));
    } else {
      StringView t(line);
      long size, tInc, qInc;
      t >> size;
      if (!t) err("bad CHAIN line: " + line);
      long tBeg = isFlip ? -(tPos + size) : tPos;
      long qBeg = isFlip ? -(qPos + size) : qPos;
      std::cout << word << '\t' << tName << '\t' << tBeg << '\t'
		<< qName << '\t' << qBeg << '\n';
      if (t >> tInc >> qInc) {
	tPos += size + tInc;
	qPos += size + qInc;
      }
    }
  }
}

static void importGff(std::istream &in, const SegImportOptions &opts) {
  StringView seqname, junk, strand;
  std::string line;
  while (getline(in, line)) {
    StringView s(line);
    s >> seqname;
    if (!s || seqname[0] == '#') continue;
    getWordWithSpaces(s, junk);
    getWordWithSpaces(s, junk);
    long beg, end;
    s >> beg >> end >> junk >> strand;
    if (!s) err("bad GFF line: " + line);
    beg -= 1;  // convert from 1-based to 0-based coordinate
    long size = end - beg;
    if (strand == '-' && opts.forwardSegNum != 1) beg = -end;
    std::cout << size << '\t' << seqname << '\t' << beg << '\n';
  }
}

static void importLastTab(std::istream &in, const SegImportOptions &opts,
			  size_t &alnNum) {
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
    bool isFlip = ((opts.forwardSegNum == 1 && rBeg < 0) ||
		   (opts.forwardSegNum == 2 && qBeg < 0));
    ++alnNum;
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
	long rOut = isFlip ? -(rBeg + x) : rBeg;
	long qOut = isFlip ? -(qBeg + x) : qBeg;
	std::cout << x << '\t'
		  << rName << '\t' << rOut << '\t' << qName << '\t' << qOut;
	if (opts.isAddAlignmentNum) {
	  long alnOut = isFlip ? -(alnPos + x) : alnPos;
	  alnPos += x;
	  std::cout << '\t' << alnNum << '\t' << alnOut;
	}
	std::cout << '\n';
	rBeg += x;
	qBeg += x;
      }
    } while (blocks);
    if (rBeg != rEnd || qBeg != qEnd)  // catches translated alignments
      err("failed on this line:\n" + line);
  }
}

struct MafRow {
  std::string line;
  StringView name;
  long start;
  StringView seq;
  int letterLength;
  int lengthPerLetter;
};

static bool isGapless(const MafRow *rows, size_t numOfRows, size_t alnPos) {
  for (size_t i = 0; i < numOfRows; ++i) {
    if (rows[i].seq[alnPos] == '-') return false;
  }
  return true;
}

static size_t numOfAlignedLetters(StringView seq) {
  size_t seqlen = seq.size();
  size_t gapCount = 0;
  for (size_t i = 0; i < seqlen; ++i) {
    if (seq[i] == '\\' || seq[i] == '/') return 0;
    if (seq[i] == '-') ++gapCount;
  }
  return seqlen - gapCount;
}

static void printOneMafSegment(const SegImportOptions &opts, long length,
			       int lenDiv, MafRow *rows, size_t numOfRows,
			       size_t alnNum, long alnPos, bool isFlip) {
  std::cout << (length / lenDiv);
  for (size_t i = 0; i < numOfRows; ++i) {
    const MafRow &r = rows[i];
    long beg = isFlip ? -r.start : r.start - length * r.letterLength;
    std::cout << '\t' << r.name << '\t' << (beg / r.lengthPerLetter);
  }
  if (opts.isAddAlignmentNum) {
    long beg = isFlip ? -alnPos : alnPos - length;  // xxx ???
    std::cout << '\t' << alnNum << '\t' << beg;
  }
  std::cout << '\n';
}

static void doOneMaf(const SegImportOptions &opts,
		     MafRow *rows, size_t numOfRows, size_t alnNum) {
  size_t alnLen = 0;
  int lenDiv = 1;
  bool isFlip = false;
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
    if (strand == '-') {
      r.start -= seqLength;
      if (opts.forwardSegNum == i + 1) isFlip = true;
    }
    size_t letterCount = numOfAlignedLetters(r.seq);
    r.letterLength = 1;
    r.lengthPerLetter = 1;
    if (letterCount < span) r.letterLength = 3;
    if (letterCount > span) {
      r.lengthPerLetter = 3;
      r.start *= 3;  // protein -> DNA coordinate
      lenDiv = 3;
    }
  }

  long len = 0;
  for (size_t alnPos = 0; alnPos < alnLen; ++alnPos) {
    if (isGapless(rows, numOfRows, alnPos)) {
      ++len;
    } else if (len) {
      printOneMafSegment(opts, len, lenDiv, rows, numOfRows, alnNum, alnPos,
			 isFlip);
      len = 0;
    }
    for (size_t i = 0; i < numOfRows; ++i) {
      MafRow &r = rows[i];
      char symbol = r.seq[alnPos];
      /**/ if (symbol == '/' ) r.start -= 1;
      else if (symbol == '\\') r.start += 1;
      else if (symbol != '-' ) r.start += r.letterLength;
    }
  }
  if (len) {
    printOneMafSegment(opts, len, lenDiv, rows, numOfRows, alnNum, alnLen,
		       isFlip);
  }
}

static void importMaf(std::istream &in, const SegImportOptions &opts,
		      size_t &alnNum) {
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
      if (numOfRows) doOneMaf(opts, &rows[0], numOfRows, ++alnNum);
      numOfRows = 0;
    }
  }
  if (numOfRows) doOneMaf(opts, &rows[0], numOfRows, ++alnNum);
}

static void skipOne(StringView &s) {
  if (!s.empty()) s.remove_prefix(1);
}

static long lastNumber(StringView commaSeparatedNumbers) {
  const char *b = commaSeparatedNumbers.begin();
  const char *e = commaSeparatedNumbers.end();
  if (!isDigit(e[-1])) --e;
  const char *m = e;
  while (m > b && isDigit(m[-1])) --m;
  StringView s(m, e);
  long n = 0;
  s >> n;
  return n;
}

static void importPsl(std::istream &in, const SegImportOptions &opts,
		      size_t &alnNum) {
  std::string line;
  StringView junk, strand, qName, tName, blockSizes, qStarts, tStarts;
  while (getline(in, line)) {
    StringView s(line);
    s >> junk;
    if (!s || !isDigit(junk)) continue;
    long qSize, qStart, qEnd, tSize, tStart, tEnd;
    for (int i = 0; i < 7; ++i) s >> junk;
    s >> strand >> qName >> qSize >> qStart >> qEnd >> tName >> tSize
      >> tStart >> tEnd >> junk >> blockSizes >> qStarts >> tStarts;
    if (!s) err("bad PSL line: " + line);
    char qStrand = strand[0];
    char tStrand = strand.size() > 1 ? strand[1] : '+';
    if (strand.size() > 2 || !isStrand(qStrand) || !isStrand(tStrand)) {
      err("unrecognized strand:\n" + line);
    }
    bool isFlip = ((opts.forwardSegNum == 1 && tStrand == '-') ||
		   (opts.forwardSegNum == 2 && qStrand == '-'));
    long tRealEnd = (tStrand == '-') ? tSize - tStart : tEnd;
    long qRealEnd = (qStrand == '-') ? qSize - qStart : qEnd;
    long blockSizeLast = lastNumber(blockSizes);
    if (blockSizeLast < 1) err("bad PSL line: " + line);
    long tLenMul = (tRealEnd - lastNumber(tStarts)) / blockSizeLast;
    long qLenMul = (qRealEnd - lastNumber(qStarts)) / blockSizeLast;
    ++alnNum;
    long alnPos = 0;
    long len, tBeg, qBeg;
    while(blockSizes >> len && tStarts >> tBeg && qStarts >> qBeg) {
      if (tStrand == '-') tBeg -= tSize;
      if (qStrand == '-') qBeg -= qSize;
      if (alnPos) alnPos += (tBeg - tEnd) + (qBeg - qEnd);
      tEnd = tBeg + len * tLenMul;
      qEnd = qBeg + len * qLenMul;
      if (isFlip) {
	tBeg = -tEnd;
	qBeg = -qEnd;
      }
      std::cout << len << '\t'
		<< tName << '\t' << tBeg << '\t' << qName << '\t' << qBeg;
      if (opts.isAddAlignmentNum) {
	long alnBeg = isFlip ? -(alnPos + len) : alnPos;
	alnPos += len;
	std::cout << '\t' << alnNum << '\t' << alnBeg;
      }
      std::cout << '\n';
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
				   unsigned isRevStrands,
				   const std::vector<ExonRange> &exons) {
  long beg = exons.front().beg;
  long end = exons.back().end;
  long size = end - beg;
  long a = (isRevStrands == 2) ? -end : beg;
  long b = (isRevStrands == 1) ? -size : 0;
  std::cout << size << '\t'
	    << chrom << '\t' << a << '\t' << name << '\t' << b << '\n';
}

static void printIntrons(StringView chrom, StringView name,
			 unsigned isRevStrands,
			 const std::vector<ExonRange> &exons) {
  long origin = (isRevStrands < 1) ? exons.front().beg : exons.back().end;
  for (size_t x = 1; x < exons.size(); ++x) {
    long i = exons[x - 1].end;
    long j = exons[x].beg;
    long a = (isRevStrands < 2) ? i : -j;
    long b = (isRevStrands < 2) ? i - origin : origin - j;
    std::cout << (j - i) << '\t'
	      << chrom << '\t' << a << '\t' << name << '\t' << b << '\n';
  }
}

static void printExons(StringView chrom, StringView name,
		       unsigned isRevStrands,
		       const std::vector<ExonRange> &exons,
		       long printBeg, long printEnd) {
  long pos = 0;
  if (isRevStrands > 0) {
    for (size_t i = 0; i < exons.size(); ++i) {
      const ExonRange &r = exons[i];
      pos -= r.end - r.beg;
    }
  }
  for (size_t i = 0; i < exons.size(); ++i) {
    const ExonRange &r = exons[i];
    long beg = std::max(r.beg, printBeg);
    long end = std::min(r.end, printEnd);
    if (beg < end) {
      long a = (isRevStrands < 2) ? beg : -end;
      long b = (isRevStrands < 2) ? pos + beg - r.beg : r.beg - end - pos;
      std::cout << (end - beg) << '\t'
		<< chrom << '\t' << a << '\t' << name << '\t' << b << '\n';
    }
    pos += r.end - r.beg;
  }
}

static void getExons(StringView chrom, StringView name, unsigned isRevStrands,
		     const std::vector<ExonRange> &exons,
		     long cdsBeg, long cdsEnd, const SegImportOptions &opts) {
  if (cdsBeg >= cdsEnd && (opts.is5utr || opts.is3utr)) return;
  bool isBegUtr = (isRevStrands < 1) ? opts.is5utr : opts.is3utr;
  bool isEndUtr = (isRevStrands < 1) ? opts.is3utr : opts.is5utr;
  long minBeg = exons.front().beg;
  long maxEnd = exons.back().end;
  if (opts.isCds) {
    if (isBegUtr && isEndUtr) {
      printExons(chrom, name, isRevStrands, exons, minBeg, maxEnd);
    } else if (isBegUtr) {
      printExons(chrom, name, isRevStrands, exons, minBeg, cdsEnd);
    } else if (isEndUtr) {
      printExons(chrom, name, isRevStrands, exons, cdsBeg, maxEnd);
    } else {
      printExons(chrom, name, isRevStrands, exons, cdsBeg, cdsEnd);
    }
  } else {
    if (isBegUtr && isEndUtr) {
      printExons(chrom, name, isRevStrands, exons, minBeg, cdsBeg);
      printExons(chrom, name, isRevStrands, exons, cdsEnd, maxEnd);
    } else if (isBegUtr) {
      printExons(chrom, name, isRevStrands, exons, minBeg, cdsBeg);
    } else if (isEndUtr) {
      printExons(chrom, name, isRevStrands, exons, cdsEnd, maxEnd);
    } else {
      printExons(chrom, name, isRevStrands, exons, minBeg, maxEnd);
    }
  }
}

static void getGene(StringView chrom, StringView name, bool isForwardStrand,
		    const std::vector<ExonRange> &exons,
		    long cdsBeg, long cdsEnd, const SegImportOptions &opts) {
  unsigned isRevStrands =
    isForwardStrand ? 0 : (opts.forwardSegNum == 2) ? 2 : 1;
  if (opts.isPrimaryTranscripts)
    printPrimaryTranscript(chrom, name, isRevStrands, exons);
  else if (opts.isIntrons)
    printIntrons(chrom, name, isRevStrands, exons);
  else
    getExons(chrom, name, isRevStrands, exons, cdsBeg, cdsEnd, opts);
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

static void importSam(std::istream &in, const SegImportOptions &opts) {
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
      long qBeg = x.qStart;
      long rBeg = x.rStart;
      if (isReverseStrand) {
	qBeg -= qpos;
	if (opts.forwardSegNum == 2) {
	  qBeg = -(qBeg + x.length);
	  rBeg = -(rBeg + x.length);
	}
      }
      std::cout << x.length << '\t' << rname << '\t' << rBeg << '\t'
		<< qname << suffix << '\t' << qBeg << '\n';
    }
    blocks.clear();
  }
}

static void importRmsk(std::istream &in, const SegImportOptions &opts) {
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
    long len = end - beg;
    long x = (strand == '+' || opts.forwardSegNum != 2) ? beg : -end;
    long y = (strand == '+' || opts.forwardSegNum == 2) ? 0 : -len;
    std::cout << len << '\t' << qName << '\t' << x << '\t'
	      << rName << '#' << rType;
    if (!s && rType2 != rType) std::cout << '/' << rType2;
    std::cout << '\t' << y << '\n';
  }
}

static void importOneFile(std::istream &in, const SegImportOptions &opts,
			  size_t &alnNum) {
  std::string n = opts.formatName;
  makeLowercase(n);
  if      (n == "bed") importBed(in, opts);
  else if (n == "chain") importChain(in, opts);
  else if (n == "genepred") importGenePred(in, opts);
  else if (n == "gff") importGff(in, opts);
  else if (n == "gtf") importGtf(in, opts);
  else if (n == "lasttab") importLastTab(in, opts, alnNum);
  else if (n == "maf") importMaf(in, opts, alnNum);
  else if (n == "psl") importPsl(in, opts, alnNum);
  else if (n == "rmsk") importRmsk(in, opts);
  else if (n == "sam") importSam(in, opts);
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
  opts.forwardSegNum = 0;
  opts.isAddAlignmentNum = false;
  opts.isCds = false;
  opts.is5utr = false;
  opts.is3utr = false;
  opts.isIntrons = false;
  opts.isPrimaryTranscripts = false;

  std::string prog = argv[0];
  std::string help = "\
Usage:\n\
  " + prog + " [options] bed inputFile(s)\n\
  " + prog + " [options] chain inputFile(s)\n\
  " + prog + " [options] genePred inputFile(s)\n\
  " + prog + " [options] gff inputFile(s)\n\
  " + prog + " [options] gtf inputFile(s)\n\
  " + prog + " [options] lastTab inputFile(s)\n\
  " + prog + " [options] maf inputFile(s)\n\
  " + prog + " [options] psl inputFile(s)\n\
  " + prog + " [options] rmsk inputFile(s)\n\
  " + prog + " [options] sam inputFile(s)\n\
\n\
Read segments or alignments in various formats, and write them in SEG format.\n\
\n\
Options:\n\
  -h, --help     show this help message and exit\n\
  -V, --version  show version number and exit\n\
  -f N           make the Nth segment in each seg line forward-stranded\n\
\n\
Options for lastTab, maf, psl:\n\
  -a             add alignment number and position to each seg line\n\
\n\
Options for bed, genePred, gtf:\n\
  -c             get CDS (coding regions)\n\
  -5             get 5' untranslated regions (UTRs)\n\
  -3             get 3' untranslated regions (UTRs)\n\
  -i             get introns\n\
  -p             get primary transcripts (exons plus introns)\n\
";

  const char sOpts[] = "hf:ac53ipV";

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
    case 'f':
      {
	StringView sv(optarg, optarg + std::strlen(optarg));
	sv >> opts.forwardSegNum;
	if (!sv) err("option -f: bad value");
      }
      break;
    case 'a':
      opts.isAddAlignmentNum = true;
      break;
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
