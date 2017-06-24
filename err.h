#ifndef _ERR_
#define _ERR_

/* Wypisuje informacje o blednym zakonczeniu funkcji systemowej
i konczy dzialanie programu. */
extern void syserr(const char *fmt, ...);

/* Wypisuje informacje o bledzie i konczy dzialanie programu. */
extern void fatal(const char *fmt, ...);

/* Wypisuje informacje o bledzie ale NIE konczy dzialania programu. */
extern void bad(const char *fmt, ...);


#endif
