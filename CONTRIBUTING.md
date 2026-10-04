# How to contribute

This document describes some points about the contribution process for OpenToonz.

## Contributing to OpenAnimationLibrary/OT-Dev

OT-Dev accepts contributions based on official OpenToonz as well as work based
on OT-Dev. Choose the pull request base in GitHub explicitly:

| Your change is based on | Base repository | Base branch |
| --- | --- | --- |
| Official `opentoonz/opentoonz:master` | `OpenAnimationLibrary/opentoonz-dev` | `upstream/master` |
| OT-Dev features or code in `main` | `OpenAnimationLibrary/opentoonz-dev` | `main` |

### Changes based on official OpenToonz

Keep your feature branch based on official `opentoonz/opentoonz:master`.
Open your OT-Dev pull request with **base: `upstream/master`** and
**compare: your feature branch**. You may use your existing fork of official
OpenToonz; you do not need to merge OT-Dev `main` into your branch.

`upstream/master` is a clean mirror of official OpenToonz `master`, not a
branch for accumulating OT-Dev features. Maintainers update it by fast-forward
from official upstream. If it is behind the upstream version your change uses,
ask a maintainer to refresh it before reviewing the comparison.

PRs targeting this branch are reviewed as upstream-based proposals. Maintainers
must not merge feature PRs into the mirror. To include an accepted change in
OT-Dev, a maintainer creates a separate integration branch from `main`,
cherry-picks the reviewed commits, resolves OT-Dev-specific conflicts, and opens
an integration PR targeting `main`. Preserve original authorship and link both
PRs. Close the upstream-based proposal with a link to the integration PR when
the change has been incorporated.

This keeps OT-Dev integration conflicts with OT-Dev maintainers. Contributors
may still need to resolve conflicts with newer official upstream changes.
Build and test both the upstream-based proposal and its OT-Dev integration;
checks that run for one base may differ from checks that run for the other.

A PR here does not submit the change to official OpenToonz. For official
acceptance, open a separate PR against `opentoonz/opentoonz:master`.

### Changes based on OT-Dev

For changes that depend on OT-Dev features, branch from OT-Dev `main` and
target `main` directly.

The remaining instructions describe the official OpenToonz contribution process.

## Pull-requests

The OpenToonz organization loves any kind of contributions, such as fixing typos and code refactoring.
If you fixed or added something useful to OpenToonz, please send pull-requests to us.
We will first review the request, then we will accept it, add comments for rework, or decline it.

### Workflow

0. `fork` OpenToonz to your GitHub account from `opentoonz/opentoonz`.
  - (use the `fork` button at the https://github.com/opentoonz/opentoonz)
0. `clone` the repository.
  - `git clone git@github.com:your-github-account/opentoonz.git`
  - `git remote add upstream https://github.com/opentoonz/opentoonz.git`, additionally.
0. modify the codes.
  - `git checkout -b your-branch-name`
    - `your-branch-name` is a name of your modifications, for example,
      `fix/fatal-bugs`, `feature/new-useful-gui` and so on.
  - fix codes, then test them.
  - `git commit` them with good commit messages.
0. `pull` the latest changes form the `master` branch of the upstream.
  - `git pull upstream master` or `git pull --rebase upstream master`.
  - apply [clang-format](http://clang.llvm.org/docs/ClangFormat.html) with `toonz/sources/.clang-format`.
    - `cd toonz/sources`
    - `./beautification.sh` or `beautification.bat`.
  - `git commit` them.
  - `git push origin your-branch-name`.
0. make a pull request.

## Bugs

If you find bugs, please report details about them using [issues](https://github.com/opentoonz/opentoonz/issues).
Please include information needed to reproduce the bug, including the operating system 
and information directly relating to the issue. Links to screen captures of what is 
observed on screen or video of specific steps to produce the problem are very helpful.  
Then we will try to reproduce the bugs and fix them.
Unfortunately, bugs can sometimes only be reproduced in your own environment, 
so we cannot reproduce them. 
If you believe you can fix the bug, please submit a pull request.

## Features

If you had an idea about a new feature, please implement it and send a pull request to us.
If you cannot implement the feature, please open a topic on the [Google Group Page](https://groups.google.com/forum/#!forum/opentoonz_en).
It enables us to discuss implementations of the feature there.
Feature requests posted on GitHub without an active developer or funding will be closed 
to keep the issue tracker from becoming cluttered with unfulfilled feature requests.

## Translations

Translation source (`.ts`) files for OpenToonz GUI are located in `toonz/sources/translations`.
If you create new `.ts` files for your language or update an existing one,
please send us those modifications as pull-requests.
[Qt Linguist](http://doc.qt.io/qt-5.6/linguist-translators.html) is useful for translating them.

Please send us Qt message (`.qm`) files with `.ts` files if you can make the following modifications.

OpenToonz uses `.qm` files generated from `.ts` files.
You can generate `.qm` files by using [Qt Linguist](http://doc.qt.io/qt-5.6/linguist-translators.html).
Please locate generated `.qm` files in `stuff/config/loc`.
It enables the OpenToonz installer to install them into the `stuff` directory.
