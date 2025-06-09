#ifndef ESH_LIBTIME_H_INCLUDED
#define ESH_LIBTIME_H_INCLUDED

#include "../esh.h"

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

typedef struct iso_time {
	int year, month, day, hour, minute, second;
	int tz;
	bool local_time;
} iso_time;

int parse_iso_time(esh_state *esh, const char *str, size_t len, iso_time *time);
int iso_time_to_string(esh_state *esh, const iso_time *time);

void tm_to_iso_time(const struct tm *tm, iso_time *it, bool local_time);
void iso_time_to_tm(const iso_time *it, struct tm *tm);

bool is_leap_year(int year);
int days_in_month(int year, int month);

#endif
