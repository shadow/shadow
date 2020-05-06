## Version control

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
