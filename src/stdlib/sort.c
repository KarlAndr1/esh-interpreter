#include "sort.h"

#include <assert.h>

int make_heap(esh_state *esh, long long array, size_t len, size_t start, int (*cmp)(esh_state *esh), bool reverse) {
	size_t at = start;
	while(true) {
		size_t a = at * 2 + 1;
		size_t b = at * 2 + 2;
		
		if(a >= len) break;	

		if(esh_index_i(esh, array, at)) return -1;
		if(esh_index_i(esh, array, a)) return -1;
		
		long long selected = -1;
		
		int cmp_res = cmp(esh);
		
		if(cmp_res == -1) return -1;
		if(cmp_res != reverse) selected = a;
		
		if(b < len) {
			if(selected == -1) esh_pop(esh, 1);
			if(esh_index_i(esh, array, b)) return -1;
			int cmp_res = cmp(esh);
			if(selected == -1) esh_pop(esh, 2);
			else esh_pop(esh, 3);
			
			if(cmp_res == -1) return -1;
			if(cmp_res != reverse) selected = b;
		} else {
			esh_pop(esh, 2);
		}
		
		if(selected != -1) {
			if(esh_index_i(esh, array, selected)) return 1;
			if(esh_index_i(esh, array, at)) return 1;
			
			if(esh_set_i(esh, array, selected, -1)) return 1;
			if(esh_set_i(esh, array, at, -2)) return 1;
			esh_pop(esh, 2);
			
			at = selected;
		} else {
			break;
		}
	}
	
	return 0;
}

int esh_sort(esh_state *esh, long long array, size_t len, int (*cmp)(esh_state *esh), bool reverse) {
	esh_save_stack(esh);
	
	for(size_t i = (len - 2) / 2; i > 0; i--) {
		if(make_heap(esh, array, len, i, cmp, reverse)) goto ERR;
	}
	if(make_heap(esh, array, len, 0, cmp, reverse)) goto ERR;
	
	for(size_t i = 1; i < len ; i++) {
		size_t index = len - i;
		if(esh_index_i(esh, array, index)) goto ERR;
		if(esh_index_i(esh, array, 0)) goto ERR;
		if(esh_set_i(esh, array, index, -1)) goto ERR;
		if(esh_set_i(esh, array, 0, -2)) goto ERR;
		esh_pop(esh, 2);
		
		if(make_heap(esh, array, len - i, 0, cmp, reverse)) goto ERR;
	}
	
	return 0;
	
	ERR:
	esh_restore_stack(esh);
	return 1;
}
