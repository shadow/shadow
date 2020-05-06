## Pull requests

### Merge strategy

When merging pull requests, we currently allow only the
[Rebase and
merge](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests/about-pull-request-merges#rebase-and-merge-your-pull-request-commits)
strategy. Using this strategy, we maintain a linear history. Each
individual commit of a pull request ends up in the linear history of
the master branch.

### Structuring commits

After merging, intermediate commits from merged pull requests are
indistinguishable from commits that were at the "tip" of the pull
request when it was merged. Typically only the state of those "tip"
commits are reviewed and required to pass our continuous integration
testing. It's helpful though if you make a best-effort to avoid
having low-quality commits in your pull request at the time it's
merged; e.g. commits that don't build or have poor log messages.

If you're asked to make changes in a review, it's usually best to
create new commits rather than rewriting the history of your pull
request. However, it's fine to rewrite history (e.g. squash some
commits) before review begins, or if the reviewer asks you to do so
before merging your PR.

If you're in the middle of the review process and need to make
changes that you think should eventually be squashed into another
commit, consider using the `--squash` flag to `git commit`.

### Creating a pull request

When creating a pull request, we suggest you first create it as a
[draft](https://github.blog/2019-02-14-introducing-draft-pull-requests/).
This will still trigger our continuous-integration checks, and give
you a chance resolve any issues with those (i.e. broken tests) before
requesting review.

Once you are ready for a Shadow developer to start a review, take the
pull request out of draft mode.


