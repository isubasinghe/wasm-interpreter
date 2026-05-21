#include <sstream>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <getopt.h>

#include "common.h"
#include "parse.h"
#include "ir.h"
#include "dump.h"
#include "interpreter.h"

static struct option long_options[] = {
  {"trace", no_argument,  &g_trace, 1},
  {"file-log", no_argument, NULL, 'l'},
  {"args", optional_argument, NULL, 'a'},
  {"help", no_argument, NULL, 'h'}
};

typedef struct args_t {
  std::string infile;
  std::vector<std::string> mainargs;
  bool file_logging = false;
} args_t;

args_t parse_args(int argc, char* argv[]) {
  int opt;
  args_t args;
  optind = 0;
  while ((opt = getopt_long_only(argc, argv, ":a:lh", long_options, NULL)) != -1) {
    switch(opt) {
      case 0: break;
      case 'l':
        args.file_logging = true;
        break;
      case 'a':
        args.mainargs.push_back(optarg);
        while ((optind < (argc - 1))) {
          args.mainargs.push_back(argv[optind++]);
        }
        break;
      case 'h':
      default:
        ERR("Usage: %s [--trace] [--file-log] [-a <space-separated args>] <input-file>\n", argv[0]);
        exit(opt != 'h');
    }
  }

  if ( ((optind + 1) != argc)) {
    ERR("Executable takes one positional argument \"filename\"\n");
    exit(1);
  }

  args.infile = std::string(argv[optind]);
  return args;
}

// Main function.
// Parses arguments and either runs a file with arguments.
//  --trace: enable tracing to stderr
int main(int argc, char *argv[]) {
  args_t args = parse_args(argc, argv);
    
  byte* start = NULL;
  byte* end = NULL;

  const char *infile = args.infile.c_str();
  ssize_t r = load_file(infile, &start, &end);
  if (r < 0) {
    ERR("failed to load: %s\n", infile);
    return 1;
  }

  TRACE("loaded %s: %ld bytes\n", infile, r);
  WasmModule mod = parse_bytecode(start, end);
  unload_file(&start, &end);
  
  /* Interpreter here */
  /* */

  // dump_module(mod);
  Interpreter interpreter = Interpreter(mod, args.file_logging);
  interpreter.run_main();
  interpreter.dump_stack();

  

  return 0;
}


