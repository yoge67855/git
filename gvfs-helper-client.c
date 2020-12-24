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
static int gh_client__includes_immediate;

struct gh_server__process {
	struct subprocess_entry subprocess; /* must be first */
	unsigned int supported_capabilities;
};

static int gh_server__subprocess_map_initialized;
static struct hashmap gh_server__subprocess_map;
static struct object_directory *gh_client__chosen_odb;

#define CAP_GET      (1u<<1)

static int gh_client__start_fn(struct subprocess_entry *subprocess)
{
	static int versions[] = {1, 0};
	static struct subprocess_capability capabilities[] = {
		{ "get", CAP_GET },
		{ NULL, 0 }
	};

	struct gh_server__process *entry = (struct gh_server__process *)subprocess;

	return subprocess_handshake(subprocess, "gvfs-helper", versions,
				    NULL, capabilities,
				    &entry->supported_capabilities);
}

/*
 * Send:
 *
 *     get LF
 *     (<hex-oid> LF)*
 *     <flush>
 *
 */
static int gh_client__get__send_command(struct child_process *process)
{
	struct oidset_iter iter;
	struct object_id *oid;
	int err;

	/*
	 * We assume that all of the packet_ routines call error()
	 * so that we don't have to.
	 */

	err = packet_write_fmt_gently(process->in, "get\n");
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
 * We expect:
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
static int gh_client__get__receive_response(
	struct child_process *process,
	enum gh_client__created *p_ghc,
	int *p_nr_loose, int *p_nr_packfile)
{
	enum gh_client__created ghc = GHC__CREATED__NOTHING;
	const char *v1;
	char *line;
	int len;
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
			*p_nr_packfile += 1;
		}

		else if (starts_with(line, "loose")) {
			gh_client__update_loose_cache(line);
			ghc |= GHC__CREATED__LOOSE;
			*p_nr_loose += 1;
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

static int gh_client__get(enum gh_client__created *p_ghc)
{
	struct gh_server__process *entry;
	struct child_process *process;
	struct strvec argv = STRVEC_INIT;
	struct strbuf quoted = STRBUF_INIT;
	int nr_loose = 0;
	int nr_packfile = 0;
	int err = 0;

	trace2_region_enter("gh-client", "get", the_repository);

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

		err = subprocess_start_strvec(
			&gh_server__subprocess_map, &entry->subprocess, 1,
			&argv, gh_client__start_fn);
		if (err) {
			free(entry);
			goto leave_region;
		}
	}

	process = &entry->subprocess.process;

	if (!(CAP_GET & entry->supported_capabilities)) {
		error("gvfs-helper: does not support GET");
		subprocess_stop(&gh_server__subprocess_map,
				(struct subprocess_entry *)entry);
		free(entry);
		err = -1;
		goto leave_region;
	}

	sigchain_push(SIGPIPE, SIG_IGN);

	err = gh_client__get__send_command(process);
	if (!err)
		err = gh_client__get__receive_response(process, p_ghc,
						 &nr_loose, &nr_packfile);

	sigchain_pop(SIGPIPE);

	if (err) {
		subprocess_stop(&gh_server__subprocess_map,
				(struct subprocess_entry *)entry);
		free(entry);
	}

leave_region:
	strvec_clear(&argv);
	strbuf_release(&quoted);

	trace2_data_intmax("gh-client", the_repository,
			   "get/immediate", gh_client__includes_immediate);

	trace2_data_intmax("gh-client", the_repository,
			   "get/nr_objects", gh_client__oidset_count);

	if (nr_loose)
		trace2_data_intmax("gh-client", the_repository,
				   "get/nr_loose", nr_loose);

	if (nr_packfile)
		trace2_data_intmax("gh-client", the_repository,
				   "get/nr_packfile", nr_packfile);

	if (err)
		trace2_data_intmax("gh-client", the_repository,
				   "get/error", err);

	trace2_region_leave("gh-client", "get", the_repository);

	oidset_clear(&gh_client__oidset_queued);
	gh_client__oidset_count = 0;
	gh_client__includes_immediate = 0;

	return err;
}

void gh_client__queue_oid(const struct object_id *oid)
{
	// TODO consider removing this trace2.  it is useful for interactive
	// TODO debugging, but may generate way too much noise for a data
	// TODO event.
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

int gh_client__drain_queue(enum gh_client__created *p_ghc)
{
	*p_ghc = GHC__CREATED__NOTHING;

	if (!gh_client__oidset_count)
		return 0;

	return gh_client__get(p_ghc);
}
int gh_client__get_immediate(const struct object_id *oid,
			     enum gh_client__created *p_ghc)
{
	gh_client__includes_immediate = 1;

	// TODO consider removing this trace2.  it is useful for interactive
	// TODO debugging, but may generate way too much noise for a data
	// TODO event.
	trace2_printf("gh_client__get_immediate: %s", oid_to_hex(oid));

	if (!oidset_insert(&gh_client__oidset_queued, oid))
		gh_client__oidset_count++;

	return gh_client__drain_queue(p_ghc);
}
