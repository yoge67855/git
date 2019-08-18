#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"
#include "pathspec.h"
#include "repository.h"
#include "run-command.h"
#include "strbuf.h"

static char const * const builtin_sparse_checkout_usage[] = {
	N_("git sparse-checkout [init|list]"),
	NULL
};

struct opts_sparse_checkout {
	const char *subcommand;
	int read_stdin;
} opts;

static char *get_sparse_checkout_filename(void)
{
	return git_pathdup("info/sparse-checkout");
}

static void write_excludes_to_file(FILE *fp, struct exclude_list *el)
{
	int i;

	for (i = 0; i < el->nr; i++) {
		struct exclude *x = el->excludes[i];

		if (x->flags & EXC_FLAG_NEGATIVE)
			fprintf(fp, "!");

		fprintf(fp, "%s", x->pattern);

		if (x->flags & EXC_FLAG_MUSTBEDIR)
			fprintf(fp, "/");

		fprintf(fp, "\n");
	}
}

static int sparse_checkout_list(int argc, const char **argv)
{
	struct exclude_list el;
	char *sparse_filename;
	int res;

	memset(&el, 0, sizeof(el));

	sparse_filename = get_sparse_checkout_filename();
	res = add_excludes_from_file_to_list(sparse_filename, "", 0, &el, NULL);
	free(sparse_filename);

	if (res < 0) {
		warning(_("failed to parse sparse-checkout file; it may not exist"));
		return 0;
	}

	write_excludes_to_file(stdout, &el);
	clear_exclude_list(&el);

	return 0;
}

static int sc_read_tree(void)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	int result = 0;
	argv_array_pushl(&argv, "read-tree", "-m", "-u", "HEAD", NULL);

	if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
		error(_("failed to update index with new sparse-checkout paths"));
		result = 1;
	}

	argv_array_clear(&argv);
	return result;
}

static int sc_enable_config(void)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	int result = 0;
	argv_array_pushl(&argv, "config", "--add", "core.sparseCheckout", "true", NULL);

	if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
		error(_("failed to enable core.sparseCheckout"));
		result = 1;
	}

	argv_array_clear(&argv);
	return result;
}

static int delete_directory(const struct object_id *oid, struct strbuf *base,
		const char *pathname, unsigned mode, int stage, void *context)
{
	struct strbuf dirname = STRBUF_INIT;
	struct stat sb;

	strbuf_addstr(&dirname, the_repository->worktree);
	strbuf_addch(&dirname, '/');
	strbuf_addstr(&dirname, pathname);

	if (stat(dirname.buf, &sb) || !(sb.st_mode & S_IFDIR))
		return 0;

	if (remove_dir_recursively(&dirname, 0))
		warning(_("failed to remove directory '%s'"),
			dirname.buf);

	strbuf_release(&dirname);
	return 0;
}

static int sparse_checkout_init(int argc, const char **argv)
{
	struct tree *t;
	struct object_id oid;
	struct exclude_list el;
	static struct pathspec pathspec;
	char *sparse_filename;
	FILE *fp;
	int res;

	if (sc_enable_config())
		return 1;

	memset(&el, 0, sizeof(el));

	sparse_filename = get_sparse_checkout_filename();
	res = add_excludes_from_file_to_list(sparse_filename, "", 0, &el, NULL);

	/* If we already have a sparse-checkout file, use it. */
	if (res >= 0) {
		free(sparse_filename);
		goto reset_dir;
	}

	/* initial mode: all blobs at root */
	fp = fopen(sparse_filename, "w");
	free(sparse_filename);
	fprintf(fp, "/*\n!/*/*\n");
	fclose(fp);

	/* remove all directories in the root, if tracked by Git */
	if (get_oid("HEAD", &oid)) {
		/* assume we are in a fresh repo */
		return 0;
	}

	t = parse_tree_indirect(&oid);

	parse_pathspec(&pathspec, PATHSPEC_ALL_MAGIC &
				  ~(PATHSPEC_FROMTOP | PATHSPEC_LITERAL),
		       PATHSPEC_PREFER_CWD,
		       "", NULL);

	if (read_tree_recursive(the_repository, t, "", 0, 0, &pathspec,
				delete_directory, NULL))
		return 1;

reset_dir:
	return sc_read_tree();
}

int cmd_sparse_checkout(int argc, const char **argv, const char *prefix)
{
	static struct option builtin_sparse_checkout_options[] = {
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_sparse_checkout_usage,
				   builtin_sparse_checkout_options);

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_options,
			     builtin_sparse_checkout_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc > 0) {
		if (!strcmp(argv[0], "list"))
			return sparse_checkout_list(argc, argv);
		if (!strcmp(argv[0], "init"))
			return sparse_checkout_init(argc, argv);
	}

	usage_with_options(builtin_sparse_checkout_usage,
			   builtin_sparse_checkout_options);
}
