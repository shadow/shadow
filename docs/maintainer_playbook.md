# Maintainer playbook

## Tagging Shadow releases

We use [Semantic Versioning](https://semver.org/), and increment version
numbers with the [bumpversion](https://pypi.org/project/bumpversion/) tool.

The following commands can be used to tag a new version of Shadow, after which
an archive will be available on github's [releases
page](https://github.com/shadow/shadow/releases).

```bash
git checkout main

# Bump the patch, minor, or major version number, commit the change, and tag
# that commit.
bumpversion <patch|minor|major> --tag --commit

# Get the new version number.
VERSION=`awk -F "=" '/current_version/ {print $2}' .bumpversion.cfg | tr -d ' '`

# Update the Cargo lock files, then update the commit and tag
(cd src/main && cargo update --workspace)
(cd src/test && cargo update --workspace)
git add src/main/Cargo.lock src/test/Cargo.lock
git commit --amend --no-edit
git tag -f -a "v$VERSION"

# Push to GitHub.
git push origin "v$VERSION"
```

Our releases will then be tagged off of the main branch.

