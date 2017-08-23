#include <stdio.h>
#include <stdarg.h>

#include "pretty_printer.h"

void PP_init(PrettyPrinter *pp, FILE *outfile)
{
  pp->outfile = outfile;
}

void PP_indent(PrettyPrinter *pp)
{
  pp->indent_level += 1;
}

void PP_dedent(PrettyPrinter *pp)
{
  pp->indent_level -= 1;
}


void PP_begin_line(PrettyPrinter *pp)
{
  for(int i=0; i < pp->indent_level; i++){
    fprintf(pp->outfile, "  ");
  }
}

void PP_write(PrettyPrinter *pp, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(pp->outfile, fmt, args);
  va_end(args);
}

void PP_end_line(PrettyPrinter *pp)
{
  fprintf(pp->outfile, "\n");
}

void PP_writeln(PrettyPrinter *pp, const char *fmt, ...)
{
  PP_begin_line(pp);

  va_list args;
  va_start(args, fmt);
  vfprintf(pp->outfile, fmt, args);
  va_end(args);

  PP_end_line(pp);
}
