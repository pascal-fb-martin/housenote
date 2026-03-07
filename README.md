# HouseNote

The House service to share markdown notes

## Overview

This service stores and shares markdown notes. One can browse the notes, rendered in HTML.

Notes can be submitted through HTTP POST methods, but the preferred method is by installing files (and clearing the cache).

Notes can be submitted using other formats, like PDF or even HTML. In that case the original content of the file is sent to the client, without any rendering.

## Installation

This service depends on the House series environment:

* Install git, icoutils, openssl (libssl-dev), libmagic (libmagic-dev).
* Install [echttp](https://github.com/pascal-fb-martin/echttp)
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal)
* It is recommended to install [housesaga](https://github.com/pascal-fb-martin/housesaga) somewhere on the local network, preferably on a file server (logs may become large, and constant write access might not be good for SD cards).
* Clone this repository.
* make rebuild
* sudo make install

## Data Design

This service keeps two hierarchies of documents: the original markdown documents in a private directory and an HTML cache in a public directory. The first is in `/var/lib/house/note` since this is user or application modified content, and the second is located in `/var/cache/house/note`.

## Publishing Notes.

To publish new or updated notes is to copy them to `/var/lib/house/note`. Installing a whole directory tree is allowed. HouseNote will not show in its UI folders named `extra` or with names that start with a '.': these folders can be used to store secondary files that are referenced in installed markdown files.

Once the notes have been published, the HouseNote cache should be cleared:

```
    sudo rm -rf /var/cache/house/note/*
```

As an alternative, a note can be published through the HouseNote web API:

```
    wget --post-file=<src>.md http://<server>/note/publish/<path>/<dst>.md -O /dev/null
```

The HouseNote service will both create the missing directories in the specified path and clear the cache of any stale file. The `<src>` part stands for the local name of the MD file to upload, while `<dst>` stands for the name that HouseNote should use. These two names can be different.

The title of each note is extrated from the markdown file to populate the left menu: it is recommended to keep titles short, 30 characters or less.

## GitHub manuals

This program implements a special set of conventions for converting markdown files from this author's GitHub repositories, i.e. other House repositories:

- An URL that starts with `https://github.com/pascal-fb-martin/` or `https://raw.githubusercontent.com/pascal-fb-martin/` and references a specific file is converted to a local HouseNote URL. An URL that references the repository itself is not converted. Other URLs are not converted.

- If the GitHub URL points to a file in the repository's top folder, HouseNote will search for that file by name anywhere in its content tree. The names of such files must therefore be unique. The most typical case is the repository's main README.md, which must be installed as "<repository>.md".

- If the GitHub URL points to a file in a subfolder, HouseNote will search for that file by name in a "extra/<repository>" subfolder of its content tree.

- The URL is converted only if HouseNote finds the requested file.

These conventions match how House services are installed.

> [!NOTE]
> The URL for an embedded image is converted as well, using the same conventions as described above.

## Web API

```
GET /note/browse/..
```

This endpoint returns a list of directory or file that are found at the provided path. The result is a JSON list of items, where each item is a list made of a boolean (true if browsable, i.e. a directory) and the URI to the resource.

```
PUT /note/publish/..
```

This endpoint is used to upload one note. This request will overwrite any pre-existing note with the same name. Directories present in the path that do not exist on disk will be created automatically. Any matching file in the cache will be deleted automatically.

## Debian Packaging

The provided Makefile supports building private Debian packages. These are _not_ official packages:

- They do not follow all Debian policies.

- They are not built using Debian standard conventions and tools.

- The packaging is not separate from the upstream sources, and there is
  no source package.

To build a Debian package, use the `debian-package` target:

```
    make debian-package
```

