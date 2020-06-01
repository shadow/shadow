## Maintainer playbook

### Tagging Shadow releases

We use [Semantic Versioning](https://semver.org/), and increment version
numbers with the [bumpversion](https://pypi.org/project/bumpversion/) tool.

The following commands can be used to tag a new version of Shadow, after which an
archive will be available on github's [releases page](https://github.com/shadow/shadow/releases).

```bash
git checkout master

# Bump the patch, minor, or major version number, commit the change, and tag
# that commit.
bumpversion <patch|minor|major> --tag --commit

# Get the new version number.
VERSION=`awk -F "=" '/current_version/ {print $2}' .bumpversion.cfg | tr -d ' '`

# Push to GitHub.
git push origin v$VERSION
```

Our releases will then be tagged off of the master branch.

