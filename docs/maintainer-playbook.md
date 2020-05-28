## Maintainer playbook

### Tagging Shadow releases

The following commands can be used to tag a new version of Shadow, after which an
archive will be available on github's [releases page](https://github.com/shadow/shadow/releases).

```bash
git checkout master
git tag -s v1.10.0
git push origin v1.10.0
```
Our releases will then be tagged off of the master branch. Once tagged, a signed archive of a release can be created like this:

```bash
git archive --prefix=shadow-v1.10.0/ --format=tar v1.10.0 | gzip > shadow-v1.10.0.tar.gz
gpg -a -b shadow-v1.10.0.tar.gz
gpg --verify shadow-v1.10.0.tar.gz.asc
```


