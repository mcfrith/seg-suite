// Copyright 2015 Martin C. Frith

#include <getopt.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stddef.h>  // size_t
#include <stdexcept>
#include <string>
#include <vector>

struct SegJoinOptions {
  bool isComplete1;
  bool isComplete2;
  int unjoinableFileNumber;
  bool isJoinOnAllSegments;
  const char *fileName1;
  const char *fileName2;
};

struct Seg {
  long length;
  std::vector<std::string> seqNames;
  std::vector<long> starts;
};

static bool isBeforeBeg(const Seg &x, const Seg &y) {
  if (x.seqNames[0] != y.seqNames[0]) return x.seqNames[0] < y.seqNames[0];
  return x.starts[0] < y.starts[0];
}

static bool isBeforeEnd(const Seg &x, const Seg &y) {
  if (x.seqNames[0] != y.seqNames[0]) return x.seqNames[0] < y.seqNames[0];
  return x.starts[0] < y.starts[0] + y.length;
}

static void err(const std::string& s) {
  throw std::runtime_error(s);
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

static bool isDataLine(const char *s) {
  for ( ; ; ++s) {
    if (*s == '#') return false;
    if (*s > ' ') return true;
    if (*s == 0) return false;
  }
}

static bool getDataLine(std::istream &in, std::string &line) {
  while (getline(in, line))
    if (isDataLine(line.c_str()))
      return true;
  return false;
}

static bool readSeg(std::istream &in, Seg &s) {
  std::string line;
  if (!getDataLine(in, line)) return false;
  std::istringstream iss(line);
  iss >> s.length;
  std::string seqName;
  long start;
  while (iss >> seqName) {
    if (!(iss >> start)) err("bad SEG line: " + line);
    s.seqNames.push_back(seqName);
    s.starts.push_back(start);
  }
  if (s.starts.empty()) err("bad SEG line: " + line);
  return true;
}

struct SortedSegReader {
  SortedSegReader(const char *fileName) : in(openIn(fileName, ifs)) { next(); }

  bool isMore() const { return !s.starts.empty(); }

  const Seg &get() const { return s; }

  void next() {
    Seg t;
    readSeg(in, t);
    if (!s.starts.empty() && !t.starts.empty() && isBeforeBeg(t, s))
      err("input not sorted properly");
    s = t;
  }

  std::ifstream ifs;
  std::istream& in;
  Seg s;
};

static void segSliceHead(const Seg &s, long beg, long end) {
  std::cout << (end - beg) << '\t' << s.seqNames[0] << '\t' << beg;
}

static void segSliceTail(const Seg &s, long beg) {
  long offset = beg - s.starts[0];
  for (size_t i = 1; i < s.starts.size(); ++i)
    std::cout << '\t' << s.seqNames[i] << '\t' << (s.starts[i] + offset);
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
  if (s.starts.size() != t.starts.size()) return false;
  long d = s.starts[0] - t.starts[0];
  for (size_t i = 1; i < s.starts.size(); ++i) {
    if (s.seqNames[i] != t.seqNames[i]) return false;
    if (s.starts[i] - t.starts[i] != d) return false;
  }
  return true;
}

struct isOldSeg {
  isOldSeg(const Seg &sIn) : s(sIn) {}
  bool operator()(const Seg &t) const { return !isBeforeEnd(s, t); }
  const Seg &s;
};

static void updateKeptSegs(std::vector<Seg> &keptSegs, SortedSegReader &r,
			   const Seg &s) {
  while (r.isMore()) {
    const Seg &t = r.get();
    if (!isBeforeEnd(t, s)) break;
    keptSegs.push_back(t);
    r.next();
  }
  keptSegs.erase(remove_if(keptSegs.begin(), keptSegs.end(), isOldSeg(s)),
		 keptSegs.end());
}

static void writeUnjoinableSegs(SortedSegReader &querys, SortedSegReader &refs,
				bool isComplete, bool isAll) {
  std::vector<Seg> keptSegs;
  while (querys.isMore()) {
    const Seg &s = querys.get();
    long ibeg = s.starts[0];
    long iend = ibeg + s.length;
    updateKeptSegs(keptSegs, refs, s);
    for (size_t j = 0; j < keptSegs.size(); ++j) {
      const Seg &t = keptSegs[j];
      if (!isBeforeEnd(t, s)) break;
      if (isAll && !isOverlappable(s, t)) continue;
      if (isComplete) {
	ibeg = iend;
	break;
      }
      long jbeg = t.starts[0];
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
    long ibeg = s.starts[0];
    long iend = ibeg + s.length;
    updateKeptSegs(keptSegs, r2, s);
    for (size_t j = 0; j < keptSegs.size(); ++j) {
      const Seg &t = keptSegs[j];
      if (!isBeforeEnd(t, s)) break;
      if (isAll && !isOverlappable(s, t)) continue;
      long jbeg = t.starts[0];
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
