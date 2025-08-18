/* HouseNote - a web server to share and browse markdown notes
 *
 * Copyright 2025, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * housenote_storage.c - Manage the markdown notes storage and rendering
 *
 * SYNOPSYS:
 *
 * void housenote_storage_initialize (int argc, const char **argv,
 *                                    const char *rooturi);
 *
 *    Initialize this module.
 *
 * void housenote_storage_background (time_t now);
 *
 *    A periodic function meant to handle any required cleanup.
 *
 * int housenote_storage_browse (const char *path, char *buffer, int size);
 *
 *    Scan the provided path and generate a JSON list of files or directories
 *    found at this level.
 *
 * const char *housenote_storage_publish (const char *path,
 *                                        const char *data, int size);
 *
 *    A function that creates new note or overwrite an existing one.
 *    Return 0 if OK, an error string otherwise.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include <mkdio.h>

#include <echttp.h>
#include <echttp_static.h>

#include "housenote_storage.h"

#define DEBUG if (echttp_isdebug()) printf

static const char *HouseNoteMarkdownRoot = "/var/lib/house/note/content";
static const char *HouseNoteWebRoot = "/var/lib/house/note/cache";

static int HouseNoteMarkdownRootLength = 0;
static int HouseNoteWebRootLength = 0;

static const char *HouseNoteFileUri = 0;

static echttp_not_found_handler *HouseNoteTranscodeChain = 0;

static int housenote_storage_render (const char *filename) {

    int fd;

    if (HouseNoteTranscodeChain) {
        fd = HouseNoteTranscodeChain (filename);
        if (fd >= 0) return fd;
    }

    if (!strstr (filename, ".html")) return -1; // Only transcode to HTML.

    FILE *in = 0;
    FILE *out = 0;
    char fullpath[1024];
    const char *base = filename + HouseNoteWebRootLength;

    snprintf (fullpath, sizeof(fullpath), "%s%s", HouseNoteMarkdownRoot, base);
    char *sep = strrchr (fullpath, '.');
    if (!sep) return -1;
    sep[1] = 'm';
    sep[2] = 'd';
    sep[3] = 0;

    in = fopen (fullpath, "r");
    if (!in) return -1;

    // Create all the directories listed in the target file's path.
    // This is as brute force as it can get. To publish is not high
    // volume enough to justify spending brain power..
    //
    snprintf (fullpath, sizeof(fullpath), "mkdir -p %s", filename);
    sep = strrchr (fullpath, '/');
    if (sep) {
       *sep = 0;
       system (fullpath);
    }

    out = fopen (filename, "w+");
    if (!out) goto failure;

    MMIOT *doc = mkd_in (in, 0);
    if (!doc) goto failure;

    if (markdown (doc, out, MKD_FENCEDCODE | MKD_GITHUBTAGS)) goto failure;

    fclose (in);
    fclose (out);
    return open (filename, O_RDONLY);

failure:
    if (in) fclose (in);
    if (out) fclose (out);
    return -1;
}

static void housenote_storage_title (char *title, int size, const char *path) {

    FILE *file = fopen (path, "r");
    if (!file) goto failure;

    // Detect and extract the markdown title. If not found in the first
    // file lines, fallback to the file name.
    int i;
    for (i = 0; (i < 5) && (!feof(file)); ++i) {
        fgets (title, size, file);
        if (title[0] == '\n') continue;
        if ((title[0] == '#') && (title[1] == ' ')) {
            fclose (file);
            memmove (title, title+2, size-2);
            title[size-2] = 0; // Be safe.
            char *e = strchr (title, '\n');
            if (e) *e = 0;
            return;
        }
    }
    fclose (file);

failure:

    // Use the file basename as a fallback.
    const char *b = strrchr (path, '/');
    if (b) path = b + 1;
    snprintf (title, size, "%s", path);
    char *s = strrchr (title, '.');
    if (s) *s = 0;
}

void housenote_storage_initialize (int argc, const char **argv,
                                   const char *rooturi) {

    HouseNoteFileUri = rooturi;
    HouseNoteMarkdownRootLength = strlen (HouseNoteMarkdownRoot);
    HouseNoteWebRootLength = strlen (HouseNoteWebRoot);
    echttp_static_route (rooturi, HouseNoteWebRoot);
    echttp_static_on_not_found (housenote_storage_render);
}

void housenote_storage_background (time_t now) {
}

int housenote_storage_browse (const char *path, char *buffer, int size) {

   // This generates a "browse" element that is an array of arrays. Each
   // subarray contains a boolean (is browsable--ie. a directory) followed
   // by an URI and then a display name.

   char *ext;
   char fullpath[1024];
   snprintf (fullpath, sizeof(fullpath), "%s%s", HouseNoteMarkdownRoot, path);
   DIR *dir = opendir (fullpath);
   if (!dir) return 0;

   int cursor = snprintf (buffer, size, ",\"browse\":");
   if (cursor >= size) goto overflow;

   const char *sep = "[";
   for (;;) {
       struct dirent *p = readdir (dir);
       if (!p) break;
       if (p->d_name[0] == '.') continue;

       char fullchildpath[1300];
       struct stat fileinfo;
       snprintf (fullchildpath, sizeof(fullchildpath),
                 "%s/%s", fullpath, p->d_name);
       if (stat (fullchildpath, &fileinfo)) continue; // Ignore.


       char display[1300];
       char basename[512];
       snprintf (basename, sizeof(basename)-5, "%s", p->d_name);

       const char *baseuri;
       int filetype = fileinfo.st_mode & S_IFMT;
       if (filetype == S_IFDIR) {
           baseuri = "";
           snprintf (display, sizeof(display), "%s", p->d_name);
       } else if (filetype == S_IFREG) {
           ext = strrchr (basename, '.');
           if (!ext || strcmp (ext, ".md")) continue; // Only markdown.
           strcpy (ext, ".html");
           baseuri = HouseNoteFileUri;
           housenote_storage_title (display, sizeof(display), fullchildpath);
       } else continue; // Ignore this entry.

       if ((path[1] == 0) && (path[0] == '/')) path += 1; // Corner case "/".

       cursor += snprintf (buffer+cursor, size-cursor,
                           "%s[%s,\"%s%s/%s\",\"%s\"]", sep,
                           (filetype == S_IFDIR)?"true":"false",
                           baseuri, path, basename, display);
       if (cursor >= size) goto overflow;
       sep = ",";
   }
   cursor += snprintf (buffer+cursor, size-cursor, "]");

overflow:
   if (cursor >= size) cursor = 0;
   closedir (dir);
   return cursor;
}

const char *housenote_storage_publish (const char *path,
                                       const char *data, int size) {

    char fullpath[1024];

    // Create all the directories listed in the path.
    // This is as brute force as it can get. To publish is not high volume
    // enough to justify spending brain power..
    //
    snprintf (fullpath, sizeof(fullpath),
              "mkdir -p %s%s", HouseNoteMarkdownRoot, path);
    char *sep = strrchr (fullpath, '/');
    if (sep) {
       *sep = 0;
       system (fullpath);
    }

    // Create the markdown file.
    snprintf (fullpath, sizeof(fullpath), "%s%s", HouseNoteMarkdownRoot, path);
    int fd = open (fullpath, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0) return "cannot create";

    if (write (fd, data, size) != size) {
       close (fd);
       return "cannot write the data";
    }
    close (fd);

    // Delete the HTML file, if any. This will trigger a new rendering when
    // the HTML file is first requested.
    //
    snprintf (fullpath, sizeof(fullpath), "%s%s", HouseNoteWebRoot, path);
    unlink (fullpath);
    return 0;
}

