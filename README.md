# Native libs common stuff

Currently it contains Conan recipes for AdGuard libs

#### How to use conan

Conan is a C++ package manager. It is similar to maven, but stores recipes and binaries separately.
Binaries can be uploaded to a repo and reused.

It is recommended to use the custom remote repo as the main repo (`-i 0`).
Conan looks up binaries in the repo from which recipes were downloaded.
So if a recipe is from conan-center, you won't be able to store binaries because it has
the highest priority by default.

```
conan remote add -i 0 $REMOTE_NAME https://$ARTIFACTORY_HOST/artifactory/api/conan/$REPO_NAME
```

We customized some packages, so they need to be exported to local conan repository.

```
cd conan/recipes
./export.sh
```

If you want to upload exported recipes to conan remote repository, use following command:

```
conan upload -r $REMOTE_NAME -c '*'
```

After successful build, you may want to upload built binaries to remote repo:
```
conan upload -t $REMOTE_NAME -c '*' --all
```
