// TODO Write a man page.  Here are some notes for dogfooding.
// TODO
//
// Usage: git gvfs-helper [<main_options>] <sub-command> [<sub-command-options>]
//
// <main_options>:
//
//     --remote=<remote-name>         // defaults to "origin"
//
//     --fallback                     // boolean. defaults to off
//
//            When a fetch from the cache-server fails, automatically
//            fallback to the main Git server.  This option has no effect
//            if no cache-server is defined.
//
//     --cache-server=<use>  // defaults to "verify"
//
//            verify   := lookup the set of defined cache-servers using
//                        "gvfs/config" and confirm that the selected
//                        cache-server is well-known.  Silently disable the
//                        cache-server if not.  (See security notes later.)
//
//            error    := verify cache-server and abort if not well-known.
//
//            trust    := do not verify cache-server.  just use it, if set.
//
//            disable  := disable the cache-server and always use the main
//                        Git server.
//
//     --shared-cache=<odb-directory-pathname>
//
//            A relative or absolute pathname to the ODB directory to store
//            fetched objects.
//
//            If this option is not specified, we default to the value
//            in the "gvfs.sharedcache" config setting and then to the
//            local ".git/objects" directory.
//
// <sub-command>:
//
//     config
//
//            Fetch the "gvfs/config" string from the main Git server.
//            (The cache-server setting is ignored because cache-servers
//            do not support this REST API.)
//
//     get
//
//            Fetch 1 or more objects one at a time using a "/gvfs/objects"
//            GET request.
//
//            If a cache-server is configured,
//            try it first.  Optionally fallback to the main Git server.
//
//            The set of objects is given on stdin and is assumed to be
//            a list of <oid>, one per line.
//
//            <get-options>:
//
//                 --max-retries=<n>     // defaults to "6"
//
//                       Number of retries after transient network errors.
//                       Set to zero to disable such retries.
//
//     post
//
//            Fetch 1 or more objects in bulk using a "/gvfs/objects" POST
//            request.
//
//            If a cache-server is configured,
//            try it first.  Optionally fallback to the main Git server.
//
//            The set of objects is given on stdin and is assumed to be
//            a list of <oid>, one per line.
//
//            <get-options>:
//
//                 --block-size=<n>      // defaults to "4000"
//
//                       Request objects from server in batches of at
//                       most n objects (not bytes).
//
//                 --depth=<depth>       // defaults to "1"
//
//                 --max-retries=<n>     // defaults to "6"
//
//                       Number of retries after transient network errors.
//                       Set to zero to disable such retries.
//
//     prefetch
//
//            Use "/gvfs/prefetch" REST API to fetch 1 or more commits-and-trees
//            prefetch packs from the server.
//
//            <prefetch-options>:
//
//                 --since=<t>           // defaults to "0"
//
//                       Time in seconds since the epoch.  If omitted or
//                       zero, the timestamp from the newest prefetch
//                       packfile found in the shared-cache ODB is used.
//                       (This is based upon the packfile name, not the
//                       mtime.)
//
//                       The GVFS Protocol defines this value as a way to
//                       request cached packfiles NEWER THAN this timestamp.
//
//     server
//
//            Interactive/sub-process mode.  Listen for a series of commands
//            and data on stdin and return results on stdout.  This command
//            uses pkt-line format [1] and implements the long-running process
//            protocol [2] to communicate with the foreground/parent process.
//
//            <server-options>:
//
//                 --block-size=<n>      // defaults to "4000"
//
//                       Request objects from server in batches of at
//                       most n objects (not bytes) when using POST
//                       requests.
//
//                 --depth=<depth>       // defaults to "1"
//
//                 --max-retries=<n>     // defaults to "6"
//
//                       Number of retries after transient network errors.
//                       Set to zero to disable such retries.
//
//            Interactive verb: objects.get
//
//                 Fetch 1 or more objects, one at a time, using a
//                 "/gvfs/ojbects" GET requests.
//
//                 Each object will be created as a loose object in the ODB.
//
//                 Create 1 or more loose objects in the shared-cache ODB.
//                 (The pathname of the selected ODB is reported at the
//                 beginning of the response; this should match the pathname
//                 given on the command line).
//
//                 git> objects.get
//                 git> <oid>
//                 git> <oid>
//                 git> ...
//                 git> <oid>
//                 git> 0000
//
//                 git< odb <directory>
//                 git< loose <oid>
//                 git< loose <oid>
//                 git< ...
//                 git< loose <oid>
//                 git< ok | partial | error <message>
//                 git< 0000
//
//            Interactive verb: objects.post
//
//                 Fetch 1 or more objects, in bulk, using one or more
//                 "/gvfs/objects" POST requests.
//
//                 Create 1 or more loose objects and/or packfiles in the
//                 shared-cache ODB.  A POST is allowed to respond with
//                 either loose or packed objects.
//
//                 git> objects.post
//                 git> <oid>
//                 git> <oid>
//                 git> ...
//                 git> <oid>
//                 git> 0000
//
//                 git< odb <directory>
//                 git< loose <oid> | packfile <filename.pack>
//                 git< loose <oid> | packfile <filename.pack>
//                 git< ...
//                 git< loose <oid> | packfile <filename.pack>
//                 git< ok | partial | error <message>
//                 git< 0000
//
//            Interactive verb: object.prefetch
//
//                 Fetch 1 or more prefetch packs using a "/gvfs/prefetch"
//                 request.
//
//                 git> objects.prefetch
//                 git> <timestamp>            // optional
//                 git> 0000
//
//                 git< odb <directory>
//                 git< packfile <filename.pack>
//                 git< packfile <filename.pack>
//                 git< ...
//                 git< packfile <filename.pack>
//                 git< ok | error <message>
//                 git< 0000
//
//            If a cache-server is configured, try it first.
//            Optionally fallback to the main Git server.
//
//            [1] Documentation/technical/protocol-common.txt
//            [2] Documentation/technical/long-running-process-protocol.txt
//            [3] See GIT_TRACE_PACKET
//
//////////////////////////////////////////////////////////////////

#include "cache.h"
#include "config.h"
#include "remote.h"
#include "connect.h"
#include "strbuf.h"
#include "walker.h"
#include "http.h"
#include "exec-cmd.h"
#include "run-command.h"
#include "pkt-line.h"
#include "string-list.h"
#include "sideband.h"
#include "strvec.h"
#include "credential.h"
#include "oid-array.h"
#include "send-pack.h"
#include "protocol.h"
#include "quote.h"
#include "transport.h"
#include "parse-options.h"
#include "object-store.h"
#include "json-writer.h"
#include "tempfile.h"
#include "oidset.h"
#include "dir.h"
#include "progress.h"
#include "packfile.h"

#define TR2_CAT "gvfs-helper"

static const char * const main_usage[] = {
	N_("git gvfs-helper [<main_options>] config      [<options>]"),
	N_("git gvfs-helper [<main_options>] get         [<options>]"),
	N_("git gvfs-helper [<main_options>] post        [<options>]"),
	N_("git gvfs-helper [<main_options>] prefetch    [<options>]"),
	N_("git gvfs-helper [<main_options>] server      [<options>]"),
	NULL
};

static const char *const objects_get_usage[] = {
	N_("git gvfs-helper [<main_options>] get [<options>]"),
	NULL
};

static const char *const objects_post_usage[] = {
	N_("git gvfs-helper [<main_options>] post [<options>]"),
	NULL
};

static const char *const prefetch_usage[] = {
	N_("git gvfs-helper [<main_options>] prefetch [<options>]"),
	NULL
};

static const char *const server_usage[] = {
	N_("git gvfs-helper [<main_options>] server [<options>]"),
	NULL
};

/*
 * "commitDepth" field in gvfs protocol
 */
#define GH__DEFAULT__OBJECTS_POST__COMMIT_DEPTH 1

/*
 * Chunk/block size in number of objects we request in each packfile
 */
#define GH__DEFAULT__OBJECTS_POST__BLOCK_SIZE 4000

/*
 * Retry attempts (after the initial request) for transient errors and 429s.
 */
#define GH__DEFAULT_MAX_RETRIES 6

/*
 * Maximum delay in seconds for transient (network) error retries.
 */
#define GH__DEFAULT_MAX_TRANSIENT_BACKOFF_SEC 300

/*
 * Our exit-codes.
 */
enum gh__error_code {
	GH__ERROR_CODE__USAGE = -1, /* will be mapped to usage() */
	GH__ERROR_CODE__OK = 0,
	GH__ERROR_CODE__ERROR = 1, /* unspecified */
	GH__ERROR_CODE__CURL_ERROR = 2,
	GH__ERROR_CODE__HTTP_401 = 3,
	GH__ERROR_CODE__HTTP_404 = 4,
	GH__ERROR_CODE__HTTP_429 = 5,
	GH__ERROR_CODE__HTTP_503 = 6,
	GH__ERROR_CODE__HTTP_OTHER = 7,
	GH__ERROR_CODE__UNEXPECTED_CONTENT_TYPE = 8,
	GH__ERROR_CODE__COULD_NOT_CREATE_TEMPFILE = 8,
	GH__ERROR_CODE__COULD_NOT_INSTALL_LOOSE = 10,
	GH__ERROR_CODE__COULD_NOT_INSTALL_PACKFILE = 11,
	GH__ERROR_CODE__SUBPROCESS_SYNTAX = 12,
	GH__ERROR_CODE__INDEX_PACK_FAILED = 13,
	GH__ERROR_CODE__COULD_NOT_INSTALL_PREFETCH = 14,
};

enum gh__cache_server_mode {
	/* verify URL. disable if unknown. */
	GH__CACHE_SERVER_MODE__VERIFY_DISABLE = 0,
	/* verify URL. error if unknown. */
	GH__CACHE_SERVER_MODE__VERIFY_ERROR,
	/* disable the cache-server, if defined */
	GH__CACHE_SERVER_MODE__DISABLE,
	/* trust any cache-server */
	GH__CACHE_SERVER_MODE__TRUST_WITHOUT_VERIFY,
};

/*
 * The set of command line, config, and environment variables
 * that we use as input to decide how we should operate.
 */
static struct gh__cmd_opts {
	const char *remote_name;

	int try_fallback; /* to git server if cache-server fails */
	int show_progress;

	int depth;
	int block_size;
	int max_retries;
	int max_transient_backoff_sec;

	enum gh__cache_server_mode cache_server_mode;
} gh__cmd_opts;

/*
 * The chosen global state derrived from the inputs in gh__cmd_opts.
 */
static struct gh__global {
	struct remote *remote;

	struct credential main_creds;
	struct credential cache_creds;

	const char *main_url;
	const char *cache_server_url;

	struct strbuf buf_odb_path;

	int http_is_initialized;
	int cache_server_is_initialized; /* did sub-command look for one */
	int main_creds_need_approval; /* try to only approve them once */

} gh__global;

enum gh__server_type {
	GH__SERVER_TYPE__MAIN = 0,
	GH__SERVER_TYPE__CACHE = 1,

	GH__SERVER_TYPE__NR,
};

static const char *gh__server_type_label[GH__SERVER_TYPE__NR] = {
	"(main)",
	"(cs)"
};

enum gh__objects_mode {
	GH__OBJECTS_MODE__NONE = 0,

	/*
	 * Bulk fetch objects.
	 *
	 * But also, force the use of HTTP POST regardless of how many
	 * objects we are requesting.
	 *
	 * The GVFS Protocol treats requests for commit objects
	 * differently in GET and POST requests WRT whether it
	 * automatically also fetches the referenced trees.
	 */
	GH__OBJECTS_MODE__POST,

	/*
	 * Fetch objects one at a time using HTTP GET.
	 *
	 * Force the use of GET (primarily because of the commit
	 * object treatment).
	 */
	GH__OBJECTS_MODE__GET,

	/*
	 * Fetch one or more pre-computed "prefetch packs" containing
	 * commits and trees.
	 */
	GH__OBJECTS_MODE__PREFETCH,
};

struct gh__azure_throttle
{
	unsigned long tstu_limit;
	unsigned long tstu_remaining;

	unsigned long reset_sec;
	unsigned long retry_after_sec;
};

static void gh__azure_throttle__zero(struct gh__azure_throttle *azure)
{
	azure->tstu_limit = 0;
	azure->tstu_remaining = 0;
	azure->reset_sec = 0;
	azure->retry_after_sec = 0;
}

#define GH__AZURE_THROTTLE_INIT { \
	.tstu_limit = 0, \
	.tstu_remaining = 0, \
	.reset_sec = 0, \
	.retry_after_sec = 0, \
	}

static struct gh__azure_throttle gh__global_throttle[GH__SERVER_TYPE__NR] = {
	GH__AZURE_THROTTLE_INIT,
	GH__AZURE_THROTTLE_INIT,
};

/*
 * Stolen from http.c
 */
static CURLcode gh__curlinfo_strbuf(CURL *curl, CURLINFO info, struct strbuf *buf)
{
	char *ptr;
	CURLcode ret;

	strbuf_reset(buf);
	ret = curl_easy_getinfo(curl, info, &ptr);
	if (!ret && ptr)
		strbuf_addstr(buf, ptr);
	return ret;
}

enum gh__progress_state {
	GH__PROGRESS_STATE__START = 0,
	GH__PROGRESS_STATE__PHASE1,
	GH__PROGRESS_STATE__PHASE2,
	GH__PROGRESS_STATE__PHASE3,
};

/*
 * Parameters to drive an HTTP request (with any necessary retries).
 */
struct gh__request_params {
	/*
	 * b_is_post indicates if the current HTTP request is a POST=1 or
	 * a GET=0.  This is a lower level field used to setup CURL and
	 * the tempfile used to receive the content.
	 *
	 * It is related to, but different from the GH__OBJECTS_MODE__
	 * field that we present to the gvfs-helper client or in the CLI
	 * (which only concerns the semantics of the /gvfs/objects protocol
	 * on the set of requested OIDs).
	 *
	 * For example, we use an HTTP GET to get the /gvfs/config data
	 * into a buffer.
	 */
	int b_is_post;
	int b_write_to_file;      /* write to file=1 or strbuf=0 */
	int b_permit_cache_server_if_defined;

	enum gh__objects_mode objects_mode;
	enum gh__server_type server_type;

	int k_attempt; /* robust retry attempt */
	int k_transient_delay_sec; /* delay before transient error retries */

	unsigned long object_count; /* number of objects being fetched */

	const struct strbuf *post_payload; /* POST body to send */

	struct curl_slist *headers; /* additional http headers to send */
	struct tempfile *tempfile; /* for response content when file */
	struct strbuf *buffer;     /* for response content when strbuf */
	struct strbuf tr2_label;   /* for trace2 regions */

	struct object_id loose_oid;

	/*
	 * Note that I am putting all of the progress-related instance data
	 * inside the request-params in the hope that we can eventually
	 * do multi-threaded/concurrent HTTP requests when chunking
	 * large requests.  However, the underlying "struct progress" API
	 * is not thread safe (that is, it doesn't allow concurrent progress
	 * reports (since that might require multiple lines on the screen
	 * or something)).
	 */
	enum gh__progress_state progress_state;
	struct strbuf progress_base_phase2_msg;
	struct strbuf progress_base_phase3_msg;

	/*
	 * The buffer for the formatted progress message is shared by the
	 * "struct progress" API and must remain valid for the duration of
	 * the start_progress..stop_progress lifespan.
	 */
	struct strbuf progress_msg;
	struct progress *progress;

	struct strbuf e2eid;

	struct string_list *result_list; /* we do not own this */
};

#define GH__REQUEST_PARAMS_INIT { \
	.b_is_post = 0, \
	.b_write_to_file = 0, \
	.b_permit_cache_server_if_defined = 1, \
	.server_type = GH__SERVER_TYPE__MAIN, \
	.k_attempt = 0, \
	.k_transient_delay_sec = 0, \
	.object_count = 0, \
	.post_payload = NULL, \
	.headers = NULL, \
	.tempfile = NULL, \
	.buffer = NULL, \
	.tr2_label = STRBUF_INIT, \
	.loose_oid = {{0}}, \
	.progress_state = GH__PROGRESS_STATE__START, \
	.progress_base_phase2_msg = STRBUF_INIT, \
	.progress_base_phase3_msg = STRBUF_INIT, \
	.progress_msg = STRBUF_INIT, \
	.progress = NULL, \
	.e2eid = STRBUF_INIT, \
	.result_list = NULL, \
	}

static void gh__request_params__release(struct gh__request_params *params)
{
	if (!params)
		return;

	params->post_payload = NULL; /* we do not own this */

	curl_slist_free_all(params->headers);
	params->headers = NULL;

	delete_tempfile(&params->tempfile);

	params->buffer = NULL; /* we do not own this */

	strbuf_release(&params->tr2_label);

	strbuf_release(&params->progress_base_phase2_msg);
	strbuf_release(&params->progress_base_phase3_msg);
	strbuf_release(&params->progress_msg);

	stop_progress(&params->progress);
	params->progress = NULL;

	strbuf_release(&params->e2eid);

	params->result_list = NULL; /* we do not own this */
}

/*
 * How we handle retries for various unexpected network errors.
 */
enum gh__retry_mode {
	/*
	 * The operation was successful, so no retry is needed.
	 * Use this for HTTP 200, for example.
	 */
	GH__RETRY_MODE__SUCCESS = 0,

	/*
	 * Retry using the normal 401 Auth mechanism.
	 */
	GH__RETRY_MODE__HTTP_401,

	/*
	 * Fail because at least one of the requested OIDs does not exist.
	 */
	GH__RETRY_MODE__FAIL_404,

	/*
	 * A transient network error, such as dropped connection
	 * or network IO error.  Our belief is that a retry MAY
	 * succeed.  (See Gremlins and Cosmic Rays....)
	 */
	GH__RETRY_MODE__TRANSIENT,

	/*
	 * Request was blocked completely because of a 429.
	 */
	GH__RETRY_MODE__HTTP_429,

	/*
	 * Request failed because the server was (temporarily?) offline.
	 */
	GH__RETRY_MODE__HTTP_503,

	/*
	 * The operation had a hard failure and we have no
	 * expectation that a second attempt will give a different
	 * answer, such as a bad hostname or a mal-formed URL.
	 */
	GH__RETRY_MODE__HARD_FAIL,
};

/*
 * Bucket to describe the results of an HTTP requests (may be
 * overwritten during retries so that it describes the final attempt).
 */
struct gh__response_status {
	struct strbuf error_message;
	struct strbuf content_type;
	enum gh__error_code ec;
	enum gh__retry_mode retry;
	intmax_t bytes_received;
	struct gh__azure_throttle *azure;
};

#define GH__RESPONSE_STATUS_INIT { \
	.error_message = STRBUF_INIT, \
	.content_type = STRBUF_INIT, \
	.ec = GH__ERROR_CODE__OK, \
	.retry = GH__RETRY_MODE__SUCCESS, \
	.bytes_received = 0, \
	.azure = NULL, \
	}

static void gh__response_status__zero(struct gh__response_status *s)
{
	strbuf_setlen(&s->error_message, 0);
	strbuf_setlen(&s->content_type, 0);
	s->ec = GH__ERROR_CODE__OK;
	s->retry = GH__RETRY_MODE__SUCCESS;
	s->bytes_received = 0;
	s->azure = NULL;
}

static void install_result(struct gh__request_params *params,
			  struct gh__response_status *status);

/*
 * Log the E2EID for the current request.
 *
 * Since every HTTP request to the cache-server and to the main Git server
 * will send back a unique E2EID (probably a GUID), we don't want to overload
 * telemetry with each ID -- rather, only the ones for which there was a
 * problem and that may be helpful in a post mortem.
 */
static void log_e2eid(struct gh__request_params *params,
		      struct gh__response_status *status)
{
	if (!params->e2eid.len)
		return;

	switch (status->retry) {
	default:
	case GH__RETRY_MODE__SUCCESS:
	case GH__RETRY_MODE__HTTP_401:
	case GH__RETRY_MODE__FAIL_404:
		return;

	case GH__RETRY_MODE__HARD_FAIL:
	case GH__RETRY_MODE__TRANSIENT:
	case GH__RETRY_MODE__HTTP_429:
	case GH__RETRY_MODE__HTTP_503:
		break;
	}

	if (trace2_is_enabled()) {
		struct strbuf key = STRBUF_INIT;

		strbuf_addstr(&key, "e2eid");
		strbuf_addstr(&key, gh__server_type_label[params->server_type]);

		trace2_data_string(TR2_CAT, NULL, key.buf,
				   params->e2eid.buf);

		strbuf_release(&key);
	}
}

/*
 * Normalize a few HTTP response codes before we try to decide
 * how to dispatch on them.
 */
static long gh__normalize_odd_codes(struct gh__request_params *params,
				    long http_response_code)
{
	if (params->server_type == GH__SERVER_TYPE__CACHE &&
	    http_response_code == 400) {
		/*
		 * The cache-server sends a somewhat bogus 400 instead of
		 * the normal 401 when AUTH is required.  Fixup the status
		 * to hide that.
		 *
		 * TODO Technically, the cache-server could send a 400
		 * TODO for many reasons, not just for their bogus
		 * TODO pseudo-401, but we're going to assume it is a
		 * TODO 401 for now.  We should confirm the expected
		 * TODO error message in the response-body.
		 */
		return 401;
	}

	if (http_response_code == 203) {
		/*
		 * A proxy server transformed a 200 from the origin server
		 * into a 203.  We don't care about the subtle distinction.
		 */
		return 200;
	}

	return http_response_code;
}

/*
 * Map HTTP response codes into a retry strategy.
 * See https://en.wikipedia.org/wiki/List_of_HTTP_status_codes
 *
 * https://docs.microsoft.com/en-us/azure/devops/integrate/concepts/rate-limits?view=azure-devops
 */
static void compute_retry_mode_from_http_response(
	struct gh__response_status *status,
	long http_response_code)
{
	switch (http_response_code) {

	case 200:
		status->retry = GH__RETRY_MODE__SUCCESS;
		status->ec = GH__ERROR_CODE__OK;
		return;

	case 301: /* all the various flavors of HTTP Redirect */
	case 302:
	case 303:
	case 304:
	case 305:
	case 306:
	case 307:
	case 308:
		/*
		 * TODO Consider a redirected-retry (with or without
		 * TODO a Retry-After header).
		 */
		goto hard_fail;

	case 401:
		strbuf_addstr(&status->error_message,
			      "(http:401) Not Authorized");
		status->retry = GH__RETRY_MODE__HTTP_401;
		status->ec = GH__ERROR_CODE__HTTP_401;
		return;

	case 404:
		/*
		 * TODO if params->object_count > 1, consider
		 * TODO splitting the request into 2 halves
		 * TODO and retrying each half in series.
		 */
		strbuf_addstr(&status->error_message,
			      "(http:404) Not Found");
		status->retry = GH__RETRY_MODE__FAIL_404;
		status->ec = GH__ERROR_CODE__HTTP_404;
		return;

	case 429:
		/*
		 * This is a hard block because we've been bad.
		 */
		strbuf_addstr(&status->error_message,
			      "(http:429) Too Many Requests [throttled]");
		status->retry = GH__RETRY_MODE__HTTP_429;
		status->ec = GH__ERROR_CODE__HTTP_429;

		trace2_data_string(TR2_CAT, NULL, "error/http",
				   status->error_message.buf);
		return;

	case 503:
		/*
		 * We assume that this comes with a "Retry-After" header like 429s.
		 */
		strbuf_addstr(&status->error_message,
			      "(http:503) Server Unavailable [throttled]");
		status->retry = GH__RETRY_MODE__HTTP_503;
		status->ec = GH__ERROR_CODE__HTTP_503;

		trace2_data_string(TR2_CAT, NULL, "error/http",
				   status->error_message.buf);
		return;

	default:
		goto hard_fail;
	}

hard_fail:
	strbuf_addf(&status->error_message, "(http:%d) Other [hard_fail]",
		    (int)http_response_code);
	status->retry = GH__RETRY_MODE__HARD_FAIL;
	status->ec = GH__ERROR_CODE__HTTP_OTHER;

	trace2_data_string(TR2_CAT, NULL, "error/http",
			   status->error_message.buf);
	return;
}

/*
 * Map CURLE errors code to a retry strategy.
 * See <curl/curl.h> and
 * https://curl.haxx.se/libcurl/c/libcurl-errors.html
 *
 * This could be a static table rather than a switch, but
 * that is harder to debug and we may want to selectively
 * log errors.
 *
 * I've commented out all of the hard-fail cases for now
 * and let the default handle them.  This is to indicate
 * that I considered them and found them to be not actionable.
 * Also, the spelling of some of the CURLE_ symbols seem
 * to change between curl releases on different platforms,
 * so I'm not going to fight that.
 */
static void compute_retry_mode_from_curl_error(
	struct gh__response_status *status,
	CURLcode curl_code)
{
	switch (curl_code) {
	case CURLE_OK:
		status->retry = GH__RETRY_MODE__SUCCESS;
		status->ec = GH__ERROR_CODE__OK;
		return;

	//se CURLE_UNSUPPORTED_PROTOCOL:     goto hard_fail;
	//se CURLE_FAILED_INIT:              goto hard_fail;
	//se CURLE_URL_MALFORMAT:            goto hard_fail;
	//se CURLE_NOT_BUILT_IN:             goto hard_fail;
	//se CURLE_COULDNT_RESOLVE_PROXY:    goto hard_fail;
	//se CURLE_COULDNT_RESOLVE_HOST:     goto hard_fail;
	case CURLE_COULDNT_CONNECT:          goto transient;
	//se CURLE_WEIRD_SERVER_REPLY:       goto hard_fail;
	//se CURLE_REMOTE_ACCESS_DENIED:     goto hard_fail;
	//se CURLE_FTP_ACCEPT_FAILED:        goto hard_fail;
	//se CURLE_FTP_WEIRD_PASS_REPLY:     goto hard_fail;
	//se CURLE_FTP_ACCEPT_TIMEOUT:       goto hard_fail;
	//se CURLE_FTP_WEIRD_PASV_REPLY:     goto hard_fail;
	//se CURLE_FTP_WEIRD_227_FORMAT:     goto hard_fail;
	//se CURLE_FTP_CANT_GET_HOST:        goto hard_fail;
	case CURLE_HTTP2:                    goto transient;
	//se CURLE_FTP_COULDNT_SET_TYPE:     goto hard_fail;
	case CURLE_PARTIAL_FILE:             goto transient;
	//se CURLE_FTP_COULDNT_RETR_FILE:    goto hard_fail;
	//se CURLE_OBSOLETE20:               goto hard_fail;
	//se CURLE_QUOTE_ERROR:              goto hard_fail;
	//se CURLE_HTTP_RETURNED_ERROR:      goto hard_fail;
	case CURLE_WRITE_ERROR:              goto transient;
	//se CURLE_OBSOLETE24:               goto hard_fail;
	case CURLE_UPLOAD_FAILED:            goto transient;
	//se CURLE_READ_ERROR:               goto hard_fail;
	//se CURLE_OUT_OF_MEMORY:            goto hard_fail;
	case CURLE_OPERATION_TIMEDOUT:       goto transient;
	//se CURLE_OBSOLETE29:               goto hard_fail;
	//se CURLE_FTP_PORT_FAILED:          goto hard_fail;
	//se CURLE_FTP_COULDNT_USE_REST:     goto hard_fail;
	//se CURLE_OBSOLETE32:               goto hard_fail;
	//se CURLE_RANGE_ERROR:              goto hard_fail;
	case CURLE_HTTP_POST_ERROR:          goto transient;
	//se CURLE_SSL_CONNECT_ERROR:        goto hard_fail;
	//se CURLE_BAD_DOWNLOAD_RESUME:      goto hard_fail;
	//se CURLE_FILE_COULDNT_READ_FILE:   goto hard_fail;
	//se CURLE_LDAP_CANNOT_BIND:         goto hard_fail;
	//se CURLE_LDAP_SEARCH_FAILED:       goto hard_fail;
	//se CURLE_OBSOLETE40:               goto hard_fail;
	//se CURLE_FUNCTION_NOT_FOUND:       goto hard_fail;
	//se CURLE_ABORTED_BY_CALLBACK:      goto hard_fail;
	//se CURLE_BAD_FUNCTION_ARGUMENT:    goto hard_fail;
	//se CURLE_OBSOLETE44:               goto hard_fail;
	//se CURLE_INTERFACE_FAILED:         goto hard_fail;
	//se CURLE_OBSOLETE46:               goto hard_fail;
	//se CURLE_TOO_MANY_REDIRECTS:       goto hard_fail;
	//se CURLE_UNKNOWN_OPTION:           goto hard_fail;
	//se CURLE_TELNET_OPTION_SYNTAX:     goto hard_fail;
	//se CURLE_OBSOLETE50:               goto hard_fail;
	//se CURLE_PEER_FAILED_VERIFICATION: goto hard_fail;
	//se CURLE_GOT_NOTHING:              goto hard_fail;
	//se CURLE_SSL_ENGINE_NOTFOUND:      goto hard_fail;
	//se CURLE_SSL_ENGINE_SETFAILED:     goto hard_fail;
	case CURLE_SEND_ERROR:               goto transient;
	case CURLE_RECV_ERROR:               goto transient;
	//se CURLE_OBSOLETE57:               goto hard_fail;
	//se CURLE_SSL_CERTPROBLEM:          goto hard_fail;
	//se CURLE_SSL_CIPHER:               goto hard_fail;
	//se CURLE_SSL_CACERT:               goto hard_fail;
	//se CURLE_BAD_CONTENT_ENCODING:     goto hard_fail;
	//se CURLE_LDAP_INVALID_URL:         goto hard_fail;
	//se CURLE_FILESIZE_EXCEEDED:        goto hard_fail;
	//se CURLE_USE_SSL_FAILED:           goto hard_fail;
	//se CURLE_SEND_FAIL_REWIND:         goto hard_fail;
	//se CURLE_SSL_ENGINE_INITFAILED:    goto hard_fail;
	//se CURLE_LOGIN_DENIED:             goto hard_fail;
	//se CURLE_TFTP_NOTFOUND:            goto hard_fail;
	//se CURLE_TFTP_PERM:                goto hard_fail;
	//se CURLE_REMOTE_DISK_FULL:         goto hard_fail;
	//se CURLE_TFTP_ILLEGAL:             goto hard_fail;
	//se CURLE_TFTP_UNKNOWNID:           goto hard_fail;
	//se CURLE_REMOTE_FILE_EXISTS:       goto hard_fail;
	//se CURLE_TFTP_NOSUCHUSER:          goto hard_fail;
	//se CURLE_CONV_FAILED:              goto hard_fail;
	//se CURLE_CONV_REQD:                goto hard_fail;
	//se CURLE_SSL_CACERT_BADFILE:       goto hard_fail;
	//se CURLE_REMOTE_FILE_NOT_FOUND:    goto hard_fail;
	//se CURLE_SSH:                      goto hard_fail;
	//se CURLE_SSL_SHUTDOWN_FAILED:      goto hard_fail;
	case CURLE_AGAIN:                    goto transient;
	//se CURLE_SSL_CRL_BADFILE:          goto hard_fail;
	//se CURLE_SSL_ISSUER_ERROR:         goto hard_fail;
	//se CURLE_FTP_PRET_FAILED:          goto hard_fail;
	//se CURLE_RTSP_CSEQ_ERROR:          goto hard_fail;
	//se CURLE_RTSP_SESSION_ERROR:       goto hard_fail;
	//se CURLE_FTP_BAD_FILE_LIST:        goto hard_fail;
	//se CURLE_CHUNK_FAILED:             goto hard_fail;
	//se CURLE_NO_CONNECTION_AVAILABLE:  goto hard_fail;
	//se CURLE_SSL_PINNEDPUBKEYNOTMATCH: goto hard_fail;
	//se CURLE_SSL_INVALIDCERTSTATUS:    goto hard_fail;
#ifdef CURLE_HTTP2_STREAM
	case CURLE_HTTP2_STREAM:             goto transient;
#endif
	default:                             goto hard_fail;
	}

hard_fail:
	strbuf_addf(&status->error_message, "(curl:%d) %s [hard_fail]",
		    curl_code, curl_easy_strerror(curl_code));
	status->retry = GH__RETRY_MODE__HARD_FAIL;
	status->ec = GH__ERROR_CODE__CURL_ERROR;

	trace2_data_string(TR2_CAT, NULL, "error/curl",
			   status->error_message.buf);
	return;

transient:
	strbuf_addf(&status->error_message, "(curl:%d) %s [transient]",
		    curl_code, curl_easy_strerror(curl_code));
	status->retry = GH__RETRY_MODE__TRANSIENT;
	status->ec = GH__ERROR_CODE__CURL_ERROR;

	trace2_data_string(TR2_CAT, NULL, "error/curl",
			   status->error_message.buf);
	return;
}

/*
 * Create a single normalized 'ec' error-code from the status we
 * received from the HTTP request.  Map a few of the expected HTTP
 * status code to 'ec', but don't get too crazy here.
 */
static void gh__response_status__set_from_slot(
	struct gh__request_params *params,
	struct gh__response_status *status,
	const struct active_request_slot *slot)
{
	long http_response_code;
	CURLcode curl_code;

	curl_code = slot->results->curl_result;
	gh__curlinfo_strbuf(slot->curl, CURLINFO_CONTENT_TYPE,
			    &status->content_type);
	curl_easy_getinfo(slot->curl, CURLINFO_RESPONSE_CODE,
			  &http_response_code);

	strbuf_setlen(&status->error_message, 0);

	http_response_code = gh__normalize_odd_codes(params,
						     http_response_code);

	/*
	 * Use normalized response/status codes form curl/http to decide
	 * how to set the error-code we propagate *AND* to decide if we
	 * we should retry because of transient network problems.
	 */
	if (curl_code == CURLE_OK ||
	    curl_code == CURLE_HTTP_RETURNED_ERROR)
		compute_retry_mode_from_http_response(status,
						      http_response_code);
	else
		compute_retry_mode_from_curl_error(status, curl_code);

	if (status->ec != GH__ERROR_CODE__OK)
		status->bytes_received = 0;
	else if (params->b_write_to_file)
		status->bytes_received = (intmax_t)ftell(params->tempfile->fp);
	else
		status->bytes_received = (intmax_t)params->buffer->len;
}

static void gh__response_status__release(struct gh__response_status *status)
{
	if (!status)
		return;
	strbuf_release(&status->error_message);
	strbuf_release(&status->content_type);
}

static int gh__curl_progress_cb(void *clientp,
				curl_off_t dltotal, curl_off_t dlnow,
				curl_off_t ultotal, curl_off_t ulnow)
{
	struct gh__request_params *params = clientp;

	/*
	 * From what I can tell, CURL progress arrives in 3 phases.
	 *
	 * [1] An initial connection setup phase where we get [0,0] [0,0].
	 * [2] An upload phase where we start sending the request headers
	 *     and body. ulnow will be > 0.  ultotal may or may not be 0.
	 * [3] A download phase where we start receiving the response
	 *     headers and payload body.  dlnow will be > 0. dltotal may
	 *     or may not be 0.
	 *
	 * If we pass zero for the total to the "struct progress" API, we
	 * get simple numbers rather than percentages.  So our progress
	 * output format may vary depending.
	 *
	 * It is unclear if CURL will give us a final callback after
	 * everything is finished, so we leave the progress handle open
	 * and let the caller issue the final stop_progress().
	 *
	 * There is a bit of a mismatch between the CURL API and the
	 * "struct progress" API.  The latter requires us to set the
	 * progress message when we call one of the start_progress
	 * methods.  We cannot change the progress message while we are
	 * showing progress state.  And we cannot change the denominator
	 * (total) after we start.  CURL may or may not give us the total
	 * sizes for each phase.
	 *
	 * Also be advised that the "struct progress" API eats messages
	 * so that the screen is only updated every second or so.  And
	 * may not print anything if the start..stop happen in less then
	 * 2 seconds.  Whereas CURL calls this callback very frequently.
	 * The net-net is that we may not actually see this progress
	 * message for small/fast HTTP requests.
	 */

	switch (params->progress_state) {
	case GH__PROGRESS_STATE__START: /* first callback */
		if (dlnow == 0 && ulnow == 0)
			goto enter_phase_1;

		if (ulnow)
			goto enter_phase_2;
		else
			goto enter_phase_3;

	case GH__PROGRESS_STATE__PHASE1:
		if (dlnow == 0 && ulnow == 0)
			return 0;

		if (ulnow)
			goto enter_phase_2;
		else
			goto enter_phase_3;

	case GH__PROGRESS_STATE__PHASE2:
		display_progress(params->progress, ulnow);
		if (dlnow == 0)
			return 0;

		stop_progress(&params->progress);
		goto enter_phase_3;

	case GH__PROGRESS_STATE__PHASE3:
		display_progress(params->progress, dlnow);
		return 0;

	default:
		return 0;
	}

enter_phase_1:
	/*
	 * Don't bother to create a progress handle during phase [1].
	 * Because we get [0,0,0,0], we don't have any data to report
	 * and would just have to synthesize some type of progress.
	 * From my testing, phase [1] is fairly quick (probably just
	 * the SSL handshake), so the "struct progress" API will most
	 * likely completely eat any messages that we did produce.
	 */
	params->progress_state = GH__PROGRESS_STATE__PHASE1;
	return 0;

enter_phase_2:
	strbuf_setlen(&params->progress_msg, 0);
	if (params->progress_base_phase2_msg.len) {
		if (params->k_attempt > 0)
			strbuf_addf(&params->progress_msg, "%s [retry %d/%d] (bytes sent)",
				    params->progress_base_phase2_msg.buf,
				    params->k_attempt, gh__cmd_opts.max_retries);
		else
			strbuf_addf(&params->progress_msg, "%s (bytes sent)",
				    params->progress_base_phase2_msg.buf);
		params->progress = start_progress(params->progress_msg.buf, ultotal);
		display_progress(params->progress, ulnow);
	}
	params->progress_state = GH__PROGRESS_STATE__PHASE2;
	return 0;

enter_phase_3:
	strbuf_setlen(&params->progress_msg, 0);
	if (params->progress_base_phase3_msg.len) {
		if (params->k_attempt > 0)
			strbuf_addf(&params->progress_msg, "%s [retry %d/%d] (bytes received)",
				    params->progress_base_phase3_msg.buf,
				    params->k_attempt, gh__cmd_opts.max_retries);
		else
			strbuf_addf(&params->progress_msg, "%s (bytes received)",
				    params->progress_base_phase3_msg.buf);
		params->progress = start_progress(params->progress_msg.buf, dltotal);
		display_progress(params->progress, dlnow);
	}
	params->progress_state = GH__PROGRESS_STATE__PHASE3;
	return 0;
}

/*
 * Run the request without using "run_one_slot()" because we
 * don't want the post-request normalization, error handling,
 * and auto-reauth handling in http.c.
 */
static void gh__run_one_slot(struct active_request_slot *slot,
			     struct gh__request_params *params,
			     struct gh__response_status *status)
{
	struct strbuf key = STRBUF_INIT;

	strbuf_addbuf(&key, &params->tr2_label);
	strbuf_addstr(&key, gh__server_type_label[params->server_type]);

	params->progress_state = GH__PROGRESS_STATE__START;
	strbuf_setlen(&params->e2eid, 0);

	trace2_region_enter(TR2_CAT, key.buf, NULL);

	if (!start_active_slot(slot)) {
		compute_retry_mode_from_curl_error(status,
						   CURLE_FAILED_INIT);
	} else {
		run_active_slot(slot);
		if (params->b_write_to_file)
			fflush(params->tempfile->fp);

		gh__response_status__set_from_slot(params, status, slot);

		log_e2eid(params, status);

		if (status->ec == GH__ERROR_CODE__OK) {
			int old_len = key.len;

			/*
			 * We only log the number of bytes received.
			 * We do not log the number of objects requested
			 * because the server may give us more than that
			 * (such as when we request a commit).
			 */
			strbuf_addstr(&key, "/nr_bytes");
			trace2_data_intmax(TR2_CAT, NULL,
					   key.buf,
					   status->bytes_received);
			strbuf_setlen(&key, old_len);
		}
	}

	if (params->progress)
		stop_progress(&params->progress);

	if (status->ec == GH__ERROR_CODE__OK && params->b_write_to_file)
		install_result(params, status);

	trace2_region_leave(TR2_CAT, key.buf, NULL);

	strbuf_release(&key);
}

static int option_parse_cache_server_mode(const struct option *opt,
					  const char *arg, int unset)
{
	if (unset) /* should not happen */
		return error(_("missing value for switch '%s'"),
			     opt->long_name);

	else if (!strcmp(arg, "verify"))
		gh__cmd_opts.cache_server_mode =
			GH__CACHE_SERVER_MODE__VERIFY_DISABLE;

	else if (!strcmp(arg, "error"))
		gh__cmd_opts.cache_server_mode =
			GH__CACHE_SERVER_MODE__VERIFY_ERROR;

	else if (!strcmp(arg, "disable"))
		gh__cmd_opts.cache_server_mode =
			GH__CACHE_SERVER_MODE__DISABLE;

	else if (!strcmp(arg, "trust"))
		gh__cmd_opts.cache_server_mode =
			GH__CACHE_SERVER_MODE__TRUST_WITHOUT_VERIFY;

	else
		return error(_("invalid value for switch '%s'"),
			     opt->long_name);

	return 0;
}

/*
 * Let command line args override "gvfs.sharedcache" config setting
 * and override the value set by git_default_config().
 *
 * The command line is parsed *AFTER* the config is loaded, so
 * prepared_alt_odb() has already been called any default or inherited
 * shared-cache has already been set.
 *
 * We have a chance to override it here.
 */
static int option_parse_shared_cache_directory(const struct option *opt,
					       const char *arg, int unset)
{
	struct strbuf buf_arg = STRBUF_INIT;

	if (unset) /* should not happen */
		return error(_("missing value for switch '%s'"),
			     opt->long_name);

	strbuf_addstr(&buf_arg, arg);
	if (strbuf_normalize_path(&buf_arg) < 0) {
		/*
		 * Pretend command line wasn't given.  Use whatever
		 * settings we already have from the config.
		 */
		strbuf_release(&buf_arg);
		return 0;
	}
	strbuf_trim_trailing_dir_sep(&buf_arg);

	if (!strbuf_cmp(&buf_arg, &gvfs_shared_cache_pathname)) {
		/*
		 * The command line argument matches what we got from
		 * the config, so we're already setup correctly. (And
		 * we have already verified that the directory exists
		 * on disk.)
		 */
		strbuf_release(&buf_arg);
		return 0;
	}

	else if (!gvfs_shared_cache_pathname.len) {
		/*
		 * A shared-cache was requested and we did not inherit one.
		 * Try it, but let alt_odb_usabe() secretly disable it if
		 * it cannot create the directory on disk.
		 */
		strbuf_addbuf(&gvfs_shared_cache_pathname, &buf_arg);

		add_to_alternates_memory(buf_arg.buf);

		strbuf_release(&buf_arg);
		return 0;
	}

	else {
		/*
		 * The requested shared-cache is different from the one
		 * we inherited.  Replace the inherited value with this
		 * one, but smartly fallback if necessary.
		 */
		struct strbuf buf_prev = STRBUF_INIT;

		strbuf_addbuf(&buf_prev, &gvfs_shared_cache_pathname);

		strbuf_setlen(&gvfs_shared_cache_pathname, 0);
		strbuf_addbuf(&gvfs_shared_cache_pathname, &buf_arg);

		add_to_alternates_memory(buf_arg.buf);

		/*
		 * alt_odb_usabe() releases gvfs_shared_cache_pathname
		 * if it cannot create the directory on disk, so fallback
		 * to the previous choice when it fails.
		 */
		if (!gvfs_shared_cache_pathname.len)
			strbuf_addbuf(&gvfs_shared_cache_pathname,
				      &buf_prev);

		strbuf_release(&buf_arg);
		strbuf_release(&buf_prev);
		return 0;
	}
}

/*
 * Lookup the URL for this remote (defaults to 'origin').
 */
static void lookup_main_url(void)
{
	/*
	 * Both VFS and Scalar only work with 'origin', so we expect this.
	 * The command line arg is mainly for debugging.
	 */
	if (!gh__cmd_opts.remote_name || !*gh__cmd_opts.remote_name)
		gh__cmd_opts.remote_name = "origin";

	gh__global.remote = remote_get(gh__cmd_opts.remote_name);
	if (!gh__global.remote->url[0] || !*gh__global.remote->url[0])
		die("unknown remote '%s'", gh__cmd_opts.remote_name);

	/*
	 * Strip out any in-line auth in the origin server URL so that
	 * we can control which creds we fetch.
	 *
	 * Azure DevOps has been known to suggest https URLS of the
	 * form "https://<account>@dev.azure.com/<account>/<path>".
	 *
	 * Break that so that we can force the use of a PAT.
	 */
	gh__global.main_url = transport_anonymize_url(gh__global.remote->url[0]);

	trace2_data_string(TR2_CAT, NULL, "remote/url", gh__global.main_url);
}

static void do__http_get__gvfs_config(struct gh__response_status *status,
				      struct strbuf *config_data);

/*
 * Find the URL of the cache-server, if we have one.
 *
 * This routined is called by the initialization code and is allowed
 * to call die() rather than returning an 'ec'.
 */
static void select_cache_server(void)
{
	struct gh__response_status status = GH__RESPONSE_STATUS_INIT;
	struct strbuf config_data = STRBUF_INIT;
	const char *match = NULL;

	/*
	 * This only indicates that the sub-command actually called
	 * this routine.  We rely on gh__global.cache_server_url to tell
	 * us if we actually have a cache-server configured.
	 */
	gh__global.cache_server_is_initialized = 1;
	gh__global.cache_server_url = NULL;

	if (gh__cmd_opts.cache_server_mode == GH__CACHE_SERVER_MODE__DISABLE) {
		trace2_data_string(TR2_CAT, NULL, "cache/url", "disabled");
		return;
	}

	if (!gvfs_cache_server_url || !*gvfs_cache_server_url) {
		switch (gh__cmd_opts.cache_server_mode) {
		default:
		case GH__CACHE_SERVER_MODE__TRUST_WITHOUT_VERIFY:
		case GH__CACHE_SERVER_MODE__VERIFY_DISABLE:
			trace2_data_string(TR2_CAT, NULL, "cache/url", "unset");
			return;

		case GH__CACHE_SERVER_MODE__VERIFY_ERROR:
			die("cache-server not set");
		}
	}

	/*
	 * If the cache-server and main Git server have the same URL, we
	 * can silently disable the cache-server (by NOT setting the field
	 * in gh__global and explicitly disable the fallback logic.)
	 */
	if (!strcmp(gvfs_cache_server_url, gh__global.main_url)) {
		gh__cmd_opts.try_fallback = 0;
		trace2_data_string(TR2_CAT, NULL, "cache/url", "same");
		return;
	}

	if (gh__cmd_opts.cache_server_mode ==
	    GH__CACHE_SERVER_MODE__TRUST_WITHOUT_VERIFY) {
		gh__global.cache_server_url = gvfs_cache_server_url;
		trace2_data_string(TR2_CAT, NULL, "cache/url",
				   gvfs_cache_server_url);
		return;
	}

	/*
	 * GVFS cache-servers use the main Git server's creds rather
	 * than having their own creds.  This feels like a security
	 * hole.  For example, if the cache-server URL is pointed to a
	 * bad site, we'll happily send them our creds to the main Git
	 * server with each request to the cache-server.  This would
	 * allow an attacker to later use our creds to impersonate us
	 * on the main Git server.
	 *
	 * So we optionally verify that the URL to the cache-server is
	 * well-known by the main Git server.
	 */

	do__http_get__gvfs_config(&status, &config_data);

	if (status.ec == GH__ERROR_CODE__OK) {
		/*
		 * The gvfs/config response is in JSON, but I don't think
		 * we need to parse it and all that.  Lets just do a simple
		 * strstr() and assume it is sufficient.
		 *
		 * We do add some context to the pattern to guard against
		 * some attacks.
		 */
		struct strbuf pattern = STRBUF_INIT;

		strbuf_addf(&pattern, "\"Url\":\"%s\"", gvfs_cache_server_url);
		match = strstr(config_data.buf, pattern.buf);

		strbuf_release(&pattern);
	}

	strbuf_release(&config_data);

	if (match) {
		gh__global.cache_server_url = gvfs_cache_server_url;
		trace2_data_string(TR2_CAT, NULL, "cache/url",
				   gvfs_cache_server_url);
	}

	else if (gh__cmd_opts.cache_server_mode ==
		 GH__CACHE_SERVER_MODE__VERIFY_ERROR) {
		if (status.ec != GH__ERROR_CODE__OK)
			die("could not verify cache-server '%s': %s",
			    gvfs_cache_server_url,
			    status.error_message.buf);
		else
			die("could not verify cache-server '%s'",
			    gvfs_cache_server_url);
	}

	else if (gh__cmd_opts.cache_server_mode ==
		 GH__CACHE_SERVER_MODE__VERIFY_DISABLE) {
		if (status.ec != GH__ERROR_CODE__OK)
			warning("could not verify cache-server '%s': %s",
				gvfs_cache_server_url,
				status.error_message.buf);
		else
			warning("could not verify cache-server '%s'",
				gvfs_cache_server_url);
		trace2_data_string(TR2_CAT, NULL, "cache/url",
				   "disabled");
	}

	gh__response_status__release(&status);
}

/*
 * Read stdin until EOF (or a blank line) and add the desired OIDs
 * to the oidset.
 *
 * Stdin should contain a list of OIDs.  Lines may have additional
 * text following the OID that we ignore.
 */
static unsigned long read_stdin_for_oids(struct oidset *oids)
{
	struct object_id oid;
	struct strbuf buf_stdin = STRBUF_INIT;
	unsigned long count = 0;

	do {
		if (strbuf_getline(&buf_stdin, stdin) == EOF || !buf_stdin.len)
			break;

		if (get_oid_hex(buf_stdin.buf, &oid))
			continue; /* just silently eat it */

		if (!oidset_insert(oids, &oid))
			count++;
	} while (1);

	return count;
}

/*
 * Build a complete JSON payload for a gvfs/objects POST request
 * containing the first `nr_in_block` OIDs found in the OIDSET
 * indexed by the given iterator.
 *
 * https://github.com/microsoft/VFSForGit/blob/master/Protocol.md
 *
 * Return the number of OIDs we actually put into the payload.
 * If only 1 OID was found, also return it.
 */
static unsigned long build_json_payload__gvfs_objects(
	struct json_writer *jw_req,
	struct oidset_iter *iter,
	unsigned long nr_in_block,
	struct object_id *oid_out)
{
	unsigned long k;
	const struct object_id *oid;
	const struct object_id *oid_prev = NULL;

	k = 0;

	jw_init(jw_req);
	jw_object_begin(jw_req, 0);
	jw_object_intmax(jw_req, "commitDepth", gh__cmd_opts.depth);
	jw_object_inline_begin_array(jw_req, "objectIds");
	while (k < nr_in_block && (oid = oidset_iter_next(iter))) {
		jw_array_string(jw_req, oid_to_hex(oid));
		k++;
		oid_prev = oid;
	}
	jw_end(jw_req);
	jw_end(jw_req);

	if (oid_out) {
		if (k == 1)
			oidcpy(oid_out, oid_prev);
		else
			oidclr(oid_out);
	}

	return k;
}

/*
 * Lookup the creds for the main/origin Git server.
 */
static void lookup_main_creds(void)
{
	if (gh__global.main_creds.username && *gh__global.main_creds.username)
		return;

	credential_from_url(&gh__global.main_creds, gh__global.main_url);
	credential_fill(&gh__global.main_creds);
	gh__global.main_creds_need_approval = 1;
}

/*
 * If we have a set of creds for the main Git server, tell the credential
 * manager to throw them away and ask it to reacquire them.
 */
static void refresh_main_creds(void)
{
	if (gh__global.main_creds.username && *gh__global.main_creds.username)
		credential_reject(&gh__global.main_creds);

	lookup_main_creds();

	// TODO should we compare before and after values of u/p and
	// TODO shortcut reauth if we already know it will fail?
	// TODO if so, return a bool if same/different.
}

static void approve_main_creds(void)
{
	if (!gh__global.main_creds_need_approval)
		return;

	credential_approve(&gh__global.main_creds);
	gh__global.main_creds_need_approval = 0;
}

/*
 * Build a set of creds for the cache-server based upon the main Git
 * server (assuming we have a cache-server configured).
 *
 * That is, we NEVER fill them directly for the cache-server -- we
 * only synthesize them from the filled main creds.
 */
static void synthesize_cache_server_creds(void)
{
	if (!gh__global.cache_server_is_initialized)
		BUG("sub-command did not initialize cache-server vars");

	if (!gh__global.cache_server_url)
		return;

	if (gh__global.cache_creds.username && *gh__global.cache_creds.username)
		return;

	/*
	 * Get the main Git server creds so we can borrow the username
	 * and password when we talk to the cache-server.
	 */
	lookup_main_creds();
	gh__global.cache_creds.username = xstrdup(gh__global.main_creds.username);
	gh__global.cache_creds.password = xstrdup(gh__global.main_creds.password);
}

/*
 * Flush and refresh the cache-server creds.  Because the cache-server
 * does not do 401s (or manage creds), we have to reload the main Git
 * server creds first.
 *
 * That is, we NEVER reject them directly because we never filled them.
 */
static void refresh_cache_server_creds(void)
{
	credential_clear(&gh__global.cache_creds);

	refresh_main_creds();
	synthesize_cache_server_creds();
}

/*
 * We NEVER approve cache-server creds directly because we never directly
 * filled them.  However, we should be able to infer that the main ones
 * are valid and can approve them if necessary.
 */
static void approve_cache_server_creds(void)
{
	approve_main_creds();
}

/*
 * Get the pathname to the ODB where we write objects that we download.
 */
static void select_odb(void)
{
	prepare_alt_odb(the_repository);

	strbuf_init(&gh__global.buf_odb_path, 0);

	if (gvfs_shared_cache_pathname.len)
		strbuf_addbuf(&gh__global.buf_odb_path,
			      &gvfs_shared_cache_pathname);
	else
		strbuf_addstr(&gh__global.buf_odb_path,
			      the_repository->objects->odb->path);
}

/*
 * Create a unique tempfile or tempfile-pair inside the
 * tempPacks directory.
 */
static void my_create_tempfile(
	struct gh__response_status *status,
	int b_fdopen,
	const char *suffix1, struct tempfile **t1,
	const char *suffix2, struct tempfile **t2)
{
	static unsigned int nth = 0;
	static struct timeval tv = {0};
	static struct tm tm = {0};
	static time_t secs = 0;
	static char date[32] = {0};

	struct strbuf basename = STRBUF_INIT;
	struct strbuf buf = STRBUF_INIT;
	int len_tp;
	enum scld_error scld;

	gh__response_status__zero(status);

	if (!nth) {
		/*
		 * Create a unique <date> string to use in the name of all
		 * tempfiles created by this process.
		 */
		gettimeofday(&tv, NULL);
		secs = tv.tv_sec;
		gmtime_r(&secs, &tm);

		xsnprintf(date, sizeof(date), "%4d%02d%02d-%02d%02d%02d-%06ld",
			  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			  tm.tm_hour, tm.tm_min, tm.tm_sec,
			  (long)tv.tv_usec);
	}

	/*
	 * Create a <basename> for this instance/pair using a series
	 * number <n>.
	 */
	strbuf_addf(&basename, "t-%s-%04d", date, nth++);

	if (!suffix1 || !*suffix1)
		suffix1 = "temp";

	/*
	 * Create full pathname as:
	 *
	 *     "<odb>/pack/tempPacks/<basename>.<suffix1>"
	 */
	strbuf_setlen(&buf, 0);
	strbuf_addbuf(&buf, &gh__global.buf_odb_path);
	strbuf_complete(&buf, '/');
	strbuf_addstr(&buf, "pack/tempPacks/");
	len_tp = buf.len;
	strbuf_addf(  &buf, "%s.%s", basename.buf, suffix1);

	scld = safe_create_leading_directories(buf.buf);
	if (scld != SCLD_OK && scld != SCLD_EXISTS) {
		strbuf_addf(&status->error_message,
			    "could not create directory for tempfile: '%s'",
			    buf.buf);
		status->ec = GH__ERROR_CODE__COULD_NOT_CREATE_TEMPFILE;
		goto cleanup;
	}

	*t1 = create_tempfile(buf.buf);
	if (!*t1) {
		strbuf_addf(&status->error_message,
			    "could not create tempfile: '%s'",
			    buf.buf);
		status->ec = GH__ERROR_CODE__COULD_NOT_CREATE_TEMPFILE;
		goto cleanup;
	}
	if (b_fdopen)
		fdopen_tempfile(*t1, "w");

	/*
	 * Optionally create a peer tempfile with the same basename.
	 * (This is useful for prefetching .pack and .idx pairs.)
	 *
	 *     "<odb>/pack/tempPacks/<basename>.<suffix2>"
	 */
	if (suffix2 && *suffix2 && t2) {
		strbuf_setlen(&buf, len_tp);
		strbuf_addf(  &buf, "%s.%s", basename.buf, suffix2);

		*t2 = create_tempfile(buf.buf);
		if (!*t2) {
			strbuf_addf(&status->error_message,
				    "could not create tempfile: '%s'",
				    buf.buf);
			status->ec = GH__ERROR_CODE__COULD_NOT_CREATE_TEMPFILE;
			goto cleanup;
		}
		if (b_fdopen)
			fdopen_tempfile(*t2, "w");
	}

cleanup:
	strbuf_release(&buf);
	strbuf_release(&basename);
}

/*
 * Create pathnames to the final location of the .pack and .idx
 * files in the ODB.  These are of the form:
 *
 *    "<odb>/pack/<term_1>-<term_2>[-<term_3>].<suffix>"
 *
 * For example, for prefetch packs, <term_2> will be the epoch
 * timestamp and <term_3> will be the packfile hash.
 */
static void create_final_packfile_pathnames(
	const char *term_1, const char *term_2, const char *term_3,
	struct strbuf *pack_path, struct strbuf *idx_path,
	struct strbuf *pack_filename)
{
	struct strbuf base = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;

	if (term_3 && *term_3)
		strbuf_addf(&base, "%s-%s-%s", term_1, term_2, term_3);
	else
		strbuf_addf(&base, "%s-%s", term_1, term_2);

	strbuf_setlen(pack_filename, 0);
	strbuf_addf(  pack_filename, "%s.pack", base.buf);

	strbuf_addbuf(&path, &gh__global.buf_odb_path);
	strbuf_complete(&path, '/');
	strbuf_addstr(&path, "pack/");

	strbuf_setlen(pack_path, 0);
	strbuf_addbuf(pack_path, &path);
	strbuf_addf(  pack_path, "%s.pack", base.buf);

	strbuf_setlen(idx_path, 0);
	strbuf_addbuf(idx_path, &path);
	strbuf_addf(  idx_path, "%s.idx", base.buf);

	strbuf_release(&base);
	strbuf_release(&path);
}

/*
 * Create a pathname to the loose object in the shared-cache ODB
 * with the given OID.  Try to "mkdir -p" to ensure the parent
 * directories exist.
 */
static int create_loose_pathname_in_odb(struct strbuf *buf_path,
					const struct object_id *oid)
{
	enum scld_error scld;
	const char *hex;

	hex = oid_to_hex(oid);

	strbuf_setlen(buf_path, 0);
	strbuf_addbuf(buf_path, &gh__global.buf_odb_path);
	strbuf_complete(buf_path, '/');
	strbuf_add(buf_path, hex, 2);
	strbuf_addch(buf_path, '/');
	strbuf_addstr(buf_path, hex+2);

	scld = safe_create_leading_directories(buf_path->buf);
	if (scld != SCLD_OK && scld != SCLD_EXISTS)
		return -1;

	return 0;
}

static void my_run_index_pack(struct gh__request_params *params,
			      struct gh__response_status *status,
			      const struct strbuf *temp_path_pack,
			      const struct strbuf *temp_path_idx,
			      struct strbuf *packfile_checksum)
{
	struct child_process ip = CHILD_PROCESS_INIT;
	struct strbuf ip_stdout = STRBUF_INIT;

	strvec_push(&ip.args, "git");
	strvec_push(&ip.args, "index-pack");
	if (gh__cmd_opts.show_progress)
		strvec_push(&ip.args, "-v");
	strvec_pushl(&ip.args, "-o", temp_path_idx->buf, NULL);
	strvec_push(&ip.args, temp_path_pack->buf);
	ip.no_stdin = 1;
	ip.out = -1;
	ip.err = -1;

	if (pipe_command(&ip, NULL, 0, &ip_stdout, 0, NULL, 0)) {
		unlink(temp_path_pack->buf);
		unlink(temp_path_idx->buf);
		strbuf_addf(&status->error_message,
			    "index-pack failed on '%s'",
			    temp_path_pack->buf);
		/*
		 * Lets assume that index-pack failed because the
		 * downloaded file is corrupt (truncated).
		 *
		 * Retry it as if the network had dropped.
		 */
		status->retry = GH__RETRY_MODE__TRANSIENT;
		status->ec = GH__ERROR_CODE__INDEX_PACK_FAILED;
		goto cleanup;
	}

	if (packfile_checksum) {
		/*
		 * stdout from index-pack should have the packfile hash.
		 * Extract it and use it in the final packfile name.
		 *
		 * TODO What kind of validation should we do on the
		 * TODO string and is there ever any other output besides
		 * TODO just the checksum ?
		 */
		strbuf_trim_trailing_newline(&ip_stdout);

		strbuf_addbuf(packfile_checksum, &ip_stdout);
	}

cleanup:
	strbuf_release(&ip_stdout);
	child_process_clear(&ip);
}

static void my_finalize_packfile(struct gh__request_params *params,
				 struct gh__response_status *status,
				 int b_keep,
				 const struct strbuf *temp_path_pack,
				 const struct strbuf *temp_path_idx,
				 struct strbuf *final_path_pack,
				 struct strbuf *final_path_idx,
				 struct strbuf *final_filename)
{
	/*
	 * Install the .pack and .idx into the ODB pack directory.
	 *
	 * We might be racing with other instances of gvfs-helper if
	 * we, in parallel, both downloaded the exact same packfile
	 * (with the same checksum SHA) and try to install it at the
	 * same time.  This might happen on Windows where the loser
	 * can get an EBUSY or EPERM trying to move/rename the
	 * tempfile into the pack dir, for example.
	 *
	 * So, we always install the .pack before the .idx for
	 * consistency.  And only if *WE* created the .pack and .idx
	 * files, do we create the matching .keep (when requested).
	 *
	 * If we get an error and the target files already exist, we
	 * silently eat the error.  Note that finalize_object_file()
	 * has already munged errno (and it has various creation
	 * strategies), so we don't bother looking at it.
	 */
	if (finalize_object_file(temp_path_pack->buf, final_path_pack->buf) ||
	    finalize_object_file(temp_path_idx->buf, final_path_idx->buf)) {
		unlink(temp_path_pack->buf);
		unlink(temp_path_idx->buf);

		if (file_exists(final_path_pack->buf) &&
		    file_exists(final_path_idx->buf)) {
			trace2_printf("%s: assuming ok for %s", TR2_CAT, final_path_pack->buf);
			goto assume_ok;
		}

		strbuf_addf(&status->error_message,
			    "could not install packfile '%s'",
			    final_path_pack->buf);
		status->ec = GH__ERROR_CODE__COULD_NOT_INSTALL_PACKFILE;
		return;
	}

	if (b_keep) {
		struct strbuf keep = STRBUF_INIT;
		int fd_keep;

		strbuf_addbuf(&keep, final_path_pack);
		strbuf_strip_suffix(&keep, ".pack");
		strbuf_addstr(&keep, ".keep");

		fd_keep = xopen(keep.buf, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd_keep >= 0)
			close(fd_keep);

		strbuf_release(&keep);
	}

assume_ok:
	if (params->result_list) {
		struct strbuf result_msg = STRBUF_INIT;

		strbuf_addf(&result_msg, "packfile %s", final_filename->buf);
		string_list_append(params->result_list, result_msg.buf);
		strbuf_release(&result_msg);
	}
}

/*
 * Convert the tempfile into a temporary .pack, index it into a temporary .idx
 * file, and then install the pair into ODB.
 */
static void install_packfile(struct gh__request_params *params,
			     struct gh__response_status *status)
{
	struct strbuf temp_path_pack = STRBUF_INIT;
	struct strbuf temp_path_idx = STRBUF_INIT;
	struct strbuf packfile_checksum = STRBUF_INIT;
	struct strbuf final_path_pack = STRBUF_INIT;
	struct strbuf final_path_idx = STRBUF_INIT;
	struct strbuf final_filename = STRBUF_INIT;

	gh__response_status__zero(status);

	/*
	 * After the download is complete, we will need to steal the file
	 * from the tempfile() class (so that it doesn't magically delete
	 * it when we close the file handle) and then index it.
	 */
	strbuf_addf(&temp_path_pack, "%s.pack",
		    get_tempfile_path(params->tempfile));
	strbuf_addf(&temp_path_idx, "%s.idx",
		    get_tempfile_path(params->tempfile));

	if (rename_tempfile(&params->tempfile,
			    temp_path_pack.buf) == -1) {
		strbuf_addf(&status->error_message,
			    "could not rename packfile to '%s'",
			    temp_path_pack.buf);
		status->ec = GH__ERROR_CODE__COULD_NOT_INSTALL_PACKFILE;
		goto cleanup;
	}

	my_run_index_pack(params, status, &temp_path_pack, &temp_path_idx,
			  &packfile_checksum);
	if (status->ec != GH__ERROR_CODE__OK)
		goto cleanup;

	create_final_packfile_pathnames("vfs", packfile_checksum.buf, NULL,
					&final_path_pack, &final_path_idx,
					&final_filename);
	my_finalize_packfile(params, status, 0,
			     &temp_path_pack, &temp_path_idx,
			     &final_path_pack, &final_path_idx,
			     &final_filename);

cleanup:
	strbuf_release(&temp_path_pack);
	strbuf_release(&temp_path_idx);
	strbuf_release(&packfile_checksum);
	strbuf_release(&final_path_pack);
	strbuf_release(&final_path_idx);
	strbuf_release(&final_filename);
}

/*
 * bswap.h only defines big endian functions.
 * The GVFS Protocol defines fields in little endian.
 */
static inline uint64_t my_get_le64(uint64_t le_val)
{
#if GIT_BYTE_ORDER == GIT_LITTLE_ENDIAN
	return le_val;
#else
	return default_bswap64(le_val);
#endif
}

#define MY_MIN(x,y) (((x) < (y)) ? (x) : (y))
#define MY_MAX(x,y) (((x) > (y)) ? (x) : (y))

/*
 * Copy the `nr_bytes_total` from `fd_in` to `fd_out`.
 *
 * This could be used to extract a single packfile from
 * a multipart file, for example.
 */
static int my_copy_fd_len(int fd_in, int fd_out, ssize_t nr_bytes_total)
{
	char buffer[8192];

	while (nr_bytes_total > 0) {
		ssize_t len_to_read = MY_MIN(nr_bytes_total, sizeof(buffer));
		ssize_t nr_read = xread(fd_in, buffer, len_to_read);

		if (!nr_read)
			break;
		if (nr_read < 0)
			return -1;

		if (write_in_full(fd_out, buffer, nr_read) < 0)
			return -1;

		nr_bytes_total -= nr_read;
	}

	return 0;
}

/*
 * Copy the `nr_bytes_total` from `fd_in` to `fd_out` AND save the
 * final `tail_len` bytes in the given buffer.
 *
 * This could be used to extract a single packfile from
 * a multipart file and read the final SHA into the buffer.
 */
static int my_copy_fd_len_tail(int fd_in, int fd_out, ssize_t nr_bytes_total,
			       unsigned char *buf_tail, ssize_t tail_len)
{
	memset(buf_tail, 0, tail_len);

	if (my_copy_fd_len(fd_in, fd_out, nr_bytes_total) < 0)
		return -1;

	if (nr_bytes_total < tail_len)
		return 0;

	/* Reset the position to read the tail */
	lseek(fd_in, -tail_len, SEEK_CUR);

	if (xread(fd_in, (char *)buf_tail, tail_len) != tail_len)
		return -1;

	return 0;
}

/*
 * See the protocol document for the per-packfile header.
 */
struct ph {
	uint64_t timestamp;
	uint64_t pack_len;
	uint64_t idx_len;
};

/*
 * Extract the next packfile from the multipack.
 * Install {.pack, .idx, .keep} set.
 *
 * Mark each successfully installed prefetch pack as .keep it as installed
 * in case we have errors decoding/indexing later packs within the received
 * multipart file.  (A later pass can delete the unnecessary .keep files
 * from this and any previous invocations.)
 */
static void extract_packfile_from_multipack(
	struct gh__request_params *params,
	struct gh__response_status *status,
	int fd_multipack,
	unsigned short k)
{
	struct ph ph;
	struct tempfile *tempfile_pack = NULL;
	struct tempfile *tempfile_idx = NULL;
	int result = -1;
	int b_no_idx_in_multipack;
	struct object_id packfile_checksum;
	char hex_checksum[GIT_MAX_HEXSZ + 1];
	struct strbuf buf_timestamp = STRBUF_INIT;
	struct strbuf temp_path_pack = STRBUF_INIT;
	struct strbuf temp_path_idx = STRBUF_INIT;
	struct strbuf final_path_pack = STRBUF_INIT;
	struct strbuf final_path_idx = STRBUF_INIT;
	struct strbuf final_filename = STRBUF_INIT;

	if (xread(fd_multipack, &ph, sizeof(ph)) != sizeof(ph)) {
		strbuf_addf(&status->error_message,
			    "could not read header for packfile[%d] in multipack",
			    k);
		status->ec = GH__ERROR_CODE__COULD_NOT_INSTALL_PREFETCH;
		goto done;
	}

	ph.timestamp = my_get_le64(ph.timestamp);
	ph.pack_len = my_get_le64(ph.pack_len);
	ph.idx_len = my_get_le64(ph.idx_len);

	if (!ph.pack_len) {
		strbuf_addf(&status->error_message,
			    "packfile[%d]: zero length packfile?", k);
		status->ec = GH__ERROR_CODE__COULD_NOT_INSTALL_PREFETCH;
		goto done;
	}

	b_no_idx_in_multipack = (ph.idx_len == maximum_unsigned_value_of_type(uint64_t) ||
				 ph.idx_len == 0);

	if (b_no_idx_in_multipack) {
		my_create_tempfile(status, 0, "pack", &tempfile_pack, NULL, NULL);
		if (!tempfile_pack)
			goto done;
	} else {
		/* create a pair of tempfiles with the same basename */
		my_create_tempfile(status, 0, "pack", &tempfile_pack, "idx", &tempfile_idx);
		if (!tempfile_pack || !tempfile_idx)
			goto done;
	}

	/*
	 * Copy the current packfile from the open stream and capture
	 * the checksum.
	 *
	 * TODO This assumes that the checksum is SHA1.  Fix this if/when
	 * TODO Git converts to SHA256.
	 */
	result = my_copy_fd_len_tail(fd_multipack,
				     get_tempfile_fd(tempfile_pack),
				     ph.pack_len,
				     packfile_checksum.hash,
				     GIT_SHA1_RAWSZ);
	if (result < 0){
		strbuf_addf(&status->error_message,
			    "could not extract packfile[%d] from multipack",
			    k);
		goto done;
	}
	strbuf_addstr(&temp_path_pack, get_tempfile_path(tempfile_pack));
	close_tempfile_gently(tempfile_pack);

	oid_to_hex_r(hex_checksum, &packfile_checksum);

	if (b_no_idx_in_multipack) {
		/*
		 * The server did not send the corresponding .idx, so
		 * we have to compute it ourselves.
		 */
		strbuf_addbuf(&temp_path_idx, &temp_path_pack);
		strbuf_strip_suffix(&temp_path_idx, ".pack");
		strbuf_addstr(&temp_path_idx, ".idx");

		my_run_index_pack(params, status,
				  &temp_path_pack, &temp_path_idx,
				  NULL);
		if (status->ec != GH__ERROR_CODE__OK)
			goto done;

	} else {
		/*
		 * Server sent the .idx immediately after the .pack in the
		 * data stream.  I'm tempted to verify it, but that defeats
		 * the purpose of having it cached...
		 */
		if (my_copy_fd_len(fd_multipack, get_tempfile_fd(tempfile_idx),
				   ph.idx_len) < 0) {
			strbuf_addf(&status->error_message,
				    "could not extract index[%d] in multipack",
				    k);
			status->ec = GH__ERROR_CODE__COULD_NOT_INSTALL_PREFETCH;
			goto done;
		}

		strbuf_addstr(&temp_path_idx, get_tempfile_path(tempfile_idx));
		close_tempfile_gently(tempfile_idx);
	}

	strbuf_addf(&buf_timestamp, "%u", (unsigned int)ph.timestamp);
	create_final_packfile_pathnames("prefetch", buf_timestamp.buf, hex_checksum,
					&final_path_pack, &final_path_idx,
					&final_filename);

	my_finalize_packfile(params, status, 1,
			     &temp_path_pack, &temp_path_idx,
			     &final_path_pack, &final_path_idx,
			     &final_filename);

done:
	delete_tempfile(&tempfile_pack);
	delete_tempfile(&tempfile_idx);
	strbuf_release(&temp_path_pack);
	strbuf_release(&temp_path_idx);
	strbuf_release(&final_path_pack);
	strbuf_release(&final_path_idx);
	strbuf_release(&final_filename);
}

struct keep_files_data {
	timestamp_t max_timestamp;
	int pos_of_max;
	struct string_list *keep_files;
};

static void cb_keep_files(const char *full_path, size_t full_path_len,
			  const char *file_path, void *void_data)
{
	struct keep_files_data *data = void_data;
	const char *val;
	timestamp_t t;

	/*
	 * We expect prefetch packfiles named like:
	 *
	 *     prefetch-<seconds>-<checksum>.keep
	 */
	if (!skip_prefix(file_path, "prefetch-", &val))
		return;
	if (!ends_with(val, ".keep"))
		return;

	t = strtol(val, NULL, 10);
	if (t > data->max_timestamp) {
		data->pos_of_max = data->keep_files->nr;
		data->max_timestamp = t;
	}

	string_list_append(data->keep_files, full_path);
}

static void delete_stale_keep_files(
	struct gh__request_params *params,
	struct gh__response_status *status)
{
	struct string_list keep_files = STRING_LIST_INIT_DUP;
	struct keep_files_data data = { 0, 0, &keep_files };
	int k;

	for_each_file_in_pack_dir(gh__global.buf_odb_path.buf,
				  cb_keep_files, &data);
	for (k = 0; k < keep_files.nr; k++) {
		if (k != data.pos_of_max)
			unlink(keep_files.items[k].string);
	}

	string_list_clear(&keep_files, 0);
}

/*
 * Cut apart the received multipart response into individual packfiles
 * and install each one.
 */
static void install_prefetch(struct gh__request_params *params,
			     struct gh__response_status *status)
{
	static unsigned char v1_h[6] = { 'G', 'P', 'R', 'E', ' ', 0x01 };

	struct mh {
		unsigned char h[6];
		unsigned char np[2];
	};

	struct mh mh;
	unsigned short np;
	unsigned short k;
	int fd = -1;
	int nr_installed = 0;

	struct strbuf temp_path_mp = STRBUF_INIT;

	/*
	 * Steal the multi-part file from the tempfile class.
	 */
	strbuf_addf(&temp_path_mp, "%s.mp", get_tempfile_path(params->tempfile));
	if (rename_tempfile(&params->tempfile, temp_path_mp.buf) == -1) {
		strbuf_addf(&status->error_message,
			    "could not rename prefetch tempfile to '%s'",
			    temp_path_mp.buf);
		status->ec = GH__ERROR_CODE__COULD_NOT_INSTALL_PREFETCH;
		goto cleanup;
	}

	fd = git_open_cloexec(temp_path_mp.buf, O_RDONLY);
	if (fd == -1) {
		strbuf_addf(&status->error_message,
			    "could not reopen prefetch tempfile '%s'",
			    temp_path_mp.buf);
		status->ec = GH__ERROR_CODE__COULD_NOT_INSTALL_PREFETCH;
		goto cleanup;
	}

	if ((xread(fd, &mh, sizeof(mh)) != sizeof(mh)) ||
	    (memcmp(mh.h, &v1_h, sizeof(mh.h)))) {
		strbuf_addstr(&status->error_message,
			      "invalid prefetch multipart header");
		goto cleanup;
	}

	np = (unsigned short)mh.np[0] + ((unsigned short)mh.np[1] << 8);
	if (np)
		trace2_data_intmax(TR2_CAT, NULL,
				   "prefetch/packfile_count", np);

	for (k = 0; k < np; k++) {
		extract_packfile_from_multipack(params, status, fd, k);
		if (status->ec != GH__ERROR_CODE__OK)
			break;
		nr_installed++;
	}

	if (nr_installed)
		delete_stale_keep_files(params, status);

cleanup:
	if (fd != -1)
		close(fd);

	unlink(temp_path_mp.buf);
	strbuf_release(&temp_path_mp);
}

/*
 * Convert the tempfile into a permanent loose object in the ODB.
 */
static void install_loose(struct gh__request_params *params,
			  struct gh__response_status *status)
{
	struct strbuf tmp_path = STRBUF_INIT;
	struct strbuf loose_path = STRBUF_INIT;

	gh__response_status__zero(status);

	/*
	 * close tempfile to steal ownership away from tempfile class.
	 */
	strbuf_addstr(&tmp_path, get_tempfile_path(params->tempfile));
	close_tempfile_gently(params->tempfile);

	/*
	 * Try to install the tempfile as the actual loose object.
	 *
	 * If the loose object already exists, finalize_object_file()
	 * will NOT overwrite/replace it.  It will silently eat the
	 * EEXIST error and unlink the tempfile as it if was
	 * successful.  We just let it lie to us.
	 *
	 * Since our job is to back-fill missing objects needed by a
	 * foreground git process -- git should have called
	 * oid_object_info_extended() and loose_object_info() BEFORE
	 * asking us to download the missing object.  So if we get a
	 * collision we have to assume something else is happening in
	 * parallel and we lost the race.  And that's OK.
	 */
	if (create_loose_pathname_in_odb(&loose_path, &params->loose_oid)) {
		strbuf_addf(&status->error_message,
			    "cannot create directory for loose object '%s'",
			    loose_path.buf);
		status->ec = GH__ERROR_CODE__COULD_NOT_INSTALL_LOOSE;
		goto cleanup;
	}

	if (finalize_object_file(tmp_path.buf, loose_path.buf)) {
		unlink(tmp_path.buf);
		strbuf_addf(&status->error_message,
			    "could not install loose object '%s'",
			    loose_path.buf);
		status->ec = GH__ERROR_CODE__COULD_NOT_INSTALL_LOOSE;
		goto cleanup;
	}

	if (params->result_list) {
		struct strbuf result_msg = STRBUF_INIT;

		strbuf_addf(&result_msg, "loose %s",
			    oid_to_hex(&params->loose_oid));
		string_list_append(params->result_list, result_msg.buf);
		strbuf_release(&result_msg);
	}

cleanup:
	strbuf_release(&tmp_path);
	strbuf_release(&loose_path);
}

static void install_result(struct gh__request_params *params,
			   struct gh__response_status *status)
{
	if (params->objects_mode == GH__OBJECTS_MODE__PREFETCH) {
		/*
		 * The "gvfs/prefetch" API is the only thing that sends
		 * these multi-part packfiles.  According to the procotol
		 * documentation, they will have this x- content type.
		 *
		 * However, it appears that there is a BUG in the origin
		 * server causing it to sometimes send "text/html" instead.
		 * So, we silently handle both.
		 */
		if (!strcmp(status->content_type.buf,
			    "application/x-gvfs-timestamped-packfiles-indexes")) {
			install_prefetch(params, status);
			return;
		}

		if (!strcmp(status->content_type.buf, "text/html")) {
			install_prefetch(params, status);
			return;
		}
	} else {
		if (!strcmp(status->content_type.buf, "application/x-git-packfile")) {
			assert(params->b_is_post);
			assert(params->objects_mode == GH__OBJECTS_MODE__POST);

			install_packfile(params, status);
			return;
		}

		if (!strcmp(status->content_type.buf,
			"application/x-git-loose-object")) {
			/*
			* We get these for "gvfs/objects" GET and POST requests.
			*
			* Note that this content type is singular, not plural.
			*/
			install_loose(params, status);
			return;
		}
	}

	strbuf_addf(&status->error_message,
		    "install_result: received unknown content-type '%s'",
		    status->content_type.buf);
	status->ec = GH__ERROR_CODE__UNEXPECTED_CONTENT_TYPE;
}

/*
 * Our wrapper to initialize the HTTP layer.
 *
 * We always use the real origin server, not the cache-server, when
 * initializing the http/curl layer.
 */
static void gh_http_init(void)
{
	if (gh__global.http_is_initialized)
		return;

	http_init(gh__global.remote, gh__global.main_url, 0);
	gh__global.http_is_initialized = 1;
}

static void gh_http_cleanup(void)
{
	if (!gh__global.http_is_initialized)
		return;

	http_cleanup();
	gh__global.http_is_initialized = 0;
}

/*
 * buffer has "<key>: <value>[\r]\n"
 */
static void parse_resp_hdr_1(const char *buffer, size_t size, size_t nitems,
			     struct strbuf *key, struct strbuf *value)
{
	const char *end = buffer + (size * nitems);
	const char *p;

	p = strchr(buffer, ':');

	strbuf_setlen(key, 0);
	strbuf_add(key, buffer, (p - buffer));

	p++; /* skip ':' */
	p++; /* skip ' ' */

	strbuf_setlen(value, 0);
	strbuf_add(value, p, (end - p));
	strbuf_trim_trailing_newline(value);
}

static size_t parse_resp_hdr(char *buffer, size_t size, size_t nitems,
			     void *void_params)
{
	struct gh__request_params *params = void_params;
	struct gh__azure_throttle *azure = &gh__global_throttle[params->server_type];

	if (starts_with(buffer, "X-RateLimit-")) {
		struct strbuf key = STRBUF_INIT;
		struct strbuf val = STRBUF_INIT;

		parse_resp_hdr_1(buffer, size, nitems, &key, &val);

		/*
		 * The following X- headers are specific to AzureDevOps.
		 * Other servers have similar sets of values, but I haven't
		 * compared them in depth.
		 */
		// trace2_printf("%s: Throttle: %s %s", TR2_CAT, key.buf, val.buf);

		if (!strcmp(key.buf, "X-RateLimit-Resource")) {
			/*
			 * The name of the resource that is complaining.
			 * Just log it because we can't do anything with it.
			 */
			strbuf_setlen(&key, 0);
			strbuf_addstr(&key, "ratelimit/resource");
			strbuf_addstr(&key, gh__server_type_label[params->server_type]);

			trace2_data_string(TR2_CAT, NULL, key.buf, val.buf);
		}

		else if (!strcmp(key.buf, "X-RateLimit-Delay")) {
			/*
			 * The amount of delay added to our response.
			 * Just log it because we can't do anything with it.
			 */
			unsigned long tarpit_delay_ms;

			strbuf_setlen(&key, 0);
			strbuf_addstr(&key, "ratelimit/delay_ms");
			strbuf_addstr(&key, gh__server_type_label[params->server_type]);

			git_parse_ulong(val.buf, &tarpit_delay_ms);

			trace2_data_intmax(TR2_CAT, NULL, key.buf, tarpit_delay_ms);
		}

		else if (!strcmp(key.buf, "X-RateLimit-Limit")) {
			/*
			 * The resource limit/quota before we get a 429.
			 */
			git_parse_ulong(val.buf, &azure->tstu_limit);
		}

		else if (!strcmp(key.buf, "X-RateLimit-Remaining")) {
			/*
			 * The amount of our quota remaining.  When zero, we
			 * should get 429s on futher requests until the reset
			 * time.
			 */
			git_parse_ulong(val.buf, &azure->tstu_remaining);
		}

		else if (!strcmp(key.buf, "X-RateLimit-Reset")) {
			/*
			 * The server gave us a time-in-seconds-since-the-epoch
			 * for when our quota will be reset (if we stop all
			 * activity right now).
			 *
			 * Checkpoint the local system clock so we can do some
			 * sanity checks on any clock skew.  Also, since we get
			 * the headers before we get the content, we can adjust
			 * our delay to compensate for the full download time.
			 */
			unsigned long now = time(NULL);
			unsigned long reset_time;

			git_parse_ulong(val.buf, &reset_time);
			if (reset_time > now)
				azure->reset_sec = reset_time - now;
		}

		strbuf_release(&key);
		strbuf_release(&val);
	}

	else if (starts_with(buffer, "Retry-After")) {
		struct strbuf key = STRBUF_INIT;
		struct strbuf val = STRBUF_INIT;

		parse_resp_hdr_1(buffer, size, nitems, &key, &val);

		/*
		 * We get this header with a 429 and 503 and possibly a 30x.
		 *
		 * Curl does have CURLINFO_RETRY_AFTER that nicely parses and
		 * normalizes the value (and supports HTTP/1.1 usage), but it
		 * is not present yet in the version shipped with the Mac, so
		 * we do it directly here.
		 */
		git_parse_ulong(val.buf, &azure->retry_after_sec);

		strbuf_release(&key);
		strbuf_release(&val);
	}

	else if (starts_with(buffer, "X-VSS-E2EID")) {
		struct strbuf key = STRBUF_INIT;

		/*
		 * Capture the E2EID as it goes by, but don't log it until we
		 * know the request result.
		 */
		parse_resp_hdr_1(buffer, size, nitems, &key, &params->e2eid);

		strbuf_release(&key);
	}

	return nitems * size;
}

/*
 * Wait "duration" seconds and drive the progress mechanism.
 *
 * We spin slightly faster than we need to to keep the progress bar
 * drawn (especially if the user presses return while waiting) and to
 * compensate for delay factors built into the progress class (which
 * might wait for 2 seconds before drawing the first message).
 */
static void do_throttle_spin(struct gh__request_params *params,
			     const char *tr2_label,
			     const char *progress_msg,
			     int duration)
{
	struct strbuf region = STRBUF_INIT;
	struct progress *progress = NULL;
	unsigned long begin = time(NULL);
	unsigned long now = begin;
	unsigned long end = begin + duration;

	strbuf_addstr(&region, tr2_label);
	strbuf_addstr(&region, gh__server_type_label[params->server_type]);
	trace2_region_enter(TR2_CAT, region.buf, NULL);

	if (gh__cmd_opts.show_progress)
		progress = start_progress(progress_msg, duration);

	while (now < end) {
		display_progress(progress, (now - begin));

		sleep_millisec(100);

		now = time(NULL);
	}

	display_progress(progress, duration);
	stop_progress(&progress);

	trace2_region_leave(TR2_CAT, region.buf, NULL);
	strbuf_release(&region);
}

/*
 * Delay the outbound request if necessary in response to previous throttle
 * blockages or hints.  Throttle data is somewhat orthogonal to the status
 * results from any previous request and/or the request params of the next
 * request.
 *
 * Note that the throttle info also is cross-process information, such as
 * 2 concurrent fetches in 2 different terminal windows to the same server
 * will be sharing the same server quota.  These could be coordinated too,
 * so that a blockage received in one process would prevent the other
 * process from starting another request (and also blocked or extending
 * the delay interval).  We're NOT going to do that level of integration.
 * We will let both processes independently attempt the next request.
 * This may cause us to miss the end-of-quota boundary if the server
 * extends it because of the second request.
 *
 * TODO Should we have a max-wait option and then return a hard-error
 * TODO of some type?
 */
static void do_throttle_wait(struct gh__request_params *params,
			     struct gh__response_status *status)
{
	struct gh__azure_throttle *azure =
		&gh__global_throttle[params->server_type];

	if (azure->retry_after_sec) {
		/*
		 * We were given a hard delay (such as after a 429).
		 * Spin until the requested time.
		 */
		do_throttle_spin(params, "throttle/hard",
				 "Waiting on hard throttle (sec)",
				 azure->retry_after_sec);
		return;
	}

	if (azure->reset_sec > 0) {
		/*
		 * We were given a hint that we are overloading
		 * the server.  Voluntarily backoff (before we
		 * get tarpitted or blocked).
		 */
		do_throttle_spin(params, "throttle/soft",
				 "Waiting on soft throttle (sec)",
				 azure->reset_sec);
		return;
	}

	if (params->k_transient_delay_sec) {
		/*
		 * Insert an arbitrary delay before retrying after a
		 * transient (network) failure.
		 */
		do_throttle_spin(params, "throttle/transient",
				 "Waiting to retry after network error (sec)",
				 params->k_transient_delay_sec);
		return;
	}
}

/*
 * Do a single HTTP request WITHOUT robust-retry, auth-retry or fallback.
 */
static void do_req(const char *url_base,
		   const char *url_component,
		   const struct credential *creds,
		   struct gh__request_params *params,
		   struct gh__response_status *status)
{
	struct active_request_slot *slot;
	struct slot_results results;
	struct strbuf rest_url = STRBUF_INIT;

	gh__response_status__zero(status);

	if (params->b_write_to_file) {
		/* Delete dirty tempfile from a previous attempt. */
		if (params->tempfile)
			delete_tempfile(&params->tempfile);

		my_create_tempfile(status, 1, NULL, &params->tempfile, NULL, NULL);
		if (!params->tempfile || status->ec != GH__ERROR_CODE__OK)
			return;
	} else {
		/* Guard against caller using dirty buffer */
		strbuf_setlen(params->buffer, 0);
	}

	end_url_with_slash(&rest_url, url_base);
	strbuf_addstr(&rest_url, url_component);

	do_throttle_wait(params, status);
	gh__azure_throttle__zero(&gh__global_throttle[params->server_type]);

	slot = get_active_slot();
	slot->results = &results;

	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 0); /* not a HEAD request */
	curl_easy_setopt(slot->curl, CURLOPT_URL, rest_url.buf);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, params->headers);

	if (params->b_is_post) {
		curl_easy_setopt(slot->curl, CURLOPT_POST, 1);
		curl_easy_setopt(slot->curl, CURLOPT_ENCODING, NULL);
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDS,
				 params->post_payload->buf);
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDSIZE,
				 params->post_payload->len);
	} else {
		curl_easy_setopt(slot->curl, CURLOPT_POST, 0);
	}

	if (params->b_write_to_file) {
		curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
		curl_easy_setopt(slot->curl, CURLOPT_WRITEDATA,
				 (void*)params->tempfile->fp);
	} else {
		curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION,
				 fwrite_buffer);
		curl_easy_setopt(slot->curl, CURLOPT_FILE, params->buffer);
	}

	curl_easy_setopt(slot->curl, CURLOPT_HEADERFUNCTION, parse_resp_hdr);
	curl_easy_setopt(slot->curl, CURLOPT_HEADERDATA, params);

	if (creds && creds->username) {
		/*
		 * Force CURL to respect the username/password we provide by
		 * turning off the AUTH-ANY negotiation stuff.
		 *
		 * That is, CURLAUTH_ANY causes CURL to NOT send the creds
		 * on an initial request in order to force a 401 and let it
		 * negotiate the best auth scheme and then retry.
		 *
		 * This is problematic when talking to the cache-servers
		 * because they send a 400 (with a "A valid Basic Auth..."
		 * message body) rather than a 401.  This means that the
		 * the automatic retry will never happen.  And even if we
		 * do force a retry, CURL still won't send the creds.
		 *
		 * So we turn it off and force it use our creds.
		 */
		curl_easy_setopt(slot->curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
		curl_easy_setopt(slot->curl, CURLOPT_USERNAME, creds->username);
		curl_easy_setopt(slot->curl, CURLOPT_PASSWORD, creds->password);
	} else {
		/*
		 * Turn on the AUTH-ANY negotiation.  This only works
		 * with the main Git server (because the cache-server
		 * doesn't handle 401s).
		 *
		 * TODO Think about if we really need to handle this case.
		 * TODO Guard with "if (params->sever_type == __MAIN)"
		 */
		curl_easy_setopt(slot->curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	}

	if (params->progress_base_phase2_msg.len ||
	    params->progress_base_phase3_msg.len) {
		curl_easy_setopt(slot->curl, CURLOPT_XFERINFOFUNCTION,
				 gh__curl_progress_cb);
		curl_easy_setopt(slot->curl, CURLOPT_XFERINFODATA, params);
		curl_easy_setopt(slot->curl, CURLOPT_NOPROGRESS, 0);
	} else {
		curl_easy_setopt(slot->curl, CURLOPT_NOPROGRESS, 1);
	}

	gh__run_one_slot(slot, params, status);
}

/*
 * Compute the delay for the nth attempt.
 *
 * No delay for the first attempt. Then use a normal exponential backoff
 * starting from 8.
 */
static int compute_transient_delay(int attempt)
{
	int v;

	if (attempt < 1)
		return 0;

	/*
	 * Let 8K be our hard limit (for integer overflow protection).
	 * That's over 2 hours.  This is 8<<10.
	 */
	if (attempt > 10)
		attempt = 10;

	v = 8 << (attempt - 1);

	if (v > gh__cmd_opts.max_transient_backoff_sec)
		v = gh__cmd_opts.max_transient_backoff_sec;

	return v;
}

/*
 * Robustly make an HTTP request.  Retry if necessary to hide common
 * transient network errors and/or 429 blockages.
 *
 * For a transient (network) failure (where we do not have a throttle
 * delay factor), we should insert a small delay to let the network
 * recover.  The outage might be because the VPN dropped, or the
 * machine went to sleep or something and we want to give the network
 * time to come back up.  Insert AI here :-)
 */
static void do_req__with_robust_retry(const char *url_base,
				      const char *url_component,
				      const struct credential *creds,
				      struct gh__request_params *params,
				      struct gh__response_status *status)
{
	for (params->k_attempt = 0;
	     params->k_attempt < gh__cmd_opts.max_retries + 1;
	     params->k_attempt++) {

		do_req(url_base, url_component, creds, params, status);

		switch (status->retry) {
		default:
		case GH__RETRY_MODE__SUCCESS:
		case GH__RETRY_MODE__HTTP_401: /* caller does auth-retry */
		case GH__RETRY_MODE__HARD_FAIL:
		case GH__RETRY_MODE__FAIL_404:
			return;

		case GH__RETRY_MODE__HTTP_429:
		case GH__RETRY_MODE__HTTP_503:
			/*
			 * We should have gotten a "Retry-After" header with
			 * these and that gives us the wait time.  If not,
			 * fallthru and use the backoff delay.
			 */
			if (gh__global_throttle[params->server_type].retry_after_sec)
				continue;
			/*fallthru*/

		case GH__RETRY_MODE__TRANSIENT:
			params->k_transient_delay_sec =
				compute_transient_delay(params->k_attempt);
			continue;
		}
	}
}

static void do_req__to_main(const char *url_component,
			    struct gh__request_params *params,
			    struct gh__response_status *status)
{
	params->server_type = GH__SERVER_TYPE__MAIN;

	/*
	 * When talking to the main Git server, we do allow non-auth'd
	 * initial requests (mainly for testing, since production servers
	 * will usually always want creds), so we DO NOT force load the
	 * user's creds here and let the normal 401 mechanism handle things.
	 * This has a slight perf penalty -- if we're bulk fetching 4000
	 * objects from a production server, we're going to do a 100K+ byte
	 * POST just to get a 401 and then repeat the 100K+ byte POST
	 * with the creds.
	 *
	 * TODO Consider making a test or runtime flag to alter this.
	 * TODO If set/not-set, add a call here to pre-populate the creds.
	 * TODO
	 * TODO     lookup_main_creds();
	 * TODO
	 *
	 * TODO Testing with GIT_TRACE_CURL shows that curl will *SOMETIME*
	 * TODO send an "Expect: 100-continue" and silently handle/eat the
	 * TODO "HTTP/1.1 100 Continue" before the POST-body is sent,
	 * TODO so maybe this isn't an issue.  (Other than the extra
	 * TODO roundtrip for the first set of headers.)  More testing here
	 * TODO should be performed.
	 */

	do_req__with_robust_retry(gh__global.main_url, url_component,
				  &gh__global.main_creds,
				  params, status);

	if (status->retry == GH__RETRY_MODE__HTTP_401) {
		refresh_main_creds();

		do_req__with_robust_retry(gh__global.main_url, url_component,
					  &gh__global.main_creds,
					  params, status);
	}

	if (status->retry == GH__RETRY_MODE__SUCCESS)
		approve_main_creds();
}

static void do_req__to_cache_server(const char *url_component,
				    struct gh__request_params *params,
				    struct gh__response_status *status)
{
	params->server_type = GH__SERVER_TYPE__CACHE;

	synthesize_cache_server_creds();

	do_req__with_robust_retry(gh__global.cache_server_url, url_component,
				  &gh__global.cache_creds,
				  params, status);

	if (status->retry == GH__RETRY_MODE__HTTP_401) {
		refresh_cache_server_creds();

		do_req__with_robust_retry(gh__global.cache_server_url,
					  url_component,
					  &gh__global.cache_creds,
					  params, status);
	}

	if (status->retry == GH__RETRY_MODE__SUCCESS)
		approve_cache_server_creds();
}

/*
 * Try the cache-server (if configured) then fall-back to the main Git server.
 */
static void do_req__with_fallback(const char *url_component,
				  struct gh__request_params *params,
				  struct gh__response_status *status)
{
	if (gh__global.cache_server_url &&
	    params->b_permit_cache_server_if_defined) {
		do_req__to_cache_server(url_component, params, status);

		if (status->retry == GH__RETRY_MODE__SUCCESS)
			return;

		if (!gh__cmd_opts.try_fallback)
			return;

		/*
		 * The cache-server shares creds with the main Git server,
		 * so if our creds failed against the cache-server, they
		 * will also fail against the main Git server.  We just let
		 * this fail.
		 *
		 * Falling-back would likely just cause the 3rd (or maybe
		 * 4th) cred prompt.
		 */
		if (status->retry == GH__RETRY_MODE__HTTP_401)
			return;
	}

	do_req__to_main(url_component, params, status);
}

/*
 * Call "gvfs/config" REST API.
 *
 * Return server's response buffer.  This is probably a raw JSON string.
 */
static void do__http_get__gvfs_config(struct gh__response_status *status,
				      struct strbuf *config_data)
{
	struct gh__request_params params = GH__REQUEST_PARAMS_INIT;

	strbuf_addstr(&params.tr2_label, "GET/config");

	params.b_is_post = 0;
	params.b_write_to_file = 0;
	/* cache-servers do not handle gvfs/config REST calls */
	params.b_permit_cache_server_if_defined = 0;
	params.buffer = config_data;
	params.objects_mode = GH__OBJECTS_MODE__NONE;

	params.object_count = 1; /* a bit of a lie */

	/*
	 * "X-TFS-FedAuthRedirect: Suppress" disables the 302 + 203 redirect
	 * sequence to a login page and forces the main Git server to send a
	 * normal 401.
	 */
	params.headers = http_copy_default_headers();
	params.headers = curl_slist_append(params.headers,
					   "X-TFS-FedAuthRedirect: Suppress");
	params.headers = curl_slist_append(params.headers,
					   "Pragma: no-cache");

	if (gh__cmd_opts.show_progress) {
		/*
		 * gvfs/config has a very small reqest payload, so I don't
		 * see any need to report progress on the upload side of
		 * the GET.  So just report progress on the download side.
		 */
		strbuf_addstr(&params.progress_base_phase3_msg,
			      "Receiving gvfs/config");
	}

	do_req__with_fallback("gvfs/config", &params, status);

	gh__request_params__release(&params);
}

static void setup_gvfs_objects_progress(struct gh__request_params *params,
					unsigned long num, unsigned long den)
{
	if (!gh__cmd_opts.show_progress)
		return;

	if (params->b_is_post) {
		strbuf_addf(&params->progress_base_phase3_msg,
			    "Receiving packfile %ld/%ld with %ld objects",
			    num, den, params->object_count);
	}
	/* If requesting only one object, then do not show progress */
}

/*
 * Call "gvfs/objects/<oid>" REST API to fetch a loose object
 * and write it to the ODB.
 */
static void do__http_get__gvfs_object(struct gh__response_status *status,
				      const struct object_id *oid,
				      unsigned long l_num, unsigned long l_den,
				      struct string_list *result_list)
{
	struct gh__request_params params = GH__REQUEST_PARAMS_INIT;
	struct strbuf component_url = STRBUF_INIT;

	gh__response_status__zero(status);

	strbuf_addf(&component_url, "gvfs/objects/%s", oid_to_hex(oid));

	strbuf_addstr(&params.tr2_label, "GET/objects");

	params.b_is_post = 0;
	params.b_write_to_file = 1;
	params.b_permit_cache_server_if_defined = 1;
	params.objects_mode = GH__OBJECTS_MODE__GET;

	params.object_count = 1;

	params.result_list = result_list;

	params.headers = http_copy_default_headers();
	params.headers = curl_slist_append(params.headers,
					   "X-TFS-FedAuthRedirect: Suppress");
	params.headers = curl_slist_append(params.headers,
					   "Pragma: no-cache");

	oidcpy(&params.loose_oid, oid);

	setup_gvfs_objects_progress(&params, l_num, l_den);

	do_req__with_fallback(component_url.buf, &params, status);

	gh__request_params__release(&params);
	strbuf_release(&component_url);
}

/*
 * Call "gvfs/objects" POST REST API to fetch a batch of objects
 * from the OIDSET.  Normal, this is results in a packfile containing
 * `nr_wanted_in_block` objects.  And we return the number actually
 * consumed (along with the filename of the resulting packfile).
 *
 * However, if we only have 1 oid (remaining) in the OIDSET, the
 * server *MAY* respond to our POST with a loose object rather than
 * a packfile with 1 object.
 *
 * Append a message to the result_list describing the result.
 *
 * Return the number of OIDs consumed from the OIDSET.
 */
static void do__http_post__gvfs_objects(struct gh__response_status *status,
					struct oidset_iter *iter,
					unsigned long nr_wanted_in_block,
					int j_pack_num, int j_pack_den,
					struct string_list *result_list,
					unsigned long *nr_oid_taken)
{
	struct json_writer jw_req = JSON_WRITER_INIT;
	struct gh__request_params params = GH__REQUEST_PARAMS_INIT;

	gh__response_status__zero(status);

	params.object_count = build_json_payload__gvfs_objects(
		&jw_req, iter, nr_wanted_in_block, &params.loose_oid);
	*nr_oid_taken = params.object_count;

	strbuf_addstr(&params.tr2_label, "POST/objects");

	params.b_is_post = 1;
	params.b_write_to_file = 1;
	params.b_permit_cache_server_if_defined = 1;
	params.objects_mode = GH__OBJECTS_MODE__POST;

	params.post_payload = &jw_req.json;

	params.result_list = result_list;

	params.headers = http_copy_default_headers();
	params.headers = curl_slist_append(params.headers,
					   "X-TFS-FedAuthRedirect: Suppress");
	params.headers = curl_slist_append(params.headers,
					   "Pragma: no-cache");
	params.headers = curl_slist_append(params.headers,
					   "Content-Type: application/json");
	/*
	 * If our POST contains more than one object, we want the
	 * server to send us a packfile.  We DO NOT want the non-standard
	 * concatenated loose object format, so we DO NOT send:
	 *     "Accept: application/x-git-loose-objects" (plural)
	 *
	 * However, if the payload only requests 1 OID, the server
	 * will send us a single loose object instead of a packfile,
	 * so we ACK that and send:
	 *     "Accept: application/x-git-loose-object" (singular)
	 */
	params.headers = curl_slist_append(params.headers,
					   "Accept: application/x-git-packfile");
	params.headers = curl_slist_append(params.headers,
					   "Accept: application/x-git-loose-object");

	setup_gvfs_objects_progress(&params, j_pack_num, j_pack_den);

	do_req__with_fallback("gvfs/objects", &params, status);

	gh__request_params__release(&params);
	jw_release(&jw_req);
}

struct find_last_data {
	timestamp_t timestamp;
	int nr_files;
};

static void cb_find_last(const char *full_path, size_t full_path_len,
			 const char *file_path, void *void_data)
{
	struct find_last_data *data = void_data;
	const char *val;
	timestamp_t t;

	if (!skip_prefix(file_path, "prefetch-", &val))
		return;
	if (!ends_with(val, ".pack"))
		return;

	data->nr_files++;

	/*
	 * We expect prefetch packfiles named like:
	 *
	 *     prefetch-<seconds>-<checksum>.pack
	 */
	t = strtol(val, NULL, 10);

	data->timestamp = MY_MAX(t, data->timestamp);
}

/*
 * Find the server timestamp on the last prefetch packfile that
 * we have in the ODB.
 *
 * TODO I'm going to assume that all prefetch packs are created
 * TODO equal and take the one with the largest t value.
 * TODO
 * TODO Or should we look for one marked with .keep ?
 *
 * TODO Alternatively, should we maybe get the 2nd largest?
 * TODO (Or maybe subtract an hour delta from the largest?)
 * TODO
 * TODO Since each cache-server maintains its own set of prefetch
 * TODO packs (such that 2 requests may hit 2 different
 * TODO load-balanced servers and get different anwsers (with or
 * TODO without clock-skew issues)), is it possible for us to miss
 * TODO the absolute fringe of new commits and trees?
 * TODO
 * TODO That is, since the cache-server generates hourly prefetch
 * TODO packs, we could do a prefetch and be up-to-date, but then
 * TODO do the main fetch and hit a different cache/main server
 * TODO and be behind by as much as an hour and have to demand-
 * TODO load the commits/trees.
 *
 * TODO Alternatively, should we compare the last timestamp found
 * TODO with "now" and silently do nothing if within an epsilon?
 */
static void find_last_prefetch_timestamp(timestamp_t *last)
{
	struct find_last_data data;

	memset(&data, 0, sizeof(data));

	for_each_file_in_pack_dir(gh__global.buf_odb_path.buf, cb_find_last, &data);

	*last = data.timestamp;
}

/*
 * Call "gvfs/prefetch[?lastPackTimestamp=<secondsSinceEpoch>]" REST API to
 * fetch a series of packfiles and write them to the ODB.
 *
 * Return a list of packfile names.
 */
static void do__http_get__gvfs_prefetch(struct gh__response_status *status,
					timestamp_t seconds_since_epoch,
					struct string_list *result_list)
{
	struct gh__request_params params = GH__REQUEST_PARAMS_INIT;
	struct strbuf component_url = STRBUF_INIT;

	gh__response_status__zero(status);

	strbuf_addstr(&component_url, "gvfs/prefetch");

	if (!seconds_since_epoch)
		find_last_prefetch_timestamp(&seconds_since_epoch);
	if (seconds_since_epoch)
		strbuf_addf(&component_url, "?lastPackTimestamp=%"PRItime,
			    seconds_since_epoch);

	params.b_is_post = 0;
	params.b_write_to_file = 1;
	params.b_permit_cache_server_if_defined = 1;
	params.objects_mode = GH__OBJECTS_MODE__PREFETCH;

	params.object_count = -1;

	params.result_list = result_list;

	params.headers = http_copy_default_headers();
	params.headers = curl_slist_append(params.headers,
					   "X-TFS-FedAuthRedirect: Suppress");
	params.headers = curl_slist_append(params.headers,
					   "Pragma: no-cache");
	params.headers = curl_slist_append(params.headers,
					   "Accept: application/x-gvfs-timestamped-packfiles-indexes");

	if (gh__cmd_opts.show_progress)
		strbuf_addf(&params.progress_base_phase3_msg,
			    "Prefetch %"PRItime" (%s)",
			    seconds_since_epoch,
			    show_date(seconds_since_epoch, 0,
				      DATE_MODE(ISO8601)));

	do_req__with_fallback(component_url.buf, &params, status);

	gh__request_params__release(&params);
	strbuf_release(&component_url);
}

/*
 * Drive one or more HTTP GET requests to fetch the objects
 * in the given OIDSET.  These are received into loose objects.
 *
 * Accumulate results for each request in `result_list` until we get a
 * hard error and have to stop.
 */
static void do__http_get__fetch_oidset(struct gh__response_status *status,
				       struct oidset *oids,
				       unsigned long nr_oid_total,
				       struct string_list *result_list)
{
	struct oidset_iter iter;
	struct strbuf err404 = STRBUF_INIT;
	const struct object_id *oid;
	unsigned long k;
	int had_404 = 0;

	gh__response_status__zero(status);
	if (!nr_oid_total)
		return;

	oidset_iter_init(oids, &iter);

	for (k = 0; k < nr_oid_total; k++) {
		oid = oidset_iter_next(&iter);

		do__http_get__gvfs_object(status, oid, k+1, nr_oid_total,
					  result_list);

		/*
		 * If we get a 404 for an individual object, ignore
		 * it and get the rest.  We'll fixup the 'ec' later.
		 */
		if (status->ec == GH__ERROR_CODE__HTTP_404) {
			if (!err404.len)
				strbuf_addf(&err404, "%s: from GET %s",
					    status->error_message.buf,
					    oid_to_hex(oid));
			/*
			 * Mark the fetch as "incomplete", but don't
			 * stop trying to get other chunks.
			 */
			had_404 = 1;
			continue;
		}

		if (status->ec != GH__ERROR_CODE__OK) {
			/* Stop at the first hard error. */
			strbuf_addf(&status->error_message, ": from GET %s",
				    oid_to_hex(oid));
			goto cleanup;
		}
	}

cleanup:
	if (had_404 && status->ec == GH__ERROR_CODE__OK) {
		strbuf_setlen(&status->error_message, 0);
		strbuf_addbuf(&status->error_message, &err404);
		status->ec = GH__ERROR_CODE__HTTP_404;
	}

	strbuf_release(&err404);
}

/*
 * Drive one or more HTTP POST requests to bulk fetch the objects in
 * the given OIDSET.  Create one or more packfiles and/or loose objects.
 *
 * Accumulate results for each request in `result_list` until we get a
 * hard error and have to stop.
 */
static void do__http_post__fetch_oidset(struct gh__response_status *status,
					struct oidset *oids,
					unsigned long nr_oid_total,
					struct string_list *result_list)
{
	struct oidset_iter iter;
	struct strbuf err404 = STRBUF_INIT;
	unsigned long k;
	unsigned long nr_oid_taken;
	int j_pack_den = 0;
	int j_pack_num = 0;
	int had_404 = 0;

	gh__response_status__zero(status);
	if (!nr_oid_total)
		return;

	oidset_iter_init(oids, &iter);

	j_pack_den = ((nr_oid_total + gh__cmd_opts.block_size - 1)
		      / gh__cmd_opts.block_size);

	for (k = 0; k < nr_oid_total; k += nr_oid_taken) {
		j_pack_num++;

		do__http_post__gvfs_objects(status, &iter,
					    gh__cmd_opts.block_size,
					    j_pack_num, j_pack_den,
					    result_list,
					    &nr_oid_taken);

		/*
		 * Because the oidset iterator has random
		 * order, it does no good to say the k-th or
		 * n-th chunk was incomplete; the client
		 * cannot use that index for anything.
		 *
		 * We get a 404 when at least one object in
		 * the chunk was not found.
		 *
		 * For now, ignore the 404 and go on to the
		 * next chunk and then fixup the 'ec' later.
		 */
		if (status->ec == GH__ERROR_CODE__HTTP_404) {
			if (!err404.len)
				strbuf_addf(&err404,
					    "%s: from POST",
					    status->error_message.buf);
			/*
			 * Mark the fetch as "incomplete", but don't
			 * stop trying to get other chunks.
			 */
			had_404 = 1;
			continue;
		}

		if (status->ec != GH__ERROR_CODE__OK) {
			/* Stop at the first hard error. */
			strbuf_addstr(&status->error_message,
				      ": from POST");
			goto cleanup;
		}
	}

cleanup:
	if (had_404 && status->ec == GH__ERROR_CODE__OK) {
		strbuf_setlen(&status->error_message, 0);
		strbuf_addbuf(&status->error_message, &err404);
		status->ec = GH__ERROR_CODE__HTTP_404;
	}

	strbuf_release(&err404);
}

/*
 * Finish with initialization.  This happens after the main option
 * parsing, dispatch to sub-command, and sub-command option parsing
 * and before actually doing anything.
 *
 * Optionally configure the cache-server if the sub-command will
 * use it.
 */
static void finish_init(int setup_cache_server)
{
	select_odb();

	lookup_main_url();
	gh_http_init();

	if (setup_cache_server)
		select_cache_server();
}

/*
 * Request gvfs/config from main Git server.  (Config data is not
 * available from a GVFS cache-server.)
 *
 * Print the received server configuration (as the raw JSON string).
 */
static enum gh__error_code do_sub_cmd__config(int argc, const char **argv)
{
	struct gh__response_status status = GH__RESPONSE_STATUS_INIT;
	struct strbuf config_data = STRBUF_INIT;
	enum gh__error_code ec = GH__ERROR_CODE__OK;

	trace2_cmd_mode("config");

	finish_init(0);

	do__http_get__gvfs_config(&status, &config_data);
	ec = status.ec;

	if (ec == GH__ERROR_CODE__OK)
		printf("%s\n", config_data.buf);
	else
		error("config: %s", status.error_message.buf);

	gh__response_status__release(&status);
	strbuf_release(&config_data);

	return ec;
}

/*
 * Read a list of objects from stdin and fetch them as a series of
 * single object HTTP GET requests.
 */
static enum gh__error_code do_sub_cmd__get(int argc, const char **argv)
{
	static struct option get_options[] = {
		OPT_INTEGER('r', "max-retries", &gh__cmd_opts.max_retries,
			    N_("retries for transient network errors")),
		OPT_END(),
	};

	struct gh__response_status status = GH__RESPONSE_STATUS_INIT;
	struct oidset oids = OIDSET_INIT;
	struct string_list result_list = STRING_LIST_INIT_DUP;
	enum gh__error_code ec = GH__ERROR_CODE__OK;
	unsigned long nr_oid_total;
	int k;

	trace2_cmd_mode("get");

	if (argc > 1 && !strcmp(argv[1], "-h"))
		usage_with_options(objects_get_usage, get_options);

	argc = parse_options(argc, argv, NULL, get_options, objects_get_usage, 0);
	if (gh__cmd_opts.max_retries < 0)
		gh__cmd_opts.max_retries = 0;

	finish_init(1);

	nr_oid_total = read_stdin_for_oids(&oids);

	do__http_get__fetch_oidset(&status, &oids, nr_oid_total, &result_list);

	ec = status.ec;

	for (k = 0; k < result_list.nr; k++)
		printf("%s\n", result_list.items[k].string);

	if (ec != GH__ERROR_CODE__OK)
		error("get: %s", status.error_message.buf);

	gh__response_status__release(&status);
	oidset_clear(&oids);
	string_list_clear(&result_list, 0);

	return ec;
}

/*
 * Read a list of objects from stdin and fetch them in a single request (or
 * multiple block-size requests) using one or more HTTP POST requests.
 */
static enum gh__error_code do_sub_cmd__post(int argc, const char **argv)
{
	static struct option post_options[] = {
		OPT_MAGNITUDE('b', "block-size", &gh__cmd_opts.block_size,
			      N_("number of objects to request at a time")),
		OPT_INTEGER('d', "depth", &gh__cmd_opts.depth,
			    N_("Commit depth")),
		OPT_INTEGER('r', "max-retries", &gh__cmd_opts.max_retries,
			    N_("retries for transient network errors")),
		OPT_END(),
	};

	struct gh__response_status status = GH__RESPONSE_STATUS_INIT;
	struct oidset oids = OIDSET_INIT;
	struct string_list result_list = STRING_LIST_INIT_DUP;
	enum gh__error_code ec = GH__ERROR_CODE__OK;
	unsigned long nr_oid_total;
	int k;

	trace2_cmd_mode("post");

	if (argc > 1 && !strcmp(argv[1], "-h"))
		usage_with_options(objects_post_usage, post_options);

	argc = parse_options(argc, argv, NULL, post_options, objects_post_usage, 0);
	if (gh__cmd_opts.depth < 1)
		gh__cmd_opts.depth = 1;
	if (gh__cmd_opts.max_retries < 0)
		gh__cmd_opts.max_retries = 0;

	finish_init(1);

	nr_oid_total = read_stdin_for_oids(&oids);

	do__http_post__fetch_oidset(&status, &oids, nr_oid_total, &result_list);

	ec = status.ec;

	for (k = 0; k < result_list.nr; k++)
		printf("%s\n", result_list.items[k].string);

	if (ec != GH__ERROR_CODE__OK)
		error("post: %s", status.error_message.buf);

	gh__response_status__release(&status);
	oidset_clear(&oids);
	string_list_clear(&result_list, 0);

	return ec;
}

/*
 * Interpret the given string as a timestamp and compute an absolute
 * UTC-seconds-since-epoch value (and without TZ).
 *
 * Note that the gvfs/prefetch API only accepts seconds since epoch,
 * so that is all we really need here. But there is a tradition of
 * various Git commands allowing a variety of formats for args like
 * this.  For example, see the `--date` arg in `git commit`.  We allow
 * these other forms mainly for testing purposes.
 */
static int my_parse_since(const char *since, timestamp_t *p_timestamp)
{
	int offset = 0;
	int errors = 0;
	unsigned long t;

	if (!parse_date_basic(since, p_timestamp, &offset))
		return 0;

	t = approxidate_careful(since, &errors);
	if (!errors) {
		*p_timestamp = t;
		return 0;
	}

	return -1;
}

/*
 * Ask the server for all available packfiles -or- all available since
 * the given timestamp.
 */
static enum gh__error_code do_sub_cmd__prefetch(int argc, const char **argv)
{
	static const char *since_str;
	static struct option prefetch_options[] = {
		OPT_STRING(0, "since", &since_str, N_("since"), N_("seconds since epoch")),
		OPT_END(),
	};

	struct gh__response_status status = GH__RESPONSE_STATUS_INIT;
	struct string_list result_list = STRING_LIST_INIT_DUP;
	enum gh__error_code ec = GH__ERROR_CODE__OK;
	timestamp_t seconds_since_epoch = 0;
	int k;

	trace2_cmd_mode("prefetch");

	if (argc > 1 && !strcmp(argv[1], "-h"))
		usage_with_options(prefetch_usage, prefetch_options);

	argc = parse_options(argc, argv, NULL, prefetch_options, prefetch_usage, 0);
	if (since_str && *since_str) {
		if (my_parse_since(since_str, &seconds_since_epoch))
			die("could not parse 'since' field");
	}

	finish_init(1);

	do__http_get__gvfs_prefetch(&status, seconds_since_epoch, &result_list);

	ec = status.ec;

	for (k = 0; k < result_list.nr; k++)
		printf("%s\n", result_list.items[k].string);

	if (ec != GH__ERROR_CODE__OK)
		error("prefetch: %s", status.error_message.buf);

	gh__response_status__release(&status);
	string_list_clear(&result_list, 0);

	return ec;
}

/*
 * Handle the 'objects.get' and 'objects.post' and 'objects.prefetch'
 * verbs in "server mode".
 *
 * Only call error() and set ec for hard errors where we cannot
 * communicate correctly with the foreground client process.  Pass any
 * actual data errors (such as 404's or 401's from the fetch) back to
 * the client process.
 */
static enum gh__error_code do_server_subprocess__objects(const char *verb_line)
{
	struct gh__response_status status = GH__RESPONSE_STATUS_INIT;
	struct oidset oids = OIDSET_INIT;
	struct object_id oid;
	struct string_list result_list = STRING_LIST_INIT_DUP;
	enum gh__error_code ec = GH__ERROR_CODE__OK;
	char *line;
	int len;
	int err;
	int k;
	enum gh__objects_mode objects_mode;
	unsigned long nr_oid_total = 0;
	timestamp_t seconds_since_epoch = 0;

	if (!strcmp(verb_line, "objects.get"))
		objects_mode = GH__OBJECTS_MODE__GET;
	else if (!strcmp(verb_line, "objects.post"))
		objects_mode = GH__OBJECTS_MODE__POST;
	else if (!strcmp(verb_line, "objects.prefetch"))
		objects_mode = GH__OBJECTS_MODE__PREFETCH;
	else {
		error("server: unexpected objects-mode verb '%s'", verb_line);
		ec = GH__ERROR_CODE__SUBPROCESS_SYNTAX;
		goto cleanup;
	}

	switch (objects_mode) {
	case GH__OBJECTS_MODE__GET:
	case GH__OBJECTS_MODE__POST:
		while (1) {
			len = packet_read_line_gently(0, NULL, &line);
			if (len < 0 || !line)
				break;

			if (get_oid_hex(line, &oid)) {
				error("server: invalid oid syntax '%s'", line);
				ec = GH__ERROR_CODE__SUBPROCESS_SYNTAX;
				goto cleanup;
			}

			if (!oidset_insert(&oids, &oid))
				nr_oid_total++;
		}

		if (!nr_oid_total) {
			/* if zero objects requested, trivial OK. */
			if (packet_write_fmt_gently(1, "ok\n")) {
				error("server: cannot write 'get' result to client");
				ec = GH__ERROR_CODE__SUBPROCESS_SYNTAX;
			} else
				ec = GH__ERROR_CODE__OK;
			goto cleanup;
		}

		if (objects_mode == GH__OBJECTS_MODE__GET)
			do__http_get__fetch_oidset(&status, &oids,
						   nr_oid_total, &result_list);
		else
			do__http_post__fetch_oidset(&status, &oids,
						    nr_oid_total, &result_list);
		break;

	case GH__OBJECTS_MODE__PREFETCH:
		/* get optional timestamp line */
		while (1) {
			len = packet_read_line_gently(0, NULL, &line);
			if (len < 0 || !line)
				break;

			seconds_since_epoch = strtoul(line, NULL, 10);
		}

		do__http_get__gvfs_prefetch(&status, seconds_since_epoch,
					    &result_list);
		break;

	default:
		BUG("unexpected object_mode in switch '%d'", objects_mode);
	}

	/*
	 * Write pathname of the ODB where we wrote all of the objects
	 * we fetched.
	 */
	if (packet_write_fmt_gently(1, "odb %s\n",
				    gh__global.buf_odb_path.buf)) {
		error("server: cannot write 'odb' to client");
		ec = GH__ERROR_CODE__SUBPROCESS_SYNTAX;
		goto cleanup;
	}

	for (k = 0; k < result_list.nr; k++)
		if (packet_write_fmt_gently(1, "%s\n",
					    result_list.items[k].string))
		{
			error("server: cannot write result to client: '%s'",
			      result_list.items[k].string);
			ec = GH__ERROR_CODE__SUBPROCESS_SYNTAX;
			goto cleanup;
		}

	/*
	 * We only use status.ec to tell the client whether the request
	 * was complete, incomplete, or had IO errors.  We DO NOT return
	 * this value to our caller.
	 */
	err = 0;
	if (status.ec == GH__ERROR_CODE__OK)
		err = packet_write_fmt_gently(1, "ok\n");
	else if (status.ec == GH__ERROR_CODE__HTTP_404)
		err = packet_write_fmt_gently(1, "partial\n");
	else
		err = packet_write_fmt_gently(1, "error %s\n",
					      status.error_message.buf);
	if (err) {
		error("server: cannot write result to client");
		ec = GH__ERROR_CODE__SUBPROCESS_SYNTAX;
		goto cleanup;
	}

	if (packet_flush_gently(1)) {
		error("server: cannot flush result to client");
		ec = GH__ERROR_CODE__SUBPROCESS_SYNTAX;
		goto cleanup;
	}

cleanup:
	oidset_clear(&oids);
	string_list_clear(&result_list, 0);

	return ec;
}

typedef enum gh__error_code (fn_subprocess_cmd)(const char *verb_line);

struct subprocess_capability {
	const char *name;
	int client_has;
	fn_subprocess_cmd *pfn;
};

static struct subprocess_capability caps[] = {
	{ "objects", 0, do_server_subprocess__objects },
	{ NULL, 0, NULL },
};

/*
 * Handle the subprocess protocol handshake as described in:
 * [] Documentation/technical/protocol-common.txt
 * [] Documentation/technical/long-running-process-protocol.txt
 */
static int do_protocol_handshake(void)
{
#define OUR_SUBPROCESS_VERSION "1"

	char *line;
	int len;
	int k;
	int b_support_our_version = 0;

	len = packet_read_line_gently(0, NULL, &line);
	if (len < 0 || !line || strcmp(line, "gvfs-helper-client")) {
		error("server: subprocess welcome handshake failed: %s", line);
		return -1;
	}

	while (1) {
		const char *v;
		len = packet_read_line_gently(0, NULL, &line);
		if (len < 0 || !line)
			break;
		if (!skip_prefix(line, "version=", &v)) {
			error("server: subprocess version handshake failed: %s",
			      line);
			return -1;
		}
		b_support_our_version |= (!strcmp(v, OUR_SUBPROCESS_VERSION));
	}
	if (!b_support_our_version) {
		error("server: client does not support our version: %s",
		      OUR_SUBPROCESS_VERSION);
		return -1;
	}

	if (packet_write_fmt_gently(1, "gvfs-helper-server\n") ||
	    packet_write_fmt_gently(1, "version=%s\n",
				    OUR_SUBPROCESS_VERSION) ||
	    packet_flush_gently(1)) {
		error("server: cannot write version handshake");
		return -1;
	}

	while (1) {
		const char *v;
		int k;

		len = packet_read_line_gently(0, NULL, &line);
		if (len < 0 || !line)
			break;
		if (!skip_prefix(line, "capability=", &v)) {
			error("server: subprocess capability handshake failed: %s",
			      line);
			return -1;
		}
		for (k = 0; caps[k].name; k++)
			if (!strcmp(v, caps[k].name))
				caps[k].client_has = 1;
	}

	for (k = 0; caps[k].name; k++)
		if (caps[k].client_has)
			if (packet_write_fmt_gently(1, "capability=%s\n",
						    caps[k].name)) {
				error("server: cannot write capabilities handshake: %s",
				      caps[k].name);
				return -1;
			}
	if (packet_flush_gently(1)) {
		error("server: cannot write capabilities handshake");
		return -1;
	}

	return 0;
}

/*
 * Interactively listen to stdin for a series of commands and execute them.
 */
static enum gh__error_code do_sub_cmd__server(int argc, const char **argv)
{
	static struct option server_options[] = {
		OPT_MAGNITUDE('b', "block-size", &gh__cmd_opts.block_size,
			      N_("number of objects to request at a time")),
		OPT_INTEGER('d', "depth", &gh__cmd_opts.depth,
			    N_("Commit depth")),
		OPT_INTEGER('r', "max-retries", &gh__cmd_opts.max_retries,
			    N_("retries for transient network errors")),
		OPT_END(),
	};

	enum gh__error_code ec = GH__ERROR_CODE__OK;
	char *line;
	int len;
	int k;

	trace2_cmd_mode("server");

	if (argc > 1 && !strcmp(argv[1], "-h"))
		usage_with_options(server_usage, server_options);

	argc = parse_options(argc, argv, NULL, server_options, server_usage, 0);
	if (gh__cmd_opts.depth < 1)
		gh__cmd_opts.depth = 1;
	if (gh__cmd_opts.max_retries < 0)
		gh__cmd_opts.max_retries = 0;

	finish_init(1);

	if (do_protocol_handshake()) {
		ec = GH__ERROR_CODE__SUBPROCESS_SYNTAX;
		goto cleanup;
	}

top_of_loop:
	while (1) {
		len = packet_read_line_gently(0, NULL, &line);
		if (len < 0 || !line) {
			/* use extra FLUSH as a QUIT */
			ec = GH__ERROR_CODE__OK;
			goto cleanup;
		}

		for (k = 0; caps[k].name; k++) {
			if (caps[k].client_has &&
			    starts_with(line, caps[k].name)) {
				ec = (caps[k].pfn)(line);
				if (ec != GH__ERROR_CODE__OK)
					goto cleanup;
				goto top_of_loop;
			}
		}

		error("server: unknown command '%s'", line);
		ec = GH__ERROR_CODE__SUBPROCESS_SYNTAX;
		goto cleanup;
	}

cleanup:
	return ec;
}

static enum gh__error_code do_sub_cmd(int argc, const char **argv)
{
	if (!strcmp(argv[0], "get"))
		return do_sub_cmd__get(argc, argv);

	if (!strcmp(argv[0], "post"))
		return do_sub_cmd__post(argc, argv);

	if (!strcmp(argv[0], "config"))
		return do_sub_cmd__config(argc, argv);

	if (!strcmp(argv[0], "prefetch"))
		return do_sub_cmd__prefetch(argc, argv);

	/*
	 * server mode is for talking with git.exe via the "gh_client_" API
	 * using packet-line format.
	 */
	if (!strcmp(argv[0], "server"))
		return do_sub_cmd__server(argc, argv);

	return GH__ERROR_CODE__USAGE;
}

/*
 * Communicate with the primary Git server or a GVFS cache-server using the
 * GVFS Protocol.
 *
 * https://github.com/microsoft/VFSForGit/blob/master/Protocol.md
 */
int cmd_main(int argc, const char **argv)
{
	static struct option main_options[] = {
		OPT_STRING('r', "remote", &gh__cmd_opts.remote_name,
			   N_("remote"),
			   N_("Remote name")),
		OPT_BOOL('f', "fallback", &gh__cmd_opts.try_fallback,
			 N_("Fallback to Git server if cache-server fails")),
		OPT_CALLBACK(0, "cache-server", NULL,
			     N_("cache-server"),
			     N_("cache-server=disable|trust|verify|error"),
			     option_parse_cache_server_mode),
		OPT_CALLBACK(0, "shared-cache", NULL,
			     N_("pathname"),
			     N_("Pathname to shared objects directory"),
			     option_parse_shared_cache_directory),
		OPT_BOOL('p', "progress", &gh__cmd_opts.show_progress,
			 N_("Show progress")),
		OPT_END(),
	};

	enum gh__error_code ec = GH__ERROR_CODE__OK;

	if (argc > 1 && !strcmp(argv[1], "-h"))
		usage_with_options(main_usage, main_options);

	trace2_cmd_name("gvfs-helper");
	packet_trace_identity("gvfs-helper");

	setup_git_directory_gently(NULL);

	/* Set any non-zero initial values in gh__cmd_opts. */
	gh__cmd_opts.depth = GH__DEFAULT__OBJECTS_POST__COMMIT_DEPTH;
	gh__cmd_opts.block_size = GH__DEFAULT__OBJECTS_POST__BLOCK_SIZE;
	gh__cmd_opts.max_retries = GH__DEFAULT_MAX_RETRIES;
	gh__cmd_opts.max_transient_backoff_sec =
		GH__DEFAULT_MAX_TRANSIENT_BACKOFF_SEC;

	gh__cmd_opts.show_progress = !!isatty(2);

	// TODO use existing gvfs config settings to override our GH__DEFAULT_
	// TODO values in gh__cmd_opts.  (And maybe add/remove our command line
	// TODO options for them.)
	// TODO
	// TODO See "scalar.max-retries" (and maybe "gvfs.max-retries")

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, NULL, main_options, main_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc == 0)
		usage_with_options(main_usage, main_options);

	ec = do_sub_cmd(argc, argv);

	gh_http_cleanup();

	if (ec == GH__ERROR_CODE__USAGE)
		usage_with_options(main_usage, main_options);

	return ec;
}
