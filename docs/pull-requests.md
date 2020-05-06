## Pull requests

### Structuring commits

When merging pull requests, we currently allow only the
[Rebase and
merge](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests/about-pull-request-merges#rebase-and-merge-your-pull-request-commits)
strategy. Using this strategy, we maintain a linear history. Each
individual commit of a pull request ends up in the linear history of
the master branch.

Since "work in progress" commits are indistinguishable from the
pull-request-tip states that get reviews and go through our continous
integration testing, it's particularly helpful to try to avoid
commits that don't build at all, and to give each commit a
descriptive log message.

In some cases it may make sense to locally rebase and squash
some of your commits. Avoid doing this *while* your pull request
is under review, but feel free to do so before requesting review, or
if and when the reviewer requests it. When responding to reviewer
comments, if you'd like your changes to eventually be squashed into
other commits, consider using the `--squash` or `--fixup` flags
to `git commit`.

### Creating a pull request

When creating a pull request, we suggest you first create it as a
[draft](https://github.blog/2019-02-14-introducing-draft-pull-requests/).
This will still trigger our continuous-integration checks, and give
you a chance resolve any issues with those (i.e. broken tests) before
requesting review.

Once you are ready for a Shadow developer to take a look, take the
pull request out of draft mode.


