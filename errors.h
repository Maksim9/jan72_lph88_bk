#ifndef _ERRORS_H
#define _ERRORS_H

typedef struct errors{
    uint16_t id;
    uint8_t * msg;
} ERRORS, * PERRORS;

extern ERRORS emsg[];

#endif