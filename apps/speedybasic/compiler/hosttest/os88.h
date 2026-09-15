#ifndef SBW_HOST_OS88_H
#define SBW_HOST_OS88_H
struct os88_place { unsigned clus; int vol; };
int os88_peek(unsigned segment, unsigned offset);
void os88_poke(unsigned segment, unsigned offset, int value);
int os88_file_write_seg(const char *name, unsigned segment, unsigned count);
unsigned os88_file_read_seg(const char *name, unsigned segment,
                            unsigned capacity);
int os88_ferr(void);
unsigned os88_mem_claim(unsigned kilobytes);
void os88_mem_free(unsigned segment);
void os88_file_here(struct os88_place *place);
int os88_file_goto(struct os88_place *place);
void os88_utoa(unsigned value, char *text);
unsigned os88_strlen(const char *text);
void os88_strcpy(char *destination, const char *source, unsigned capacity);
#endif
