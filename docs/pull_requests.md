# Pull requests (PRs)

## Clean commits

Ideally, every commit in history of the `main` branch should:

* Be a focused, self-contained change.
* Have a commit message that summarizes the change and explains *why* the change
  is being made, if not self-evident.
* Build (`./setup build --test`).
* Pass tests (`./setup test`).

## Drafting a PR

PRs should be split into smaller, more focused, changes when feasible.
However, we also want to avoid polluting the history with commits that don't
build or pass tests, or commits within a single PR that fix a mistake earlier in
the PR. While iterating on the PR, the `--fixup` and
`--squash` flags are useful for committing changes that should ultimately be
merged with one of the earlier commits.

When creating a pull request, we suggest you first create it as a
[draft](https://github.blog/2019-02-14-introducing-draft-pull-requests/).  This
will still trigger our continuous-integration checks, and give you a chance
resolve any issues with those (i.e. broken tests) before requesting review.

Once done iterating, first consider using `git rebase -i --autosquash` to clean
up your commit history, and then force pushing to update your PR.  Finally, take
the pull request out of draft mode to signal that you're ready for review.

## Responding to review feedback

*During* PR review, please do not rebase or force-push, since this makes it
difficult to see what's changed between rounds of review. Consider using
`--fixup` and `--squash` for commits responding to review feedback, so that they
can be appropriately squashed before the final merge. [git autofixup](
https://github.com/torbiak/git-autofixup/) can also be useful for generating
`--fixup` commits.

## Merging

When the PR is ready to be merged, the reviewer might ask you to `git rebase`
and force push to clean up history, or might do it themselves.

For the maintainer doing the merge:

If the PR is relatively small, or if it's not worth the effort of rewriting
history into clean commits, use the "squash and merge" strategy.

If the individual commits appear to be useful to keep around in our history,
instead use the "create a merge commit" strategy. There's no need to review
every individual commit when using this strategy, but if the intermediate
commits are obviously low quality consider using the "squash and merge strategy"
instead. Note that since this strategy creates a merge commit, we can still
later identify and filter out the intermediate commits if desired, e.g. with
`git log --first-parent main`.

We've disabled the "Rebase and merge" option, since it does a fast-forward
merge, which makes the intermediate commits indistingishuable from the validated
and reviewed final state of the PR.

A common task is to rebase a PR on main so that it is up to date, perhaps fix
some conflicts or add some changes to the PR, and then push the updated branch
to test it in the CI before merging. Suppose a user `contributor` submitted a
branch `bugfix` as PR `1234`, and has allowed the maintainers to update the PR.
Then you could fetch the branch to perform work on the PR locally:

    git fetch origin pull/1234/head:pr-1234
    git checkout pr-1234
    git rebase main
    <fix conflicts or commit other changes>
    git push -f git@github.com:contributor/shadow.git pr-1234:bugfix
    git checkout main
    git branch -D pr-1234

If it passes the tests, you can merge the PR in the Github interface as usual.
