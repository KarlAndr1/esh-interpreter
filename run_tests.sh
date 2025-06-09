#!/usr/bin/env sh

tests_dir="tests"
src_dir="src"
interpreter="./bin/esh --gc-freq 1"

proj_objs="$(find "$src_dir" -name '*.o' -not -name 'main.o')"

passed=0
total=0

run_test_module() {
	i=0
	
	dst="tmp/main.c"
	bin="./tmp/testbin"
	log="tmp/log"
	
	echo "#include <stdlib.h>" > "$dst"
	echo "#include <stdio.h>" >> "$dst"
	echo "int main(int argc, const char **argv) {" >> "$dst"
	echo "	if(argc != 2) return 1;" >> "$dst"
	echo "	switch(atoi(argv[1])) {" >> "$dst"
	for t in $(grep -o 'test_\w*' "$1"); do
		printf '		case %i: { extern void %s(); fputs("\t%s: ", stdout); fflush(stdout); %s(); break; }\n' "$i" "$t" "$t" "$t" >> "$dst"
		i=$((i + 1))
	done
	echo "	}" >> "$dst"
	echo "}" >> "$dst"
	
	"$CC" $CFLAGS $proj_objs "$dst" "$1" -o "$bin"
	
	j=0
	while [ $j -lt $i ]; do
		prefix=""
		if [ "$(basename $1):$j" = "$DEBUG_TEST" ]; then
			prefix="gdb --args"
		fi
		
		if $prefix "$bin" $j 2> "$log"; then
			echo "OK"
			passed=$((passed + 1))
		else
			echo "FAILED"
			cat "$log" | sed 's/^/\t\t/'
		fi
		j=$((j + 1))
		total=$((total + 1))
	done
}

echo "Running tests in $tests_dir"

for f in $(find "$tests_dir" -name '*.c' -printf '%P\n'); do
	echo $f
	run_test_module "$tests_dir/$f"
done

for f in $(find "$tests_dir" -name '*.esh' -printf '%P\n'); do
	printf '%s: ' "$f"
	if $interpreter "$tests_dir/$f" 2> "$log"; then
		echo "OK"
		passed=$((passed + 1))
	else
		echo "FAILED"
		cat "$log" | sed 's/^/\t\t/'
	fi
	j=$((j + 1))
	total=$((total + 1))
done

echo "$passed/$total"
