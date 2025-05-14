#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "output_infra.h"

FILE *out = NULL;
FILE *debug_f = NULL;

void set_output_channel(const char *varname, FILE **channel, FILE *def_chan) {
   char *outdir = getenv(varname);
   if (!outdir) {
      *channel = def_chan;
      return;
   }

   // create directory if needed
   struct stat st = {0};
   if (stat(outdir, &st) == -1) {
      mkdir(outdir, 0700);
   }

   // generate a unique file per process
   int i = getpid();
   char *outfile = malloc(strlen(outdir) + 1 + 2*sizeof(pid_t)); // one byte is two hex chars
   if (!outfile) {
      fprintf(stderr, "Could not allocate for pathname");
      exit(1);
   }

   snprintf(outfile, strlen(outdir) + 1 + 2*sizeof(pid_t), "%s/%x", outdir, i);


   *channel = fopen(outfile, "a");
   if (!*channel) {
      fprintf(stderr, "Error opening file %s", outdir);
   }

}

void init_output(void) {
   set_output_channel("OUTFILE", &out, stdout);
#ifdef DEBUG
   set_output_channel("DEBUG", &debug_f, stderr);
#endif

}