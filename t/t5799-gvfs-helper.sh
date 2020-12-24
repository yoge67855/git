#!/bin/sh

test_description='test gvfs-helper and GVFS Protocol'

. ./test-lib.sh

# Set the port for t/helper/test-gvfs-protocol.exe from either the
# environment or from the test number of this shell script.
#
test_set_port GIT_TEST_GVFS_PROTOCOL_PORT

# Setup the following repos:
#
#    repo_src:
#        A normal, no-magic, fully-populated clone of something.
#        No GVFS (aka VFS4G).  No Scalar.  No partial-clone.
#        This will be used by "t/helper/test-gvfs-protocol.exe"
#        to serve objects.
#
#    repo_t1:
#        An empty repo with no contents nor commits.  That is,
#        everything is missing.  For the tests based on this repo,
#        we don't care why it is missing objects (or if we could
#        actually use it).  We are only testing explicit object
#        fetching using gvfs-helper.exe in isolation.
#
REPO_SRC="$PWD"/repo_src
REPO_T1="$PWD"/repo_t1

# Setup some loopback URLs where test-gvfs-protocol.exe will be
# listening.  We will spawn it directly inside the repo_src directory,
# so we don't need any of the directory mapping or configuration
# machinery found in "git-daemon.exe" or "git-http-backend.exe".
#
# This lets us use the "uri-base" part of the URL (prior to the REST
# API "/gvfs/<token>") to control how our mock server responds.  For
# example, only the origin (main Git) server supports "/gvfs/config".
#
# For example, this means that if we add a remote containing $ORIGIN_URL,
# it will work with gvfs-helper, but not for fetch (without some mapping
# tricks).
#
HOST_PORT=127.0.0.1:$GIT_TEST_GVFS_PROTOCOL_PORT
ORIGIN_URL=http://$HOST_PORT/servertype/origin
CACHE_URL=http://$HOST_PORT/servertype/cache

SHARED_CACHE_T1="$PWD"/shared_cache_t1

# The pid-file is created by test-gvfs-protocol.exe when it starts.
# The server will shut down if/when we delete it.  (This is a little
# easier than killing it by PID.)
#
PID_FILE="$PWD"/pid-file.pid
SERVER_LOG="$PWD"/OUT.server.log

PATH="$GIT_BUILD_DIR/t/helper/:$PATH" && export PATH

OIDS_FILE="$PWD"/oid_list.txt
OIDS_CT_FILE="$PWD"/oid_ct_list.txt
OIDS_BLOBS_FILE="$PWD"/oids_blobs_file.txt
OID_ONE_BLOB_FILE="$PWD"/oid_one_blob_file.txt
OID_ONE_COMMIT_FILE="$PWD"/oid_one_commit_file.txt

# Get a list of available OIDs in repo_src so that we can try to fetch
# them and so that we don't have to hard-code a list of known OIDs.
# This doesn't need to be a complete list -- just enough to drive some
# representative tests.
#
# Optionally require that we find a minimum number of OIDs.
#
get_list_of_oids () {
	git -C "$REPO_SRC" rev-list --objects HEAD | sed 's/ .*//' | sort >"$OIDS_FILE"

	if test $# -eq 1
	then
		actual_nr=$(wc -l <"$OIDS_FILE")
		if test $actual_nr -lt $1
		then
			echo "get_list_of_oids: insufficient data.  Need $1 OIDs."
			return 1
		fi
	fi
	return 0
}

get_list_of_blobs_oids () {
	git -C "$REPO_SRC" ls-tree HEAD | grep ' blob ' | awk "{print \$3}" | sort >"$OIDS_BLOBS_FILE"
	head -1 <"$OIDS_BLOBS_FILE" >"$OID_ONE_BLOB_FILE"
}

get_list_of_commit_and_tree_oids () {
	git -C "$REPO_SRC" cat-file --batch-check --batch-all-objects | awk "/commit|tree/ {print \$1}" | sort >"$OIDS_CT_FILE"

	if test $# -eq 1
	then
		actual_nr=$(wc -l <"$OIDS_CT_FILE")
		if test $actual_nr -lt $1
		then
			echo "get_list_of_commit_and_tree_oids: insufficient data.  Need $1 OIDs."
			return 1
		fi
	fi
	return 0
}

get_one_commit_oid () {
	git -C "$REPO_SRC" rev-parse HEAD >"$OID_ONE_COMMIT_FILE"
	return 0
}

test_expect_success 'setup repos' '
	test_create_repo "$REPO_SRC" &&
	#
	# test_commit_bulk() does magic to create a packfile containing
	# the new commits.
	#
	test_commit_bulk -C "$REPO_SRC" --filename="batch_a.%s.t" 9 &&
	cp "$REPO_SRC"/.git/refs/heads/master m1.branch &&
	test_commit_bulk -C "$REPO_SRC" --filename="batch_b.%s.t" 9 &&
	cp "$REPO_SRC"/.git/refs/heads/master m2.branch &&
	#
	# test_commit() creates commits, trees, tags, and blobs and leave
	# them loose.
	#
	test_config gc.auto 0 &&
	#
	test_commit -C "$REPO_SRC" file1.txt &&
	test_commit -C "$REPO_SRC" file2.txt &&
	test_commit -C "$REPO_SRC" file3.txt &&
	test_commit -C "$REPO_SRC" file4.txt &&
	test_commit -C "$REPO_SRC" file5.txt &&
	test_commit -C "$REPO_SRC" file6.txt &&
	test_commit -C "$REPO_SRC" file7.txt &&
	test_commit -C "$REPO_SRC" file8.txt &&
	test_commit -C "$REPO_SRC" file9.txt &&
	cp "$REPO_SRC"/.git/refs/heads/master m3.branch &&
	#
	# gvfs-helper.exe writes downloaded objects to a shared-cache directory
	# rather than the ODB inside the .git directory.
	#
	mkdir "$SHARED_CACHE_T1" &&
	mkdir "$SHARED_CACHE_T1/pack" &&
	mkdir "$SHARED_CACHE_T1/info" &&
	#
	# setup repo_t1 and point all of the gvfs.* values to repo_src.
	#
	test_create_repo "$REPO_T1" &&
	git -C "$REPO_T1" remote add origin $ORIGIN_URL &&
	git -C "$REPO_T1" config --local gvfs.cache-server $CACHE_URL &&
	git -C "$REPO_T1" config --local gvfs.sharedcache "$SHARED_CACHE_T1" &&
	echo "$SHARED_CACHE_T1" >> "$REPO_T1"/.git/objects/info/alternates &&
	#
	#
	#
	cat <<-EOF >creds.txt &&
		username=x
		password=y
	EOF
	cat <<-EOF >creds.sh &&
		#!/bin/sh
		cat "$PWD"/creds.txt
	EOF
	chmod 755 creds.sh &&
	git -C "$REPO_T1" config --local credential.helper "!f() { cat \"$PWD\"/creds.txt; }; f" &&
	#
	# Create some test data sets.
	#
	get_list_of_oids 30 &&
	get_list_of_commit_and_tree_oids 30 &&
	get_list_of_blobs_oids &&
	get_one_commit_oid
'

stop_gvfs_protocol_server () {
	if ! test -f "$PID_FILE"
	then
		return 0
	fi
	#
	# The server will shutdown automatically when we delete the pid-file.
	#
	rm -f "$PID_FILE"
	#
	# Give it a few seconds to shutdown (mainly to completely release the
	# port before the next test start another instance and it attempts to
	# bind to it).
	#
	for k in 0 1 2 3 4
	do
		if grep -q "Starting graceful shutdown" "$SERVER_LOG"
		then
			return 0
		fi
		sleep 1
	done

	echo "stop_gvfs_protocol_server: timeout waiting for server shutdown"
	return 1
}

start_gvfs_protocol_server () {
	#
	# Launch our server into the background in repo_src.
	#
	(
		cd "$REPO_SRC"
		test-gvfs-protocol --verbose \
			--listen=127.0.0.1 \
			--port=$GIT_TEST_GVFS_PROTOCOL_PORT \
			--reuseaddr \
			--pid-file="$PID_FILE" \
			2>"$SERVER_LOG" &
	)
	#
	# Give it a few seconds to get started.
	#
	for k in 0 1 2 3 4
	do
		if test -f "$PID_FILE"
		then
			return 0
		fi
		sleep 1
	done

	echo "start_gvfs_protocol_server: timeout waiting for server startup"
	return 1
}

start_gvfs_protocol_server_with_mayhem () {
	if test $# -lt 1
	then
		echo "start_gvfs_protocol_server_with_mayhem: need mayhem args"
		return 1
	fi

	mayhem=""
	for k in $*
	do
		mayhem="$mayhem --mayhem=$k"
	done
	#
	# Launch our server into the background in repo_src.
	#
	(
		cd "$REPO_SRC"
		test-gvfs-protocol --verbose \
			--listen=127.0.0.1 \
			--port=$GIT_TEST_GVFS_PROTOCOL_PORT \
			--reuseaddr \
			--pid-file="$PID_FILE" \
			$mayhem \
			2>"$SERVER_LOG" &
	)
	#
	# Give it a few seconds to get started.
	#
	for k in 0 1 2 3 4
	do
		if test -f "$PID_FILE"
		then
			return 0
		fi
		sleep 1
	done

	echo "start_gvfs_protocol_server: timeout waiting for server startup"
	return 1
}

# Verify the number of connections from the client.
#
# If keep-alive is working, a series of successful sequential requests to the
# same server should use the same TCP connection, so a simple multi-get would
# only have one connection.
#
# On the other hand, an auto-retry after a network error (mayhem) will have
# more than one for a single object request.
#
# TODO This may generate false alarm when we get to complicated tests, so
# TODO we might only want to use it for basic tests.
#
verify_connection_count () {
	if test $# -eq 1
	then
		expected_nr=$1
	else
		expected_nr=1
	fi

	actual_nr=$(grep -c "Connection from" "$SERVER_LOG")

	if test $actual_nr -ne $expected_nr
	then
		echo "verify_keep_live: expected $expected_nr; actual $actual_nr"
		return 1
	fi
	return 0
}

# Verify that the set of requested objects are present in
# the shared-cache and that there is no corruption.  We use
# cat-file to hide whether the object is packed or loose in
# the test repo.
#
# Usage: <pathname_to_file_of_oids>
#
verify_objects_in_shared_cache () {
	#
	# See if any of the objects are missing from repo_t1.
	#
	git -C "$REPO_T1" cat-file --batch-check <"$1" >OUT.bc_actual || return 1
	grep -q " missing" OUT.bc_actual && return 1
	#
	# See if any of the objects have different sizes or types than repo_src.
	#
	git -C "$REPO_SRC" cat-file --batch-check <"$1" >OUT.bc_expect || return 1
	test_cmp OUT.bc_expect OUT.bc_actual || return 1
	#
	# See if any of the objects are corrupt in repo_t1.  This fully
	# reconstructs the objects and verifies the hash and therefore
	# detects corruption not found by the earlier "batch-check" step.
	#
	git -C "$REPO_T1" cat-file --batch <"$1" >OUT.b_actual || return 1
	#
	# TODO move the shared-cache directory (and/or the
	# TODO .git/objects/info/alternates and temporarily unset
	# TODO gvfs.sharedcache) and repeat the first "batch-check"
	# TODO and make sure that they are ALL missing.
	#
	return 0
}

verify_received_packfile_count () {
	if test $# -eq 1
	then
		expected_nr=$1
	else
		expected_nr=1
	fi

	actual_nr=$(grep -c "packfile " <OUT.output)

	if test $actual_nr -ne $expected_nr
	then
		echo "verify_received_packfile_count: expected $expected_nr; actual $actual_nr"
		return 1
	fi
	return 0
}

per_test_cleanup () {
	stop_gvfs_protocol_server

	rm -rf "$SHARED_CACHE_T1"/[0-9a-f][0-9a-f]/
	rm -rf "$SHARED_CACHE_T1"/info/*
	rm -rf "$SHARED_CACHE_T1"/pack/*

	rm -rf OUT.*
	return 0
}

#################################################################
# Basic tests to confirm the happy path works.
#################################################################

test_expect_success 'basic: GET origin multi-get no-auth' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server &&

	# Connect to the origin server (w/o auth) and make a series of
	# single-object GET requests.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		<"$OIDS_FILE" >OUT.output &&

	# Stop the server to prevent the verification steps from faulting-in
	# any missing objects.
	#
	stop_gvfs_protocol_server &&

	# gvfs-helper prints a "loose <oid>" message for each received object.
	# Verify that gvfs-helper received each of the requested objects.
	#
	sed "s/loose //" <OUT.output | sort >OUT.actual &&
	test_cmp "$OIDS_FILE" OUT.actual &&

	verify_objects_in_shared_cache "$OIDS_FILE" &&
	verify_connection_count 1
'

test_expect_success 'basic: GET cache-server multi-get trust-mode' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server &&

	# Connect to the cache-server and make a series of
	# single-object GET requests.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=trust \
		--remote=origin \
		get \
		<"$OIDS_FILE" >OUT.output &&

	# Stop the server to prevent the verification steps from faulting-in
	# any missing objects.
	#
	stop_gvfs_protocol_server &&

	# gvfs-helper prints a "loose <oid>" message for each received object.
	# Verify that gvfs-helper received each of the requested objects.
	#
	sed "s/loose //" <OUT.output | sort >OUT.actual &&
	test_cmp "$OIDS_FILE" OUT.actual &&

	verify_objects_in_shared_cache "$OIDS_FILE" &&
	verify_connection_count 1
'

test_expect_success 'basic: GET gvfs/config' '
#	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server &&

	# Connect to the cache-server and make a series of
	# single-object GET requests.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		config \
		<"$OIDS_FILE" >OUT.output &&

	# Stop the server to prevent the verification steps from faulting-in
	# any missing objects.
	#
	stop_gvfs_protocol_server &&

	# The cache-server URL should be listed in the gvfs/config output.
	# We confirm this before assuming error-mode will work.
	#
	grep -q "$CACHE_URL" OUT.output
'

test_expect_success 'basic: GET cache-server multi-get error-mode' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server &&

	# Connect to the cache-server and make a series of
	# single-object GET requests.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=error \
		--remote=origin \
		get \
		<"$OIDS_FILE" >OUT.output &&

	# Stop the server to prevent the verification steps from faulting-in
	# any missing objects.
	#
	stop_gvfs_protocol_server &&

	# gvfs-helper prints a "loose <oid>" message for each received object.
	# Verify that gvfs-helper received each of the requested objects.
	#
	sed "s/loose //" <OUT.output | sort >OUT.actual &&
	test_cmp "$OIDS_FILE" OUT.actual &&

	verify_objects_in_shared_cache "$OIDS_FILE" &&

	# Technically, we have 1 connection to the origin server
	# for the "gvfs/config" request and 1 to cache server to
	# get the objects, but because we are using the same port
	# for both, keep-alive will handle it.  So 1 connection.
	#
	verify_connection_count 1
'

# The GVFS Protocol POST verb behaves like GET for non-commit objects
# (in that it just returns the requested object), but for commit
# objects POST *also* returns all trees referenced by the commit.
#
# The goal of this test is to confirm that gvfs-helper can send us
# a packfile at all.  So, this test only passes blobs to not blur
# the issue.
#
test_expect_success 'basic: POST origin blobs' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server &&

	# Connect to the origin server (w/o auth) and make
	# multi-object POST request.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		--no-progress \
		post \
		<"$OIDS_BLOBS_FILE" >OUT.output &&

	# Stop the server to prevent the verification steps from faulting-in
	# any missing objects.
	#
	stop_gvfs_protocol_server &&

	# gvfs-helper prints a "packfile <path>" message for each received
	# packfile.  We verify the number of expected packfile(s) and we
	# individually verify that each requested object is present in the
	# shared cache (and index-pack already verified the integrity of
	# the packfile), so we do not bother to run "git verify-pack -v"
	# and do an exact matchup here.
	#
	verify_received_packfile_count 1 &&

	verify_objects_in_shared_cache "$OIDS_BLOBS_FILE" &&
	verify_connection_count 1
'

# Request a single blob via POST.  Per the GVFS Protocol, the server
# should implicitly send a loose object for it.  Confirm that.
#
test_expect_success 'basic: POST-request a single blob' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server &&

	# Connect to the origin server (w/o auth) and request a single
	# blob via POST.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		--no-progress \
		post \
		<"$OID_ONE_BLOB_FILE" >OUT.output &&

	# Stop the server to prevent the verification steps from faulting-in
	# any missing objects.
	#
	stop_gvfs_protocol_server &&

	# gvfs-helper prints a "loose <oid>" message for each received
	# loose object.
	#
	sed "s/loose //" <OUT.output | sort >OUT.actual &&
	test_cmp "$OID_ONE_BLOB_FILE" OUT.actual &&

	verify_connection_count 1
'

# Request a single commit via POST.  Per the GVFS Protocol, the server
# should implicitly send us a packfile containing the commit and the
# trees it references.  Confirm that properly handled the receipt of
# the packfile.  (Here, we are testing that asking for a single object
# yields a packfile rather than a loose object.)
#
# We DO NOT verify that the packfile contains commits/trees and no blobs
# because our test helper doesn't implement the filtering.
#
test_expect_success 'basic: POST-request a single commit' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server &&

	# Connect to the origin server (w/o auth) and request a single
	# commit via POST.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		--no-progress \
		post \
		<"$OID_ONE_COMMIT_FILE" >OUT.output &&

	# Stop the server to prevent the verification steps from faulting-in
	# any missing objects.
	#
	stop_gvfs_protocol_server &&

	# gvfs-helper prints a "packfile <path>" message for each received
	# packfile.
	#
	verify_received_packfile_count 1 &&

	verify_connection_count 1
'

#################################################################
# Tests to see how gvfs-helper responds to network problems.
#
# We use small --max-retry value because of exponential backoff.
#
# These mayhem tests are interested in how gvfs-helper gracefully
# retries when there is a network error.  And verify that it gives
# up gracefully too.
#################################################################

mayhem_observed__close__connections () {
	if $(grep -q "transient" OUT.stderr)
	then
		# Transient errors should retry.
		# 1 for initial request + 2 retries.
		#
		verify_connection_count 3
		return $?
	elif $(grep -q "hard_fail" OUT.stderr)
	then
		# Hard errors should not retry.
		#
		verify_connection_count 1
		return $?
	else
		error "mayhem_observed__close: unexpected mayhem-induced error type"
		return 1
	fi
}

mayhem_observed__close () {
	# Expected error codes for mayhem events:
	#     close_read
	#     close_write
	#     close_no_write
	#
	# CURLE_PARTIAL_FILE 18
	# CURLE_GOT_NOTHING 52
	# CURLE_SEND_ERROR 55
	# CURLE_RECV_ERROR 56
	#
	# I don't want to pin it down to an exact error for each because there may
	# be races here because of network buffering.
	#
	# Also, It is unclear which of these network errors should be transient
	# (with retry) and which should be a hard-fail (without retry).  I'm only
	# going to verify the connection counts based upon what type of error
	# gvfs-helper claimed it to be.
	#
	if      $(grep -q "error: get: (curl:18)" OUT.stderr) ||
		$(grep -q "error: get: (curl:52)" OUT.stderr) ||
		$(grep -q "error: get: (curl:55)" OUT.stderr) ||
		$(grep -q "error: get: (curl:56)" OUT.stderr)
	then
		mayhem_observed__close__connections
		return $?
	else
		echo "mayhem_observed__close: unexpected mayhem-induced error"
		return 1
	fi
}

test_expect_success 'curl-error: no server' '
	test_when_finished "per_test_cleanup" &&

	# Try to do a multi-get without a server.
	#
	# Use small max-retry value because of exponential backoff,
	# but yet do exercise retry some.
	#
	test_must_fail \
		git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		--max-retries=2 \
		<"$OIDS_FILE" >OUT.output 2>OUT.stderr &&

	# CURLE_COULDNT_CONNECT 7
	grep -q "error: get: (curl:7)" OUT.stderr
'

test_expect_success 'curl-error: close socket while reading request' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server_with_mayhem close_read &&

	test_must_fail \
		git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		--max-retries=2 \
		<"$OIDS_FILE" >OUT.output 2>OUT.stderr &&

	stop_gvfs_protocol_server &&

	mayhem_observed__close
'

test_expect_success 'curl-error: close socket while writing response' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server_with_mayhem close_write &&

	test_must_fail \
		git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		--max-retries=2 \
		<"$OIDS_FILE" >OUT.output 2>OUT.stderr &&

	stop_gvfs_protocol_server &&

	mayhem_observed__close
'

test_expect_success 'curl-error: close socket before writing response' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server_with_mayhem close_no_write &&

	test_must_fail \
		git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		--max-retries=2 \
		<"$OIDS_FILE" >OUT.output 2>OUT.stderr &&

	stop_gvfs_protocol_server &&

	mayhem_observed__close
'

#################################################################
# Tests to confirm that gvfs-helper does silently recover when
# a retry succeeds.
#
# Note: I'm only to do this for 1 of the close_* mayhem events.
#################################################################

test_expect_success 'successful retry after curl-error: origin get' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server_with_mayhem close_read_1 &&

	# Connect to the origin server (w/o auth).
	# Make a single-object GET request.
	# Confirm that it succeeds without error.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		--max-retries=2 \
		<"$OID_ONE_BLOB_FILE" >OUT.output &&

	stop_gvfs_protocol_server &&

	# gvfs-helper prints a "loose <oid>" message for each received object.
	# Verify that gvfs-helper received each of the requested objects.
	#
	sed "s/loose //" <OUT.output | sort >OUT.actual &&
	test_cmp "$OID_ONE_BLOB_FILE" OUT.actual &&

	verify_objects_in_shared_cache "$OID_ONE_BLOB_FILE" &&
	verify_connection_count 2
'

#################################################################
# Tests to see how gvfs-helper responds to HTTP errors/problems.
#
#################################################################

# See "enum gh__error_code" in gvfs-helper.c
#
GH__ERROR_CODE__HTTP_404=4
GH__ERROR_CODE__HTTP_429=5
GH__ERROR_CODE__HTTP_503=6

test_expect_success 'http-error: 503 Service Unavailable (with retry)' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server_with_mayhem http_503 &&

	test_expect_code $GH__ERROR_CODE__HTTP_503 \
		git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		--max-retries=2 \
		<"$OIDS_FILE" >OUT.output 2>OUT.stderr &&

	stop_gvfs_protocol_server &&

	grep -q "error: get: (http:503)" OUT.stderr &&
	verify_connection_count 3
'

test_expect_success 'http-error: 429 Service Unavailable (with retry)' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server_with_mayhem http_429 &&

	test_expect_code $GH__ERROR_CODE__HTTP_429 \
		git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		--max-retries=2 \
		<"$OIDS_FILE" >OUT.output 2>OUT.stderr &&

	stop_gvfs_protocol_server &&

	grep -q "error: get: (http:429)" OUT.stderr &&
	verify_connection_count 3
'

test_expect_success 'http-error: 404 Not Found (no retry)' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server_with_mayhem http_404 &&

	test_expect_code $GH__ERROR_CODE__HTTP_404 \
		git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		--max-retries=2 \
		<"$OID_ONE_BLOB_FILE" >OUT.output 2>OUT.stderr &&

	stop_gvfs_protocol_server &&

	grep -q "error: get: (http:404)" OUT.stderr &&
	verify_connection_count 1
'

#################################################################
# Tests to confirm that gvfs-helper does silently recover when an
# HTTP request succeeds after a failure.
#
#################################################################

test_expect_success 'successful retry after http-error: origin get' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server_with_mayhem http_429_1 &&

	# Connect to the origin server (w/o auth).
	# Make a single-object GET request.
	# Confirm that it succeeds without error.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		--max-retries=2 \
		<"$OID_ONE_BLOB_FILE" >OUT.output &&

	stop_gvfs_protocol_server &&

	# gvfs-helper prints a "loose <oid>" message for each received object.
	# Verify that gvfs-helper received each of the requested objects.
	#
	sed "s/loose //" <OUT.output | sort >OUT.actual &&
	test_cmp "$OID_ONE_BLOB_FILE" OUT.actual &&

	verify_objects_in_shared_cache "$OID_ONE_BLOB_FILE" &&
	verify_connection_count 2
'

#################################################################
# Test HTTP Auth
#
#################################################################

test_expect_success 'HTTP GET Auth on Origin Server' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server_with_mayhem http_401 &&

	# Force server to require auth.
	# Connect to the origin server without auth.
	# Make a single-object GET request.
	# Confirm that it gets a 401 and then retries with auth.
	#
	GIT_CONFIG_NOSYSTEM=1 \
		git -C "$REPO_T1" gvfs-helper \
			--cache-server=disable \
			--remote=origin \
			get \
			--max-retries=2 \
			<"$OID_ONE_BLOB_FILE" >OUT.output &&

	stop_gvfs_protocol_server &&

	# gvfs-helper prints a "loose <oid>" message for each received object.
	# Verify that gvfs-helper received each of the requested objects.
	#
	sed "s/loose //" <OUT.output | sort >OUT.actual &&
	test_cmp "$OID_ONE_BLOB_FILE" OUT.actual &&

	verify_objects_in_shared_cache "$OID_ONE_BLOB_FILE" &&
	verify_connection_count 2
'

test_expect_success 'HTTP POST Auth on Origin Server' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server_with_mayhem http_401 &&

	# Connect to the origin server and make multi-object POST
	# request and verify that it automatically handles the 401.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		--no-progress \
		post \
		<"$OIDS_BLOBS_FILE" >OUT.output &&

	# Stop the server to prevent the verification steps from faulting-in
	# any missing objects.
	#
	stop_gvfs_protocol_server &&

	# gvfs-helper prints a "packfile <path>" message for each received
	# packfile.  We verify the number of expected packfile(s) and we
	# individually verify that each requested object is present in the
	# shared cache (and index-pack already verified the integrity of
	# the packfile), so we do not bother to run "git verify-pack -v"
	# and do an exact matchup here.
	#
	verify_received_packfile_count 1 &&

	verify_objects_in_shared_cache "$OIDS_BLOBS_FILE" &&
	verify_connection_count 2
'

test_expect_success 'HTTP GET Auth on Cache Server' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server_with_mayhem http_401 &&

	# Try auth to cache-server.  Note that gvfs-helper *ALWAYS* sends
	# creds to cache-servers, so we will never see the "400 Bad Request"
	# response.  And we are using "trust" mode, so we only expect 1
	# connection to the server.
	#
	GIT_CONFIG_NOSYSTEM=1 \
		git -C "$REPO_T1" gvfs-helper \
			--cache-server=trust \
			--remote=origin \
			get \
			--max-retries=2 \
			<"$OID_ONE_BLOB_FILE" >OUT.output &&

	stop_gvfs_protocol_server &&

	# gvfs-helper prints a "loose <oid>" message for each received object.
	# Verify that gvfs-helper received each of the requested objects.
	#
	sed "s/loose //" <OUT.output | sort >OUT.actual &&
	test_cmp "$OID_ONE_BLOB_FILE" OUT.actual &&

	verify_objects_in_shared_cache "$OID_ONE_BLOB_FILE" &&
	verify_connection_count 1
'

#################################################################
# Integration tests with Git.exe
#
# Now that we have confirmed that gvfs-helper works in isolation,
# run a series of tests using random Git commands that fault-in
# objects as needed.
#
# At this point, I'm going to stop verifying the shape of the ODB
# (loose vs packfiles) and the number of connections required to
# get them.  The tests from here on are to verify that objects are
# magically fetched whenever required.
#################################################################

test_expect_success 'integration: explicit commit/trees, implicit blobs: log file' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server &&

	# We have a very empty repo.  Seed it with all of the commits
	# and trees.  The purpose of this test is to demand-load the
	# needed blobs only, so we prefetch the commits and trees.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		<"$OIDS_CT_FILE" >OUT.output &&

	# Confirm that we do not have the blobs locally.
	# With gvfs-helper turned off, we should fail.
	#
	test_must_fail \
		git -C "$REPO_T1" -c core.usegvfshelper=false \
			log $(cat m3.brach) -- file9.txt \
			>OUT.output 2>OUT.stderr &&

	# Turn on gvfs-helper and retry.  This should implicitly fetch
	# any needed blobs.
	#
	git -C "$REPO_T1" -c core.usegvfshelper=true \
		log $(cat m3.branch) -- file9.txt \
		>OUT.output 2>OUT.stderr &&

	# Verify that gvfs-helper wrote the fetched the blobs to the
	# local ODB, such that a second attempt with gvfs-helper
	# turned off should succeed.
	#
	git -C "$REPO_T1" -c core.usegvfshelper=false \
		log $(cat m3.branch) -- file9.txt \
		>OUT.output 2>OUT.stderr
'

test_expect_success 'integration: explicit commit/trees, implicit blobs: diff 2 commits' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server &&

	# We have a very empty repo.  Seed it with all of the commits
	# and trees.  The purpose of this test is to demand-load the
	# needed blobs only, so we prefetch the commits and trees.
	#
	git -C "$REPO_T1" gvfs-helper \
		--cache-server=disable \
		--remote=origin \
		get \
		<"$OIDS_CT_FILE" >OUT.output &&

	# Confirm that we do not have the blobs locally.
	# With gvfs-helper turned off, we should fail.
	#
	test_must_fail \
		git -C "$REPO_T1" -c core.usegvfshelper=false \
			diff $(cat m1.branch)..$(cat m3.branch) \
			>OUT.output 2>OUT.stderr &&

	# Turn on gvfs-helper and retry.  This should implicitly fetch
	# any needed blobs.
	#
	git -C "$REPO_T1" -c core.usegvfshelper=true \
		diff $(cat m1.branch)..$(cat m3.branch) \
		>OUT.output 2>OUT.stderr &&

	# Verify that gvfs-helper wrote the fetched the blobs to the
	# local ODB, such that a second attempt with gvfs-helper
	# turned off should succeed.
	#
	git -C "$REPO_T1" -c core.usegvfshelper=false \
		diff $(cat m1.branch)..$(cat m3.branch) \
		>OUT.output 2>OUT.stderr
'

test_expect_success 'integration: fully implicit: diff 2 commits' '
	test_when_finished "per_test_cleanup" &&
	start_gvfs_protocol_server &&

	# Implicitly demand-load everything without any pre-seeding.
	#
	git -C "$REPO_T1" -c core.usegvfshelper=true \
		diff $(cat m1.branch)..$(cat m3.branch) \
		>OUT.output 2>OUT.stderr
'

test_done
