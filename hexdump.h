#ifndef INCLUDED_HEXDUMP_H
#define INCLUDED_HEXDUMP_H

#ifdef __cplusplus
extern "C" {
#endif

void hex_dump(char *desc, void *addr, int len);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDED_HEXDUMP_H */
