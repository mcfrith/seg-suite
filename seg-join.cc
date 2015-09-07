// Copyright 2015 Martin C. Frith

#include <getopt.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <stddef.h>  // size_t
#include <stdexcept>
#include <string>
#include <vector>

typedef const char *String;

struct SegJoinOptions {
  bool isComplete1;
  bool isComplete2;
  int unjoinableFileNumber;
  bool isJoinOnAllSegments;
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

static char *writeLong(char *dest, long x) {
  unsigned long y = x;
  if (x < 0) {
    *dest++ = '-';
    y = -y;
  }
  char *start = dest;
  do {
    *dest++ = '0' + y % 10;
    y /= 10;
  } while (y);
  std::reverse(start, dest);
  return dest;
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

static int wordCmp(const char* x, const char* y) {
  // Like strcmp, but stops at spaces.
  while (isGraph(*y)) {
    if (*x != *y) return *x - *y;
    ++x;
    ++y;
  }
  return isGraph(*x);
}

static char *writeWord(char *dest, const char *c) {
  while (isGraph(*c)) *dest++ = *c++;
  return dest;
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
  long start;
};

struct Seg {
  std::string line;
  long end0;  // end coordinate of the first segment
  std::vector<SegPart> parts;
};

static void moveSeg(Seg &from, Seg &to) {
  swap(from.line, to.line);
  to.end0 = from.end0;
  swap(from.parts, to.parts);
}

static int nameCmp(const Seg &x, const Seg &y, size_t part) {
  return wordCmp(x.line.c_str() + x.parts[part].seqNameBeg,
		 y.line.c_str() + y.parts[part].seqNameBeg);
}

static char *writeName(char *dest, const Seg &s, size_t part) {
  return writeWord(dest, s.line.c_str() + s.parts[part].seqNameBeg);
}

static bool isBeforeBeg(const Seg &x, const Seg &y) {
  int c = nameCmp(x, y, 0);
  if (c) return c < 0;
  return x.parts[0].start < y.parts[0].start;
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
    c = readLong(c, p.start);
    if (!c) err("bad SEG line: " + s.line);
    s.parts.push_back(p);
  }
  if (s.parts.empty()) err("bad SEG line: " + s.line);
  s.end0 = s.parts[0].start + length;
  return true;
}

struct SortedSegReader {
  SortedSegReader(const char *fileName) : in(openIn(fileName, ifs)) { next(); }

  bool isMore() const { return !s.parts.empty(); }

  const Seg &get() const { return s; }

  void next() {
    readSeg(in, t);
    if (!s.parts.empty() && !t.parts.empty() && isBeforeBeg(t, s))
      err("input not sorted properly");
    moveSeg(t, s);
  }

  std::ifstream ifs;
  std::istream& in;
  Seg s, t;
};

static char *segSliceHead(char *dest, const Seg &s, long beg, long end) {
  dest = writeLong(dest, end - beg);
  *dest++ = '\t';
  dest = writeName(dest, s, 0);
  *dest++ = '\t';
  dest = writeLong(dest, beg);
  return dest;
}

static char *segSliceTail(char *dest, const Seg &s, long beg) {
  long offset = beg - s.parts[0].start;
  for (size_t i = 1; i < s.parts.size(); ++i) {
    *dest++ = '\t';
    dest = writeName(dest, s, i);
    *dest++ = '\t';
    dest = writeLong(dest, s.parts[i].start + offset);
  }
  return dest;
}

std::vector<char> buffer;

static void writeSegSlice(const Seg &s, long beg, long end) {
  size_t maxChangedStarts = s.parts.size();
  size_t space = s.line.size() + 32 * maxChangedStarts;
  buffer.resize(space);
  char *b = &buffer[0];
  char *c = segSliceHead(b, s, beg, end);
  c = segSliceTail(c, s, beg);
  *c++ = '\n';
  std::cout.write(b, c - b);
}

static void writeSegJoin(const Seg &s, const Seg &t, long beg, long end) {
  size_t maxChangedStarts = std::max(s.parts.size(), t.parts.size()) - 1;
  size_t space = s.line.size() + t.line.size() + 32 * maxChangedStarts;
  buffer.resize(space);
  char *b = &buffer[0];
  char *c = segSliceHead(b, s, beg, end);
  c = segSliceTail(c, s, beg);
  c = segSliceTail(c, t, beg);
  *c++ = '\n';
  std::cout.write(b, c - b);
}

static bool isOverlappable(const Seg &s, const Seg &t) {
  if (s.parts.size() != t.parts.size()) return false;
  long d = s.parts[0].start - t.parts[0].start;
  for (size_t i = 1; i < s.parts.size(); ++i) {
    if (nameCmp(s, t, i)) return false;
    if (s.parts[i].start - t.parts[i].start != d) return false;
  }
  return true;
}

static void removeOldSegs(std::vector<Seg> &keptSegs, long ibeg) {
  size_t end = keptSegs.size();
  size_t j = 0;
  for ( ; ; ++j) {
    if (j == end) return;
    if (keptSegs[j].end0 <= ibeg) break;
  }
  for (size_t k = j + 1; k < end; ++k) {
    Seg &t = keptSegs[k];
    if (t.end0 > ibeg)
      moveSeg(t, keptSegs[j++]);
  }
  keptSegs.resize(j);
}

static void updateKeptSegs(std::vector<Seg> &keptSegs, SortedSegReader &r,
			   const Seg &s) {
  long ibeg = s.parts[0].start;
  long iend = s.end0;
  if (keptSegs.size()) {
    const Seg &t = keptSegs[0];
    if (nameCmp(s, t, 0))
      keptSegs.clear();
    else
      removeOldSegs(keptSegs, ibeg);
  }
  for ( ; r.isMore(); r.next()) {
    const Seg &t = r.get();
    int c = nameCmp(s, t, 0);
    if (c > 0) continue;
    if (c < 0) break;
    long jbeg = t.parts[0].start;
    if (jbeg >= iend) break;
    long jend = t.end0;
    if (jend > ibeg) keptSegs.push_back(t);
  }
}

static void writeUnjoinableSegs(SortedSegReader &querys, SortedSegReader &refs,
				bool isComplete, bool isAll) {
  std::vector<Seg> keptSegs;
  for ( ; querys.isMore(); querys.next()) {
    const Seg &s = querys.get();
    long ibeg = s.parts[0].start;
    long iend = s.end0;
    updateKeptSegs(keptSegs, refs, s);
    for (size_t j = 0; j < keptSegs.size(); ++j) {
      const Seg &t = keptSegs[j];
      long jbeg = t.parts[0].start;
      if (jbeg >= iend) break;
      if (isAll && !isOverlappable(s, t)) continue;
      if (isComplete) {
	ibeg = iend;
	break;
      }
      long jend = t.end0;
      if (jbeg > ibeg) writeSegSlice(s, ibeg, jbeg);
      if (jend > ibeg) ibeg = jend;
    }
    if (iend > ibeg) writeSegSlice(s, ibeg, iend);
  }
}

static void writeJoinedSegs(SortedSegReader &r1, SortedSegReader &r2,
			    bool isComplete1, bool isComplete2, bool isAll) {
  std::vector<Seg> keptSegs;
  for ( ; r1.isMore(); r1.next()) {
    const Seg &s = r1.get();
    long ibeg = s.parts[0].start;
    long iend = s.end0;
    updateKeptSegs(keptSegs, r2, s);
    for (size_t j = 0; j < keptSegs.size(); ++j) {
      const Seg &t = keptSegs[j];
      long jbeg = t.parts[0].start;
      if (jbeg >= iend) break;
      if (isAll && !isOverlappable(s, t)) continue;
      long jend = t.end0;
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
  else
    writeJoinedSegs(r1, r2, opts.isComplete1, opts.isComplete2,
		    opts.isJoinOnAllSegments);
}

static void run(int argc, char **argv) {
  SegJoinOptions opts;
  opts.isComplete1 = false;
  opts.isComplete2 = false;
  opts.unjoinableFileNumber = 0;
  opts.isJoinOnAllSegments = false;

  std::string help = "\
Usage: " + std::string(argv[0]) + " [options] file1.seg file2.seg\n\
\n\
Read two SEG files, and write their JOIN.\n\
\n\
Options:\n\
  -h, --help     show this help message and exit\n\
  -c FILENUM     only use complete/contained records of file FILENUM\n\
  -v FILENUM     only write unjoinable parts of file FILENUM\n\
  -w             join on whole segment-tuples, not just first segments\n\
  -V, --version  show version number and exit\n\
";

  const char sOpts[] = "hc:v:wV";

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
