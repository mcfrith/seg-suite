// Copyright 2015 Martin C. Frith

#include <getopt.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
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

static void writeWord(std::ostream &out, const char *c) {
  const char *e = c;
  while (isGraph(*e)) ++e;
  out.write(c, e - c);
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
  const char *seqName;
  long start;
};

struct Seg {
  std::string line;
  long length;
  std::vector<SegPart> parts;
};

static bool isBeforeBeg(const Seg &x, const Seg &y) {
  const SegPart &x0 = x.parts[0];
  const SegPart &y0 = y.parts[0];
  int c = wordCmp(x0.seqName, y0.seqName);
  if (c) return c < 0;
  return x0.start < y0.start;
}

static bool readSeg(std::istream &in, Seg &s) {
  if (!getDataLine(in, s.line)) return false;
  const char *c = s.line.c_str();
  c = readLong(c, s.length);
  SegPart p;
  while (true) {
    c = readWord(c, p.seqName);
    if (!c) break;
    c = readLong(c, p.start);
    if (!c) err("bad SEG line: " + s.line);
    s.parts.push_back(p);
  }
  if (s.parts.empty()) err("bad SEG line: " + s.line);
  return true;
}

struct SortedSegReader {
  SortedSegReader(const char *fileName) : in(openIn(fileName, ifs)) { next(); }

  bool isMore() const { return !s.parts.empty(); }

  const Seg &get() const { return s; }

  void next() {
    Seg t;
    readSeg(in, t);
    if (!s.parts.empty() && !t.parts.empty() && isBeforeBeg(t, s))
      err("input not sorted properly");
    s = t;
  }

  std::ifstream ifs;
  std::istream& in;
  Seg s;
};

static void segSliceHead(const Seg &s, long beg, long end) {
  std::cout << (end - beg) << '\t';
  writeWord(std::cout, s.parts[0].seqName);
  std::cout << '\t' << beg;
}

static void segSliceTail(const Seg &s, long beg) {
  long offset = beg - s.parts[0].start;
  for (size_t i = 1; i < s.parts.size(); ++i) {
    const SegPart &si = s.parts[i];
    std::cout << '\t';
    writeWord(std::cout, si.seqName);
    std::cout << '\t' << (si.start + offset);
  }
}

static void writeSegSlice(const Seg &s, long beg, long end) {
  segSliceHead(s, beg, end);
  segSliceTail(s, beg);
  std::cout << '\n';
}

static void writeSegJoin(const Seg &s, const Seg &t, long beg, long end) {
  segSliceHead(s, beg, end);
  segSliceTail(s, beg);
  segSliceTail(t, beg);
  std::cout << '\n';
}

static bool isOverlappable(const Seg &s, const Seg &t) {
  if (s.parts.size() != t.parts.size()) return false;
  long d = s.parts[0].start - t.parts[0].start;
  for (size_t i = 1; i < s.parts.size(); ++i) {
    const SegPart &si = s.parts[i];
    const SegPart &ti = t.parts[i];
    if (wordCmp(si.seqName, ti.seqName)) return false;
    if (si.start - ti.start != d) return false;
  }
  return true;
}

struct isOldSeg {
  isOldSeg(long ibegIn) : ibeg(ibegIn) {}
  bool operator()(const Seg &t) const
  { return t.parts[0].start + t.length <= ibeg; }
  long ibeg;
};

static void updateKeptSegs(std::vector<Seg> &keptSegs, SortedSegReader &r,
			   const Seg &s) {
  const SegPart &s0 = s.parts[0];
  long ibeg = s0.start;
  long iend = ibeg + s.length;
  if (keptSegs.size()) {
    const Seg &t = keptSegs[0];
    const SegPart &t0 = t.parts[0];
    if (wordCmp(s0.seqName, t0.seqName))
      keptSegs.clear();
    else
      keptSegs.erase(remove_if(keptSegs.begin(), keptSegs.end(),
			       isOldSeg(ibeg)), keptSegs.end());
  }
  while (r.isMore()) {
    const Seg &t = r.get();
    const SegPart &t0 = t.parts[0];
    int c = wordCmp(s0.seqName, t0.seqName);
    if (c < 0) break;
    if (c == 0) {
      long jbeg = t0.start;
      if (jbeg >= iend) break;
      long jend = jbeg + t.length;
      if (jend > ibeg) keptSegs.push_back(t);
    }
    r.next();
  }
}

static void writeUnjoinableSegs(SortedSegReader &querys, SortedSegReader &refs,
				bool isComplete, bool isAll) {
  std::vector<Seg> keptSegs;
  while (querys.isMore()) {
    const Seg &s = querys.get();
    long ibeg = s.parts[0].start;
    long iend = ibeg + s.length;
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
      long jend = jbeg + t.length;
      if (jbeg > ibeg) writeSegSlice(s, ibeg, jbeg);
      if (jend > ibeg) ibeg = jend;
    }
    if (iend > ibeg) writeSegSlice(s, ibeg, iend);
    querys.next();
  }
}

static void writeJoinedSegs(SortedSegReader &r1, SortedSegReader &r2,
			    bool isComplete1, bool isComplete2, bool isAll) {
  std::vector<Seg> keptSegs;
  while (r1.isMore()) {
    const Seg &s = r1.get();
    long ibeg = s.parts[0].start;
    long iend = ibeg + s.length;
    updateKeptSegs(keptSegs, r2, s);
    for (size_t j = 0; j < keptSegs.size(); ++j) {
      const Seg &t = keptSegs[j];
      long jbeg = t.parts[0].start;
      if (jbeg >= iend) break;
      if (isAll && !isOverlappable(s, t)) continue;
      long jend = jbeg + t.length;
      if (isComplete1 && (ibeg < jbeg || iend > jend)) continue;
      if (isComplete2 && (jbeg < ibeg || jend > iend)) continue;
      long beg = std::max(ibeg, jbeg);
      long end = std::min(iend, jend);
      if (isAll) writeSegSlice(s, beg, end);
      else writeSegJoin(s, t, beg, end);
    }
    r1.next();
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
