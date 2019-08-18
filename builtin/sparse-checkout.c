#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"
#include "pathspec.h"
#include "repository.h"
#include "run-command.h"
#include "strbuf.h"

static char const * const builtin_sparse_checkout_usage[] = {
	N_("git sparse-checkout [list]"),
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
	}

	usage_with_options(builtin_sparse_checkout_usage,
			   builtin_sparse_checkout_options);
}
