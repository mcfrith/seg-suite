// Copyright 2015 Martin C. Frith

#include <getopt.h>

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <stddef.h>  // size_t
#include <stdexcept>
#include <string>
#include <vector>

typedef const char *String;

struct Fraction {
  double numer;
  double denom;
};

struct SegJoinOptions {
  bool isComplete1;
  bool isComplete2;
  int overlappingFileNumber;
  int unjoinableFileNumber;
  bool isJoinOnAllSegments;
  Fraction minOverlap;
  const char *fileName1;
  const char *fileName2;
};

static void err(const std::string& s) {
  throw std::runtime_error(s);
}

static bool isGraph(char c) {
  return c > ' ';  // faster than std::isgraph
}

static bool isSpace(char c) {
  return c > 0 && c <= ' ';  // faster than std::isspace
}

static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

static bool isChar(const char *myString, char myChar) {
  return myString[0] == myChar && myString[1] == 0;
}

static std::istream &openIn(const char *fileName, std::ifstream &ifs) {
  if (isChar(fileName, '-')) return std::cin;
  ifs.open(fileName);
  if (!ifs) err("can't open file: " + std::string(fileName));
  return ifs;
}

static const char *readLong(const char *c, long &x) {
  if (!c) return 0;
  while (isSpace(*c)) ++c;
  if (*c == '-') {
    ++c;
    if (!isDigit(*c)) return 0;
    long z = '0' - *c++;
    while (isDigit(*c)) {
      if (z < LONG_MIN / 10) return 0;
      z *= 10;
      long digit = *c++ - '0';
      if (z < LONG_MIN + digit) return 0;
      z -= digit;
    }
    x = z;
  } else {
    // should we allow an initial '+'?
    if (!isDigit(*c)) return 0;
    long z = *c++ - '0';
    while (isDigit(*c)) {
      if (z > LONG_MAX / 10) return 0;
      z *= 10;
      long digit = *c++ - '0';
      if (z > LONG_MAX - digit) return 0;
      z += digit;
    }
    x = z;
  }
  return c;
}

// This writes a "long" integer into a char buffer ending at "end".
// It writes backwards from the end, because that's easier & faster.
static char *writeLong(char *end, long x) {
  unsigned long y = x;
  if (x < 0) y = -y;
  do {
    *--end = '0' + y % 10;
    y /= 10;
  } while (y);
  if (x < 0) *--end = '-';
  return end;
}

static const char *readWord(const char *c, String &s) {
  if (!c) return 0;
  while (isSpace(*c)) ++c;
  const char *e = c;
  while (isGraph(*e)) ++e;
  if (e == c) return 0;
  s = c;
  return e;
}

static const char *readFraction(const char *c, Fraction &f) {
  if (!c) return 0;
  char *e;
  double numer = std::strtod(c, &e);
  if (numer < 0 || e == c) return 0;
  double denom = 100;
  if (*e == '/') {
    denom = std::strtod(e + 1, &e);
    if (denom <= 0) return 0;
  }
  if (numer > denom) return 0;
  f.numer = numer;
  f.denom = denom;
  return e;
}

static bool isDataLine(const char *s) {
  for ( ; ; ++s) {
    if (*s == '#') return false;
    if (isGraph(*s)) return true;
    if (*s == 0) return false;
  }
}

static bool getDataLine(std::istream &in, std::string &line) {
  while (getline(in, line))
    if (isDataLine(line.c_str()))
      return true;
  return false;
}

struct SegPart {
  size_t seqNameBeg;
  size_t seqNameLen;
  long start;
};

struct Seg {
  std::string line;
  long part0end;
  std::vector<SegPart> parts;
};

static long segBeg(const Seg &s, size_t i) {
  return s.parts[i].start;
}

static long beg0(const Seg &s) {
  return s.parts[0].start;
}

static long end0(const Seg &s) {
  return s.part0end;
}

static void moveSeg(Seg &from, Seg &to) {
  swap(from.line, to.line);
  to.part0end = from.part0end;
  swap(from.parts, to.parts);
}

static int nameCmp(const Seg &x, const Seg &y, size_t part) {
  const SegPart &xp = x.parts[part];
  const SegPart &yp = y.parts[part];
  size_t n = std::min(xp.seqNameLen, yp.seqNameLen);
  int c = std::memcmp(x.line.c_str() + xp.seqNameBeg,
		      y.line.c_str() + yp.seqNameBeg, n);
  return c ? c : xp.seqNameLen - yp.seqNameLen;
}

static char *writeName(char *end, const Seg &s, size_t part) {
  const SegPart &p = s.parts[part];
  end -= p.seqNameLen;
  std::memcpy(end, s.line.c_str() + p.seqNameBeg, p.seqNameLen);
  return end;
}

static bool readSeg(std::istream &in, Seg &s) {
  s.parts.clear();
  if (!getDataLine(in, s.line)) return false;
  const char *b = s.line.c_str();
  long length;
  const char *c = readLong(b, length);
  SegPart p;
  while (true) {
    const char *n;
    c = readWord(c, n);
    if (!c) break;
    p.seqNameBeg = n - b;
    p.seqNameLen = c - n;
    c = readLong(c, p.start);
    if (!c) err("bad SEG line: " + s.line);
    s.parts.push_back(p);
  }
  if (s.parts.empty()) err("bad SEG line: " + s.line);
  s.part0end = beg0(s) + length;
  return true;
}

struct SortedSegReader {
  SortedSegReader(const char *fileName) : in(openIn(fileName, ifs)) { next(); }

  bool isMore() const { return !s.parts.empty(); }

  bool isNewSeqName() const { return isNewSeq; }

  const Seg &get() const { return s; }

  void next() {
    readSeg(in, t);
    if (s.parts.empty() || t.parts.empty()) {
      isNewSeq = true;
    } else {
      int c = nameCmp(s, t, 0);
      if (c > 0 || (c == 0 && beg0(s) > beg0(t)))
	err("input not sorted properly");
      isNewSeq = c;
    }
    moveSeg(t, s);
  }

  std::ifstream ifs;
  std::istream& in;
  Seg s, t;
  bool isNewSeq;
};

static char *segSliceHead(char *e, const Seg &s, long beg, long end) {
  e = writeLong(e, beg);
  *--e = '\t';
  e = writeName(e, s, 0);
  *--e = '\t';
  e = writeLong(e, end - beg);
  return e;
}

static char *segSliceTail(char *e, const Seg &s, long beg) {
  long offset = beg - beg0(s);
  for (size_t i = s.parts.size(); i --> 1; ) {
    e = writeLong(e, segBeg(s, i) + offset);
    *--e = '\t';
    e = writeName(e, s, i);
    *--e = '\t';
  }
  return e;
}

std::vector<char> buffer;

static void writeSegSlice(const Seg &s, long beg, long end) {
  size_t maxChangedStarts = s.parts.size();
  size_t space = s.line.size() + 32 * maxChangedStarts;
  buffer.resize(space);
  char *bufferEnd = &buffer.back() + 1;
  char *e = bufferEnd;
  *--e = '\n';
  e = segSliceTail(e, s, beg);
  e = segSliceHead(e, s, beg, end);
  std::cout.write(e, bufferEnd - e);
}

static void writeSegJoin(const Seg &s, const Seg &t, long beg, long end) {
  size_t maxChangedStarts = std::max(s.parts.size(), t.parts.size()) - 1;
  size_t space = s.line.size() + t.line.size() + 32 * maxChangedStarts;
  buffer.resize(space);
  char *bufferEnd = &buffer.back() + 1;
  char *e = bufferEnd;
  *--e = '\n';
  e = segSliceTail(e, t, beg);
  e = segSliceTail(e, s, beg);
  e = segSliceHead(e, s, beg, end);
  std::cout.write(e, bufferEnd - e);
}

static bool isOverlappable(const Seg &s, const Seg &t) {
  if (s.parts.size() != t.parts.size()) return false;
  long d = beg0(s) - beg0(t);
  for (size_t i = 1; i < s.parts.size(); ++i) {
    if (nameCmp(s, t, i)) return false;
    if (segBeg(s, i) - segBeg(t, i) != d) return false;
  }
  return true;
}

static void removeOldSegs(std::vector<Seg> &keptSegs, long ibeg) {
  size_t end = keptSegs.size();
  size_t j = 0;
  for ( ; ; ++j) {
    if (j == end) return;
    if (end0(keptSegs[j]) <= ibeg) break;
  }
  for (size_t k = j + 1; k < end; ++k) {
    Seg &t = keptSegs[k];
    if (end0(t) > ibeg)
      moveSeg(t, keptSegs[j++]);
  }
  keptSegs.resize(j);
}

static int newNameCmp(const Seg &s, const SortedSegReader &r) {
  return r.isMore() ? nameCmp(s, r.get(), 0) : -1;
}

static void skipOneSequence(SortedSegReader &r) {
  do {
    r.next();
  } while (!r.isNewSeqName());
}

static void updateKeptSegs(std::vector<Seg> &keptSegs, SortedSegReader &r,
			   const SortedSegReader &q) {
  const Seg &s = q.get();
  long ibeg = beg0(s);
  long iend = end0(s);

  if (q.isNewSeqName()) {
    keptSegs.clear();
    if (r.isNewSeqName()) {
      while (true) {
	int c = newNameCmp(s, r);
	if (c < 0) return;
	if (c == 0) break;
	skipOneSequence(r);
      }
    } else {
      while (true) {
	skipOneSequence(r);
	int c = newNameCmp(s, r);
	if (c < 0) return;
	if (c == 0) break;
      }
    }
  } else {
    removeOldSegs(keptSegs, ibeg);
    if (r.isNewSeqName()) {
      int c = newNameCmp(s, r);
      if (c < 0) return;
      assert(c == 0);
    }
  }

  do {
    const Seg &t = r.get();
    long jbeg = beg0(t);
    if (jbeg >= iend) break;
    long jend = end0(t);
    if (jend > ibeg) keptSegs.push_back(t);
    r.next();
  } while (!r.isNewSeqName());
}

static void writeUnjoinableSegs(SortedSegReader &querys, SortedSegReader &refs,
				bool isComplete, bool isAll) {
  std::vector<Seg> keptSegs;
  for ( ; querys.isMore(); querys.next()) {
    const Seg &s = querys.get();
    long ibeg = beg0(s);
    long iend = end0(s);
    updateKeptSegs(keptSegs, refs, querys);
    for (size_t j = 0; j < keptSegs.size(); ++j) {
      const Seg &t = keptSegs[j];
      long jbeg = beg0(t);
      if (jbeg >= iend) break;
      if (isAll && !isOverlappable(s, t)) continue;
      if (isComplete) {
	ibeg = iend;
	break;
      }
      long jend = end0(t);
      if (jbeg > ibeg) writeSegSlice(s, ibeg, jbeg);
      if (jend > ibeg) ibeg = jend;
    }
    if (iend > ibeg) writeSegSlice(s, ibeg, iend);
  }
}

static void writeOverlappingSegs(SortedSegReader &querys,
				 SortedSegReader &refs,
				 Fraction minFrac, bool isAll) {
  std::vector<Seg> keptSegs;
  for ( ; querys.isMore(); querys.next()) {
    const Seg &s = querys.get();
    long ibeg = beg0(s);
    long iend = end0(s);
    long overlap = 0;
    long kbeg = ibeg;
    updateKeptSegs(keptSegs, refs, querys);
    for (size_t j = 0; j < keptSegs.size(); ++j) {
      const Seg &t = keptSegs[j];
      long jbeg = beg0(t);
      long jend = end0(t);
      if (jbeg >= iend) break;
      if (jend <= kbeg) continue;
      if (isAll && !isOverlappable(s, t)) continue;
      long end = std::min(iend, jend);
      overlap += end - std::max(jbeg, kbeg);
      kbeg = end;
    }
    if (overlap * minFrac.denom >= (iend - ibeg) * minFrac.numer) {
      writeSegSlice(s, ibeg, iend);
    }
  }
}

static void writeJoinedSegs(SortedSegReader &r1, SortedSegReader &r2,
			    bool isComplete1, bool isComplete2, bool isAll) {
  std::vector<Seg> keptSegs;
  for ( ; r1.isMore(); r1.next()) {
    const Seg &s = r1.get();
    long ibeg = beg0(s);
    long iend = end0(s);
    updateKeptSegs(keptSegs, r2, r1);
    for (size_t j = 0; j < keptSegs.size(); ++j) {
      const Seg &t = keptSegs[j];
      long jbeg = beg0(t);
      if (jbeg >= iend) break;
      if (isAll && !isOverlappable(s, t)) continue;
      long jend = end0(t);
      if (isComplete1 && (ibeg < jbeg || iend > jend)) continue;
      if (isComplete2 && (jbeg < ibeg || jend > iend)) continue;
      long beg = std::max(ibeg, jbeg);
      long end = std::min(iend, jend);
      if (isAll) writeSegSlice(s, beg, end);
      else writeSegJoin(s, t, beg, end);
    }
  }
}

static void segJoin(const SegJoinOptions &opts) {
  SortedSegReader r1(opts.fileName1);
  SortedSegReader r2(opts.fileName2);
  if (opts.unjoinableFileNumber == 1)
    writeUnjoinableSegs(r1, r2, opts.isComplete1, opts.isJoinOnAllSegments);
  else if (opts.unjoinableFileNumber == 2)
    writeUnjoinableSegs(r2, r1, opts.isComplete2, opts.isJoinOnAllSegments);
  else if (opts.overlappingFileNumber == 1)
    writeOverlappingSegs(r1, r2, opts.minOverlap, opts.isJoinOnAllSegments);
  else if (opts.overlappingFileNumber == 2)
    writeOverlappingSegs(r2, r1, opts.minOverlap, opts.isJoinOnAllSegments);
  else
    writeJoinedSegs(r1, r2, opts.isComplete1, opts.isComplete2,
		    opts.isJoinOnAllSegments);
}

static void run(int argc, char **argv) {
  SegJoinOptions opts;
  opts.isComplete1 = false;
  opts.isComplete2 = false;
  opts.overlappingFileNumber = 0;
  opts.unjoinableFileNumber = 0;
  opts.isJoinOnAllSegments = false;
  opts.minOverlap.numer = 0;
  opts.minOverlap.denom = 0;

  std::string help = "\
Usage: " + std::string(argv[0]) + " [options] file1.seg file2.seg\n\
\n\
Read two SEG files, and write their JOIN.\n\
\n\
Options:\n\
  -h, --help     show this help message and exit\n\
  -c FILENUM     only use complete/contained records of file FILENUM\n\
  -f FILENUM     write complete records of file FILENUM, that overlap anything\n\
                 in the other file\n\
  -n PERCENT     write each record of file 2, if at least PERCENT of it is\n\
                 covered by file 1\n\
  -x PERCENT     write each record of file 2, if at most PERCENT of it is\n\
                 covered by file 1\n\
  -v FILENUM     only write unjoinable parts of file FILENUM\n\
  -w             join on whole segment-tuples, not just first segments\n\
  -V, --version  show version number and exit\n\
";

  const char sOpts[] = "hc:f:n:x:v:wV";

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
      if      (isChar(optarg, '1')) opts.isComplete1 = true;
      else if (isChar(optarg, '2')) opts.isComplete2 = true;
      else err("option -c: should be 1 or 2");
      break;
    case 'f':
      if (opts.overlappingFileNumber) err("option -f: cannot use twice");
      else if (isChar(optarg, '1')) opts.overlappingFileNumber = 1;
      else if (isChar(optarg, '2')) opts.overlappingFileNumber = 2;
      else err("option -f: should be 1 or 2");
      break;
    case 'n':
      if (opts.minOverlap.denom) err("option -n/-x: cannot use twice");
      if (!readFraction(optarg, opts.minOverlap))
	err("option -n: bad value");
      break;
    case 'x':
      if (opts.minOverlap.denom) err("option -n/-x: cannot use twice");
      if (!readFraction(optarg, opts.minOverlap))
	err("option -x: bad value");
      opts.minOverlap.numer *= -1;
      opts.minOverlap.denom *= -1;
      break;
    case 'v':
      if (opts.unjoinableFileNumber) err("option -v: cannot use twice");
      else if (isChar(optarg, '1')) opts.unjoinableFileNumber = 1;
      else if (isChar(optarg, '2')) opts.unjoinableFileNumber = 2;
      else err("option -v: should be 1 or 2");
      break;
    case 'w':
      opts.isJoinOnAllSegments = true;
      break;
    case 'V':
      std::cout << "seg-join "
#include "version.hh"
	"\n";
      return;
    case '?':
      std::cerr << help;
      err("");
    }
  }

  if (opts.minOverlap.denom && !opts.overlappingFileNumber) {
    opts.overlappingFileNumber = 2;
  }

  if (opts.overlappingFileNumber && !opts.minOverlap.denom) {
    opts.minOverlap.numer = 1;
    unsigned long x = -1;
    opts.minOverlap.denom = x / 2 + 1;
  }

  if (optind != argc - 2) {
    std::cerr << help;
    err("");
  }

  opts.fileName1 = argv[argc - 2];
  opts.fileName2 = argv[argc - 1];

  std::ios_base::sync_with_stdio(false);  // makes it faster!

  segJoin(opts);
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
