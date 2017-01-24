#!/bin/sh

test_description="Tests performance of sparse-checkout"

. ./perf-lib.sh

test_perf_large_repo

test_expect_success 'setup sparse-checkout' '
	git config --local --bool core.sparsecheckout true &&
	git ls-files > .git/info/sparse-checkout
'

test_perf 'perform normal sparse checkout of master' '
	git checkout master
'

test_expect_success 'setup for hashmap sparse-checkout' '
	git config --local core.gvfs 2
'

test_perf 'perform hashmap sparse checkout of master' '
	git checkout master
'

test_done
