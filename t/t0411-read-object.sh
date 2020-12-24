#!/bin/sh

test_description='tests for long running read-object process'

. ./test-lib.sh

test_expect_success 'setup host repo with a root commit' '
	test_commit zero &&
	hash1=$(git ls-tree HEAD | grep zero.t | cut -f1 | cut -d\  -f3)
'

test_expect_success 'blobs can be retrieved from the host repo' '
	git init guest-repo &&
	(cd guest-repo &&
	 mkdir -p .git/hooks &&
	 cp $TEST_DIRECTORY/t0410/read-object .git/hooks/ &&
	 git config core.virtualizeobjects true &&
	 git cat-file blob "$hash1")
'

test_expect_success 'invalid blobs generate errors' '
	(cd guest-repo &&
	 test_must_fail git cat-file blob "invalid")
'


test_done
