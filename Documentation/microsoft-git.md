Details about the Microsoft/Git Fork
====================================

The [`microsoft/git` fork of Git](https://github.com/microsoft/git)
is used to take early performance patches or custom patches for use with
[VFS for Git](https://github.com/microsoft/vfsforgit).

Integration Testing with C# Layer
---------------------------------

If you have a pull request in `microsoft/git` and it passes all the normal
Git-specific builds and tests, then you can test integration in VFS for Git
using a pull request there.

1. First, build a custom Git installer for your PR merge commit. The GVFS test
   suite will install this on the test machine and use it to support the GVFS
   functional tests. The installer packages will be posted to a private nuget
   feed. The installer build step it in a different repo that is unknown to
   `microsoft/git`. Run the [`git - build installers`](https://dev.azure.com/gvfs/ci/_build?definitionId=11)
   using "Branch/tag" as `refs/pull/<pr-number>/merge`. Leave
   "package.preview.extension" as `-pr`. Note the build number. It will have
   the form `2.YYMMDD.#`.

2. Create a topic branch in VFS for Git to consume your custom built Git installer.
   Modify the [`GVFS/GVFS.Build/GVFS.props`](https://github.com/microsoft/VFSForGit/blob/master/GVFS/GVFS.Build/GVFS.props)
   file to change the `<GitPackageVersion>` to be the build number of the installer
   build, followed by the extension (`-pr`), such as `2.YYMMDD.#-pr`. Include
   "[PR Build]" in your commit message, to make it clear this is a build based
   on unmerged commits.

3. Create a pull request in VFS for Git and the functional tests will run. (Make sure
   the installer build has finished before doing this.) Include "[PR Build]" in your
   pull request title.

4. Repeat the previous three instructions as you update your `microsoft/git`
   pull request. Those steps are all manual, so be sure that you have run the
   VFS for Git functional tests based on an up-to-date build of your Git pull
   request.

5. After merging your pull request in `microsoft/git`, you can generate an
   installer build based on the target branch.

   i. For a `vfs-2.*` branch, set the "package.preview.extension" to be empty.
   ii. For the `features/sparse-checkout-2.*` branch, set the "package.preview.extension" to "-sc".

6. Amend your commit in VFS for Git to point to the build number of your "release" build.
   Remove the "[PR Build]" from your commit message and pull request title. Merge that
   pull request when ready.
