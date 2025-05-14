#include <stdio.h>


#define DEBUG

#ifdef DEBUG
#define debug(...) fprintf(debug_f, __VA_ARGS__)
#define debug_std(...) fprintf(out, __VA_ARGS__)
#else
#define debug(...)
#define debug_std(...)
#endif


/* Stream for output */
extern FILE *out;
extern FILE *debug_f;

void set_output_channel(const char *varname, FILE **channel, FILE *def_chan);
void init_output(void);