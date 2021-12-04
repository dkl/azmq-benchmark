#!/bin/bash -eu

test_rev_buffersize() {
	local rev="$1"
	local buffersize="$2"

	echo
	echo "testing commit: $(git log --oneline -1 $rev)"
	echo "buffer size: $buffersize"

	git checkout "$rev" >/dev/null
	# Abort if working dir not clean
	[ -z "$(git diff)" ]

	echo "compiling"
	clang++ -std=c++17 -Wall -Wextra -O2 -pthread iobench.cpp -o iobench -lzmq -lboost_regex

	for i in $(seq 1 100); do
		echo "run $i"

		#./iobench recv &
		#sender_pid=$!
		#timeout 30s ./iobench send || true
		#kill -TERM $sender_pid
		#wait || true

		timeout 10s ./iobench both "$buffersize" || true
	done
}

# original master
GIT_REV_ORIGINAL=6675a0ab6d8442fc35648f566b748bde97d764f2
# with leak fix
GIT_REV_LEAKFIX=1a53ed375070d86d25cddb3b54c16a4552e77c24
# with leak fix and refactoring
GIT_REV_LEAKFIX_REFACTOR=15a4f45ee92f57ed1938b885bcba876b97ba5967

test_buffersize() {
	local buffersize="$1"
	test_rev_buffersize "$GIT_REV_ORIGINAL" "$buffersize"
	test_rev_buffersize "$GIT_REV_LEAKFIX" "$buffersize"
	test_rev_buffersize "$GIT_REV_LEAKFIX_REFACTOR" "$buffersize"
	test_rev_buffersize "$GIT_REV_LEAKFIX" "$buffersize"
	test_rev_buffersize "$GIT_REV_ORIGINAL" "$buffersize"
}

(
	test_buffersize 8
	test_buffersize 1024
	test_buffersize $((1024 * 1024))
	test_buffersize $((10 * 1024 * 1024))
) 2>&1 | tee log.txt

./parse_log.py log.txt
