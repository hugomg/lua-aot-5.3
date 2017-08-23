#ifndef pretty_printer_h
#define pretty_printer_h

typedef struct {
    FILE *outfile;
    int indent_level;
} PrettyPrinter;

void PP_init(PrettyPrinter *pp, FILE *outfile);
void PP_indent(PrettyPrinter *pp);
void PP_dedent(PrettyPrinter *pp);

// Write a full line
void PP_writeln(PrettyPrinter *pp, const char *fmt, ...);

// Write a line part by part
void PP_begin_line(PrettyPrinter *pp);
void PP_write(PrettyPrinter *pp, const char *fmt, ...);
void PP_end_line(PrettyPrinter *pp);

#endif
