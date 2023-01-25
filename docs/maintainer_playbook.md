# Maintainer playbook

## Tagging Shadow releases

Before creating a new release, be sure to handle all issues in its
[GitHub Project](https://github.com/shadow/shadow/projects?type=classic).
Issues that can wait until the next release
can be moved to the next release's project (which you may need to create).
Remaining issues should be resolved before continuing with the release process.

We use [Semantic Versioning](https://semver.org/), and increment version
numbers with the [bumpversion](https://pypi.org/project/bumpversion/) tool.

The following commands can be used to tag a new version of Shadow, after which
an archive will be available on github's [releases
page](https://github.com/shadow/shadow/releases).

Install bumpversion if needed:

    python3 -m venv bumpenv
    source bumpenv/bin/activate
    pip install -U pip
    pip install bumpversion

Make sure main is up to date:

    git checkout main
    git pull

The bumpversion command is run like this (it is recommended to add
`--dry-run --verbose` until you are confident in the result):

    bumpversion --dry-run --verbose <major|minor|patch|release|build>

Decide which part of the version you are bumping. Our format is
`{major}.{minor}.{patch}-{release}.{build}`. Bumping earlier parts of the
version will cause later parts to get reset to 0 (or 'pre' for the release
part). For example, if you are at `2.0.0`, going to `2.1.0-pre` is easy:

    bumpversion minor --tag --commit

In the above case, we can just tag and commit immediately. But if you are going
from `2.0.0` to `2.1.0`, you'll need to either run twice (first to bump the
minor from 0 to 2, then to bump the release from 'pre' to the invisible
'stable'):

    bumpversion minor
    bumpversion --allow-dirty release --commit --tag

or use the serialize option to specify the intended format of the next version:

    bumpversion minor --serialize '{major}.{minor}.{patch}' --commit --tag

Now check that things worked and get the new version number:

    git log -1 --stat
    git describe --tags
    VERSION=`awk -F "=" '/current_version/ {print $2}' .bumpversion.cfg | tr -d ' '`

Update the Cargo lock file, then ammend the commit and tag to include the update
(closely check and update the `Bump version: from → to` messages as needed):

    (cd src && cargo update --workspace)
    git add src/Cargo.lock
    git commit --amend
    git tag -f -a "v$VERSION"

Check again:

    git log -1 --stat
    git describe --tags

Now if everything looks good, push to GitHub:

    git push origin "v$VERSION"

Our releases will then be tagged off of the main branch.

You probably want to also reset the `CHANGELOG.md` file in a new commit after
tagging/pushing the release.
