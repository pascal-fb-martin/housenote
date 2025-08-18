# HouseNote

The House service to share markdown notes

## Overview

This service stores and shares markdown notes. One can browse the notes, rendered in HTML.

Notes can be submitted through HTTP POST methods, but the preferred method is by installing files (and clearing the cache).

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

This service keeps two hierarchies of documents: the original markdown document in a private directory. An HTML cache in a public directory. Both are in `/var/lib/house/note` since this is user or application modified content.

## Notes Installation.

New or updated notes can be copied to /var/lib/house/note/content. Installing a whole directory tree is allowed.

After each installation, the HouseNote cache should be cleared:

```
sudo rm -rf /var/lib/house/note/cache/*
```

The title of each note is extrated to populate the left menu: it is recommended to keep titles short, 30 characters or less.

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

