#!/bin/sh

test_description='git status rename detection options'

. ./test-lib.sh

test_expect_success 'setup' '
	echo 1 >original &&
	git add . &&
	git commit -m"Adding original file." &&
	mv original renamed &&
	echo 2 >> renamed &&
	git add .
'

cat >.gitignore <<\EOF
.gitignore
expect*
output*
EOF

test_expect_success 'status no-options' '
cat >expect-no-options <<\EOF &&
On branch master
Changes to be committed:
  (use "git reset HEAD <file>..." to unstage)

	renamed:    original -> renamed

EOF
	git status >output-no-options &&
	test_i18ncmp expect-no-options output-no-options
'

test_expect_success 'status --no-renames' '
cat >expect-no-renames <<\EOF &&
On branch master
Changes to be committed:
  (use "git reset HEAD <file>..." to unstage)

	deleted:    original
	new file:   renamed

EOF
	git status --no-renames >output-no-renames &&
	test_i18ncmp expect-no-renames output-no-renames
'

test_expect_success 'status status.renames=false' '
cat >expect-no-renames-config <<\EOF &&
On branch master
Changes to be committed:
  (use "git reset HEAD <file>..." to unstage)

	deleted:    original
	new file:   renamed

EOF
	git -c status.renames=false status >output-no-renames-config &&
	test_i18ncmp expect-no-renames-config output-no-renames-config
'

test_expect_success 'status status.renames=true' '
cat >expect-renames-config <<\EOF &&
On branch master
Changes to be committed:
  (use "git reset HEAD <file>..." to unstage)

	renamed:    original -> renamed

EOF
	git -c status.renames=true status >output-renames-config &&
	test_i18ncmp expect-renames-config output-renames-config
'

test_expect_success 'status config overriden' '
cat >expect-config-overriden <<\EOF &&
On branch master
Changes to be committed:
  (use "git reset HEAD <file>..." to unstage)

	deleted:    original
	new file:   renamed

EOF
	git -c status.renames=true status --no-renames >output-config-overriden &&
	test_i18ncmp expect-config-overriden output-config-overriden
'

test_expect_success 'status score=100%' '
cat >expect-perfect-score <<\EOF &&
On branch master
Changes to be committed:
  (use "git reset HEAD <file>..." to unstage)

	deleted:    original
	new file:   renamed

EOF
	git status -M=100% >output-perfect-score1 &&
	test_i18ncmp expect-perfect-score output-perfect-score1 &&

	git status --find-rename=100% >output-perfect-score2 &&
	test_i18ncmp expect-perfect-score output-perfect-score2
'

test_expect_success 'status score=01%' '
cat >expect-perfect-score <<\EOF &&
On branch master
Changes to be committed:
  (use "git reset HEAD <file>..." to unstage)

	renamed:    original -> renamed

EOF
	git status -M=01% >output-perfect-score1 &&
	test_i18ncmp expect-perfect-score output-perfect-score1 &&

	git status --find-rename=01% >output-perfect-score2 &&
	test_i18ncmp expect-perfect-score output-perfect-score2
'

test_done
