#include <stdlib.h>
#include <string.h>
#include "TimeParser.h"

int time_parse(char *time) {
    // 1) NULL
    if (time == NULL) return TIME_ARRAY_ERROR;

    // 2) pituus = 6
    if (strlen(time) != 6) return TIME_LEN_ERROR;

  	// 3) Vain numerot (unsigned char, ettei negatiivisia lipsu)
    for (int i = 0; i < 6; ++i) {
        unsigned char uc = (unsigned char)time[i];
        if (uc < '0' || uc > '9') return TIME_LEN_ERROR;
    }

	// Parsinta 
	char ss[3] = { time[4], time[5], '\0' };
	char mm[3] = { time[2], time[3], '\0' };
	char hh[3] = { time[0], time[1], '\0' };

	int second = atoi(ss);
	int minute = atoi(mm);
	int hour   = atoi(hh);

   // 4) Raja-arvot sis negatiivinen ehto
	if (hour < 0 || hour > 23) return TIME_VALUE_ERROR; // hour
	if (minute < 0 || minute > 59) return TIME_VALUE_ERROR; // minute
	if (second < 0 || second > 59) return TIME_VALUE_ERROR; // second
   
	// 6) sekunteina palautus
	return hour*3600 + minute*60 + second;
}