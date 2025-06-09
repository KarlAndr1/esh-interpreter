#include "libtime.h"

#include <assert.h>

typedef struct cursor {
	const char *str, *at, *end;
} cursor;

static int expect_char(esh_state *esh, cursor *cr, char c) {
	if(cr->at == cr->end) {
		esh_err_printf(esh, "Invalid date: '%s'; expected %c, got EOF", cr->str, c);
		return 1;
	}
	
	char fc = *(cr->at++);
	if(fc != c) {
		esh_err_printf(esh, "Invalid date: '%s'; expected %c, got %c", c, fc);
		return 1;
	}
	
	return 0;
}

static int parse_int(esh_state *esh, cursor *cr, int n_digits, int *num) {
	*num = 0;
	if(cr->at + n_digits > cr->end) {
		esh_err_printf(esh, "Invalid date: '%s'; expected digit, got EOF", cr->str);
		return 1;
	}
	
	while(n_digits--) {
		*num *= 10;
		char c = *(cr->at++);
		if(c < '0' || c > '9') {
			esh_err_printf(esh, "Invalid date: '%s'; expected digit, got %c", cr->str, c);
			return 1;
		}
		
		*num += c - '0';
	}
	
	return 0;
}

static bool at_end(cursor *cr) {
	return cr->at == cr->end;
}

int parse_iso_time(esh_state *esh, const char *str, size_t len, iso_time *time) {
	cursor cr = { str, str, str + len };
	
	*time = (iso_time) { .local_time = true };
	
	if(parse_int(esh, &cr, 4, &time->year)) goto ERR;

	if(at_end(&cr)) return 0;
	
	if(expect_char(esh, &cr, '-')) goto ERR;
	if(parse_int(esh, &cr, 2, &time->month)) goto ERR;
	if(time->month < 1 || time->month > 12) {
		esh_err_printf(esh, "Invalid date: '%s'; invalid month '%i'", str, time->month);
		goto ERR;
	}
	
	if(at_end(&cr)) return 0;
	
	if(expect_char(esh, &cr, '-')) goto ERR;
	if(parse_int(esh, &cr, 2, &time->day)) goto ERR;
	if(time->day < 1 || time->day > 31) {
		esh_err_printf(esh, "Invalid date: '%s'; invalid day '%i'", str, time->day);
		goto ERR;
	}
	
	if(at_end(&cr)) return 0;
	
	if(expect_char(esh, &cr, 'T')) goto ERR;
	if(parse_int(esh, &cr, 2, &time->hour)) goto ERR;
	if(time->hour < 0 || time->hour > 23) {
		esh_err_printf(esh, "Invalid date: '%s'; invalid hour '%i'", str, time->hour);
		goto ERR;
	}
	
	if(at_end(&cr)) return 0;
	
	if(expect_char(esh, &cr, ':')) goto ERR;
	if(parse_int(esh, &cr, 2, &time->minute)) goto ERR;
	if(time->minute < 0 || time->minute > 59) {
		esh_err_printf(esh, "Invalid date: '%s'; invalid minute '%i'", str, time->minute);
		goto ERR;
	}
	
	if(at_end(&cr)) return 0;
	if(expect_char(esh, &cr, ':')) goto ERR;
	if(parse_int(esh, &cr, 2, &time->second)) goto ERR;
	if(time->second < 0 || time->second > 59) {
		esh_err_printf(esh, "Invalid date: '%s'; invalid second '%i'", str, time->second);
		goto ERR;
	}
	
	if(at_end(&cr)) return 0;
	
	if(expect_char(esh, &cr, 'Z')) goto ERR;
	time->local_time = false;
	
	if(at_end(&cr)) return 0;
	
	if(!at_end(&cr)) {
		esh_err_printf(esh, "Invalid date: '%s'; expected EOF", str);
		goto ERR;
	}
	return 0;
	
	ERR:
	return 1;
}

static int write_int(esh_state *esh, int digits, int i) {
	assert(i <= digits * 10);
	assert(i >= 0);
	while(digits) {
		char digit = i / digits;
		i -= digit * digits;
		
		digit += '0';
		if(esh_str_buff_appendc(esh, digit)) return 1;
		
		digits /= 10;
	}
	
	return 0;
}

int iso_time_to_string(esh_state *esh, const iso_time *time) {
	esh_str_buff_begin(esh);
	
	if(time->year < 0 || time->year > 9999) {
		esh_err_printf(esh, "Unable to convert date to string; invalid year '%i'", time->year);
		goto ERR;
	}
	if(write_int(esh, 1000, time->year)) goto ERR;
	
	if(esh_str_buff_appendc(esh, '-')) goto ERR;
	
	if(time->month < 1 || time->month > 12) {
		esh_err_printf(esh, "Unable to convert date to string; invalid month '%i'", time->month);
		goto ERR;
	}
	if(write_int(esh, 10, time->month)) goto ERR;
	
	if(esh_str_buff_appendc(esh, '-')) goto ERR;
	
	if(time->day < 1 || time->day > 31) {
		esh_err_printf(esh, "Unable to convert date to string; invalid day '%i'", time->day);
		goto ERR;
	}
	if(write_int(esh, 10, time->day)) goto ERR;
	
	if(esh_str_buff_appendc(esh, 'T')) goto ERR;
	
	if(time->hour < 0 || time->hour > 23) {
		esh_err_printf(esh, "Unable to convert date to string; invalid hour '%i'", time->hour);
		goto ERR;
	}
	if(write_int(esh, 10, time->hour)) goto ERR;
	
	if(esh_str_buff_appendc(esh, ':')) goto ERR;
	
	if(time->minute < 0 || time->minute > 59) {
		esh_err_printf(esh, "Unable to convert date to string; invalid minute '%i'", time->minute);
		goto ERR;
	}
	if(write_int(esh, 10, time->minute)) goto ERR;
	
	if(esh_str_buff_appendc(esh, ':')) goto ERR;
	
	if(time->second < 0 || time->second > 59) {
		esh_err_printf(esh, "Unable to convert date to string; invalid second '%i'", time->second);
		goto ERR;
	}
	if(write_int(esh, 10, time->second)) goto ERR;
	
	if(!time->local_time) {
		if(esh_str_buff_appendc(esh, 'Z')) goto ERR;
	}
	
	size_t len;
	const char *str = esh_str_buff(esh, &len);
	if(esh_new_string(esh, str, len)) goto ERR;
	
	return 0;
	
	ERR:
	return 1;
}

void tm_to_iso_time(const struct tm *tm, iso_time *it, bool local_time) {
	*it = (iso_time) {
		.year = tm->tm_year + 1900,
		.month = tm->tm_mon + 1,
		.day = tm->tm_mday,
		.hour = tm->tm_hour,
		.minute = tm->tm_min,
		.second = tm->tm_sec,
		
		.local_time = local_time,
		.tz = 0
	};
}

void iso_time_to_tm(const iso_time *it, struct tm *tm) {
	*tm = (struct tm) {
		.tm_year = it->year - 1900,
		.tm_mon = it->month - 1,
		.tm_mday = it->day,
		.tm_hour = it->hour,
		.tm_min = it->minute,
		.tm_sec = it->second,
		
		.tm_isdst = it->local_time? -1 : 0
	};
}

bool is_leap_year(int year) {
	if(year % 4 != 0) return false;
	
	if(year % 100 == 0 && year % 400 != 0) return false;
	
	return true;
}

int days_in_month(int year, int month) {
	month = (month - 1) % 12;
	if(month < 0) month += 12;
	month += 1;
	
	switch(month) {
		case 1:
			return 31;
		case 2:
			if(is_leap_year(year)) return 29;
			return 28;
		case 3:
			return 31;
		case 4:
			return 30;
		case 5:
			return 31;
		case 6:
			return 30;
		case 7:
			return 31;
		case 8:
			return 31;
		case 9:
			return 30;
		case 10:
			return 31;
		case 11:
			return 30;
		case 12:
			return 31;
		
		default:
			return -1;
	}
}
