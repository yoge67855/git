#include "cache.h"
#include "strvec.h"
#include "trace2.h"
#include "oidset.h"
#include "object.h"
#include "object-store.h"
#include "gvfs-helper-client.h"
#include "sub-process.h"
#include "sigchain.h"
#include "pkt-line.h"
#include "quote.h"
#include "packfile.h"

static struct oidset gh_client__oidset_queued = OIDSET_INIT;
static unsigned long gh_client__oidset_count;

struct gh_server__process {
	struct subprocess_entry subprocess; /* must be first */
	unsigned int supported_capabilities;
};

static int gh_server__subprocess_map_initialized;
static struct hashmap gh_server__subprocess_map;
static struct object_directory *gh_client__chosen_odb;

/*
 * The "objects" capability has verbs: "get" and "post" and "prefetch".
 */
#define CAP_OBJECTS      (1u<<1)
#define CAP_OBJECTS_NAME "objects"

#define CAP_OBJECTS__VERB_GET1_NAME "get"
#define CAP_OBJECTS__VERB_POST_NAME "post"
#define CAP_OBJECTS__VERB_PREFETCH_NAME "prefetch"

static int gh_client__start_fn(struct subprocess_entry *subprocess)
{
	static int versions[] = {1, 0};
	static struct subprocess_capability capabilities[] = {
		{ CAP_OBJECTS_NAME, CAP_OBJECTS },
		{ NULL, 0 }
	};

	struct gh_server__process *entry = (struct gh_server__process *)subprocess;

	return subprocess_handshake(subprocess, "gvfs-helper", versions,
				    NULL, capabilities,
				    &entry->supported_capabilities);
}

/*
 * Send the queued OIDs in the OIDSET to gvfs-helper for it to
 * fetch from the cache-server or main Git server using "/gvfs/objects"
 * POST semantics.
 *
 *     objects.post LF
 *     (<hex-oid> LF)*
 *     <flush>
 *
 */
static int gh_client__send__objects_post(struct child_process *process)
{
	struct oidset_iter iter;
	struct object_id *oid;
	int err;

	/*
	 * We assume that all of the packet_ routines call error()
	 * so that we don't have to.
	 */

	err = packet_write_fmt_gently(
		process->in,
		(CAP_OBJECTS_NAME "." CAP_OBJECTS__VERB_POST_NAME "\n"));
	if (err)
		return err;

	oidset_iter_init(&gh_client__oidset_queued, &iter);
	while ((oid = oidset_iter_next(&iter))) {
		err = packet_write_fmt_gently(process->in, "%s\n",
					      oid_to_hex(oid));
		if (err)
			return err;
	}

	err = packet_flush_gently(process->in);
	if (err)
		return err;

	return 0;
}

/*
 * Send the given OID to gvfs-helper for it to fetch from the
 * cache-server or main Git server using "/gvfs/objects" GET
 * semantics.
 *
 * This ignores any queued OIDs.
 *
 *     objects.get LF
 *     <hex-oid> LF
 *     <flush>
 *
 */
static int gh_client__send__objects_get(struct child_process *process,
					const struct object_id *oid)
{
	int err;

	/*
	 * We assume that all of the packet_ routines call error()
	 * so that we don't have to.
	 */

	err = packet_write_fmt_gently(
		process->in,
		(CAP_OBJECTS_NAME "." CAP_OBJECTS__VERB_GET1_NAME "\n"));
	if (err)
		return err;

	err = packet_write_fmt_gently(process->in, "%s\n",
				      oid_to_hex(oid));
	if (err)
		return err;

	err = packet_flush_gently(process->in);
	if (err)
		return err;

	return 0;
}

/*
 * Send a request to gvfs-helper to prefetch packfiles from either the
 * cache-server or the main Git server using "/gvfs/prefetch".
 *
 *     objects.prefetch LF
 *     [<seconds-since_epoch> LF]
 *     <flush>
 */
static int gh_client__send__objects_prefetch(struct child_process *process,
					     timestamp_t seconds_since_epoch)
{
	int err;

	/*
	 * We assume that all of the packet_ routines call error()
	 * so that we don't have to.
	 */

	err = packet_write_fmt_gently(
		process->in,
		(CAP_OBJECTS_NAME "." CAP_OBJECTS__VERB_PREFETCH_NAME "\n"));
	if (err)
		return err;

	if (seconds_since_epoch) {
		err = packet_write_fmt_gently(process->in, "%" PRItime "\n",
					      seconds_since_epoch);
		if (err)
			return err;
	}

	err = packet_flush_gently(process->in);
	if (err)
		return err;

	return 0;
}

/*
 * Verify that the pathname found in the "odb" response line matches
 * what we requested.
 *
 * Since we ALWAYS send a "--shared-cache=<path>" arg to "gvfs-helper",
 * we should be able to verify that the value is what we requested.
 * In particular, I don't see a need to try to search for the response
 * value in from our list of alternates.
 */
static void gh_client__verify_odb_line(const char *line)
{
	const char *v1_odb_path;

	if (!skip_prefix(line, "odb ", &v1_odb_path))
		BUG("verify_odb_line: invalid line '%s'", line);

	if (!gh_client__chosen_odb ||
	    strcmp(v1_odb_path, gh_client__chosen_odb->path))
		BUG("verify_odb_line: unexpeced odb path '%s' vs '%s'",
		    v1_odb_path, gh_client__chosen_odb->path);
}

/*
 * Update the loose object cache to include the newly created
 * object.
 */
static void gh_client__update_loose_cache(const char *line)
{
	const char *v1_oid;
	struct object_id oid;

	if (!skip_prefix(line, "loose ", &v1_oid))
		BUG("update_loose_cache: invalid line '%s'", line);

	if (get_oid_hex(v1_oid, &oid))
		BUG("update_loose_cache: invalid line '%s'", line);

	odb_loose_cache_add_new_oid(gh_client__chosen_odb, &oid);
}

/*
 * Update the packed-git list to include the newly created packfile.
 */
static void gh_client__update_packed_git(const char *line)
{
	struct strbuf path = STRBUF_INIT;
	const char *v1_filename;
	struct packed_git *p;
	int is_local;

	if (!skip_prefix(line, "packfile ", &v1_filename))
		BUG("update_packed_git: invalid line '%s'", line);

	/*
	 * ODB[0] is the local .git/objects.  All others are alternates.
	 */
	is_local = (gh_client__chosen_odb == the_repository->objects->odb);

	strbuf_addf(&path, "%s/pack/%s",
		    gh_client__chosen_odb->path, v1_filename);
	strbuf_strip_suffix(&path, ".pack");
	strbuf_addstr(&path, ".idx");

	p = add_packed_git(path.buf, path.len, is_local);
	if (p)
		install_packed_git_and_mru(the_repository, p);
}

/*
 * CAP_OBJECTS verbs return the same format response:
 *
 *    <odb>
 *    <data>*
 *    <status>
 *    <flush>
 *
 * Where:
 *
 * <odb>      ::= odb SP <directory> LF
 *
 * <data>     ::= <packfile> / <loose>
 *
 * <packfile> ::= packfile SP <filename> LF
 *
 * <loose>    ::= loose SP <hex-oid> LF
 *
 * <status>   ::=   ok LF
 *                / partial LF
 *                / error SP <message> LF
 *
 * Note that `gvfs-helper` controls how/if it chunks the request when
 * it talks to the cache-server and/or main Git server.  So it is
 * possible for us to receive many packfiles and/or loose objects *AND
 * THEN* get a hard network error or a 404 on an individual object.
 *
 * If we get a partial result, we can let the caller try to continue
 * -- for example, maybe an immediate request for a tree object was
 * grouped with a queued request for a blob.  The tree-walk *might* be
 * able to continue and let the 404 blob be handled later.
 */
static int gh_client__objects__receive_response(
	struct child_process *process,
	enum gh_client__created *p_ghc,
	int *p_nr_loose, int *p_nr_packfile)
{
	enum gh_client__created ghc = GHC__CREATED__NOTHING;
	const char *v1;
	char *line;
	int len;
	int nr_loose = 0;
	int nr_packfile = 0;
	int err = 0;

	while (1) {
		/*
		 * Warning: packet_read_line_gently() calls die()
		 * despite the _gently moniker.
		 */
		len = packet_read_line_gently(process->out, NULL, &line);
		if ((len < 0) || !line)
			break;

		if (starts_with(line, "odb")) {
			gh_client__verify_odb_line(line);
		}

		else if (starts_with(line, "packfile")) {
			gh_client__update_packed_git(line);
			ghc |= GHC__CREATED__PACKFILE;
			nr_packfile++;
		}

		else if (starts_with(line, "loose")) {
			gh_client__update_loose_cache(line);
			ghc |= GHC__CREATED__LOOSE;
			nr_loose++;
		}

		else if (starts_with(line, "ok"))
			;
		else if (starts_with(line, "partial"))
			;
		else if (skip_prefix(line, "error ", &v1)) {
			error("gvfs-helper error: '%s'", v1);
			err = -1;
		}
	}

	*p_ghc = ghc;
	*p_nr_loose = nr_loose;
	*p_nr_packfile = nr_packfile;

	return err;
}

/*
 * Select the preferred ODB for fetching missing objects.
 * This should be the alternate with the same directory
 * name as set in `gvfs.sharedCache`.
 *
 * Fallback to .git/objects if necessary.
 */
static void gh_client__choose_odb(void)
{
	struct object_directory *odb;

	if (gh_client__chosen_odb)
		return;

	prepare_alt_odb(the_repository);
	gh_client__chosen_odb = the_repository->objects->odb;

	if (!gvfs_shared_cache_pathname.len)
		return;

	for (odb = the_repository->objects->odb->next; odb; odb = odb->next) {
		if (!strcmp(odb->path, gvfs_shared_cache_pathname.buf)) {
			gh_client__chosen_odb = odb;
			return;
		}
	}
}

static struct gh_server__process *gh_client__find_long_running_process(
	unsigned int cap_needed)
{
	struct gh_server__process *entry;
	struct strvec argv = STRVEC_INIT;
	struct strbuf quoted = STRBUF_INIT;

	gh_client__choose_odb();

	/*
	 * TODO decide what defaults we want.
	 */
	strvec_push(&argv, "gvfs-helper");
	strvec_push(&argv, "--fallback");
	strvec_push(&argv, "--cache-server=trust");
	strvec_pushf(&argv, "--shared-cache=%s",
			 gh_client__chosen_odb->path);
	strvec_push(&argv, "server");

	sq_quote_argv_pretty(&quoted, argv.v);

	/*
	 * Find an existing long-running process with the above command
	 * line -or- create a new long-running process for this and
	 * subsequent requests.
	 */
	if (!gh_server__subprocess_map_initialized) {
		gh_server__subprocess_map_initialized = 1;
		hashmap_init(&gh_server__subprocess_map,
			     (hashmap_cmp_fn)cmd2process_cmp, NULL, 0);
		entry = NULL;
	} else
		entry = (struct gh_server__process *)subprocess_find_entry(
			&gh_server__subprocess_map, quoted.buf);

	if (!entry) {
		entry = xmalloc(sizeof(*entry));
		entry->supported_capabilities = 0;

		if (subprocess_start_strvec(&gh_server__subprocess_map,
					  &entry->subprocess, 1,
					  &argv, gh_client__start_fn))
			FREE_AND_NULL(entry);
	}

	if (entry &&
	    (entry->supported_capabilities & cap_needed) != cap_needed) {
		error("gvfs-helper: does not support needed capabilities");
		subprocess_stop(&gh_server__subprocess_map,
				(struct subprocess_entry *)entry);
		FREE_AND_NULL(entry);
	}

	strvec_clear(&argv);
	strbuf_release(&quoted);

	return entry;
}

void gh_client__queue_oid(const struct object_id *oid)
{
	/*
	 * Keep this trace as a printf only, so that it goes to the
	 * perf log, but not the event log.  It is useful for interactive
	 * debugging, but generates way too much (unuseful) noise for the
	 * database.
	 */
	if (trace2_is_enabled())
		trace2_printf("gh_client__queue_oid: %s", oid_to_hex(oid));

	if (!oidset_insert(&gh_client__oidset_queued, oid))
		gh_client__oidset_count++;
}

/*
 * This routine should actually take a "const struct oid_array *"
 * rather than the component parts, but fetch_objects() uses
 * this model (because of the call in sha1-file.c).
 */
void gh_client__queue_oid_array(const struct object_id *oids, int oid_nr)
{
	int k;

	for (k = 0; k < oid_nr; k++)
		gh_client__queue_oid(&oids[k]);
}

/*
 * Bulk fetch all of the queued OIDs in the OIDSET.
 */
int gh_client__drain_queue(enum gh_client__created *p_ghc)
{
	struct gh_server__process *entry;
	struct child_process *process;
	int nr_loose = 0;
	int nr_packfile = 0;
	int err = 0;

	*p_ghc = GHC__CREATED__NOTHING;

	if (!gh_client__oidset_count)
		return 0;

	entry = gh_client__find_long_running_process(CAP_OBJECTS);
	if (!entry)
		return -1;

	trace2_region_enter("gh-client", "objects/post", the_repository);

	process = &entry->subprocess.process;

	sigchain_push(SIGPIPE, SIG_IGN);

	err = gh_client__send__objects_post(process);
	if (!err)
		err = gh_client__objects__receive_response(
			process, p_ghc, &nr_loose, &nr_packfile);

	sigchain_pop(SIGPIPE);

	if (err) {
		subprocess_stop(&gh_server__subprocess_map,
				(struct subprocess_entry *)entry);
		FREE_AND_NULL(entry);
	}

	trace2_data_intmax("gh-client", the_repository,
			   "objects/post/nr_objects", gh_client__oidset_count);
	trace2_region_leave("gh-client", "objects/post", the_repository);

	oidset_clear(&gh_client__oidset_queued);
	gh_client__oidset_count = 0;

	return err;
}

/*
 * Get exactly 1 object immediately.
 * Ignore any queued objects.
 */
int gh_client__get_immediate(const struct object_id *oid,
			     enum gh_client__created *p_ghc)
{
	struct gh_server__process *entry;
	struct child_process *process;
	int nr_loose = 0;
	int nr_packfile = 0;
	int err = 0;

	/*
	 * Keep this trace as a printf only, so that it goes to the
	 * perf log, but not the event log.  It is useful for interactive
	 * debugging, but generates way too much (unuseful) noise for the
	 * database.
	 */
	if (trace2_is_enabled())
		trace2_printf("gh_client__get_immediate: %s", oid_to_hex(oid));

	entry = gh_client__find_long_running_process(CAP_OBJECTS);
	if (!entry)
		return -1;

	trace2_region_enter("gh-client", "objects/get", the_repository);

	process = &entry->subprocess.process;

	sigchain_push(SIGPIPE, SIG_IGN);

	err = gh_client__send__objects_get(process, oid);
	if (!err)
		err = gh_client__objects__receive_response(
			process, p_ghc, &nr_loose, &nr_packfile);

	sigchain_pop(SIGPIPE);

	if (err) {
		subprocess_stop(&gh_server__subprocess_map,
				(struct subprocess_entry *)entry);
		FREE_AND_NULL(entry);
	}

	trace2_region_leave("gh-client", "objects/get", the_repository);

	return err;
}

/*
 * Ask gvfs-helper to prefetch commits-and-trees packfiles since a
 * given timestamp.
 *
 * If seconds_since_epoch is zero, gvfs-helper will scan the ODB for
 * the last received prefetch and ask for ones newer than that.
 */
int gh_client__prefetch(timestamp_t seconds_since_epoch,
			int *nr_packfiles_received)
{
	struct gh_server__process *entry;
	struct child_process *process;
	enum gh_client__created ghc;
	int nr_loose = 0;
	int nr_packfile = 0;
	int err = 0;

	entry = gh_client__find_long_running_process(CAP_OBJECTS);
	if (!entry)
		return -1;

	trace2_region_enter("gh-client", "objects/prefetch", the_repository);
	trace2_data_intmax("gh-client", the_repository, "prefetch/since",
			   seconds_since_epoch);

	process = &entry->subprocess.process;

	sigchain_push(SIGPIPE, SIG_IGN);

	err = gh_client__send__objects_prefetch(process, seconds_since_epoch);
	if (!err)
		err = gh_client__objects__receive_response(
			process, &ghc, &nr_loose, &nr_packfile);

	sigchain_pop(SIGPIPE);

	if (err) {
		subprocess_stop(&gh_server__subprocess_map,
				(struct subprocess_entry *)entry);
		FREE_AND_NULL(entry);
	}

	trace2_data_intmax("gh-client", the_repository,
			   "prefetch/packfile_count", nr_packfile);
	trace2_region_leave("gh-client", "objects/prefetch", the_repository);

	if (nr_packfiles_received)
		*nr_packfiles_received = nr_packfile;

	return err;
}
