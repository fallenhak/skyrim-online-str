# Prospective commit diff validation

Before the real Git index is touched, `local_validate()` calls
`prospective_commit_diff_check()` with the exact status-derived file set.

The implementation creates a temporary `GIT_INDEX_FILE`, populates it from
`HEAD` with `git read-tree HEAD`, stages only the explicit paths into that
temporary index, and runs `git diff --cached --check` against it. This covers
tracked edits, staged-plus-unstaged recovery content, deletions, renames where
Git supports them, and new untracked text files. The temporary index is
removed in a `finally` block. A logical `git ls-files --stage -z` snapshot is
recorded before and after the preview; any real-index change fails validation.

The existing real-index defense remains in `commit_phase()`: it rechecks the
current explicit status set, runs the trusted `git add -- <files>`, runs
`git diff --cached --check` again, and commits only after that second check.
There is no `git add -A` path.

The 92-test suite proves valid tracked and untracked files pass, an untracked
trailing-whitespace file fails before real staging, deletions are represented,
staged-plus-unstaged recovery is safe, the logical real index is unchanged,
and the real cached recheck remains in the commit path.
