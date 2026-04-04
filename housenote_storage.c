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
#include <echttp_libc.h>

#include "housenote_storage.h"

#define DEBUG if (echttp_isdebug()) printf

static const char *HouseNoteContentRoot = "/var/lib/house/note";
static const char *HouseNoteWebRoot = "/var/cache/house/note";

static int HouseNoteContentRootLength = 0;
static int HouseNoteWebRootLength = 0;

static const char *HouseNoteFileUri = 0;

static echttp_not_found_handler *HouseNoteTranscodeChain = 0;

static int housenote_storage_find (char *path, int size, int offset, const char *name) {

    DIR *dir = opendir (path);
    if (!dir) return 0;

    char *base = path + offset;
    *(base++) = '/';
    int sizeleft = size - offset - 1;

    int found = 0; // File not found as the default.
    struct dirent *p;
    for (p = readdir(dir); p; p = readdir(dir)) {
        snprintf (base, sizeleft, "%s", p->d_name);
        if (!strcmp (p->d_name, name)) {
            found = 1; // Found the file at this level.
            break;
        }
        if (p->d_name[0] == '.') {
           if (p->d_name[1] == 0) continue;
           if (p->d_name[1] == '.') continue;
        }
        if (p->d_type == DT_DIR) {
            int delta = strlen (base) + 1;
            if (housenote_storage_find (path, size, offset + delta, name)) {
                found = 1; // The file was found at a deeper level.
                break;
            }
        }
    }
    closedir (dir);
    return found;
}

// Translate URL references in the markdown text.
// If the URL points to a House GitHub project, and the file exists in
// the HouseNote's content tree, then the URL is replaced with an HouseNote
// URL to the corresponding HTML file.
// Otherwise the URL is kept as-is.
//
// TBD: make the list of translations configurable.
//
struct TranslationPattern {
   const char *text;
   int length;
};
struct TranslationRule {
   const char *prefix;
   int length;
   const char *flags;
   struct TranslationPattern mid[4];
};
static struct TranslationRule Translations[] = {
   {"https://github.com/pascal-fb-martin/", 0, "target=\"_blank\"", {{"/blob/master/", 0}, {"/blob/main/", 0}, {0,0}}},
   {"https://raw.githubusercontent.com/pascal-fb-martin/", 0, 0, {{"/master/", 0}, {"/main/", 0}, {0,0}}},
   {0, 0}
};

static char *housenote_storage_render_url
                 (const char *url, const int size, void *data) {

    // Does this URL matches any of our translation rules?
    int i;
    for (i = 0; Translations[i].prefix; ++i) {
        if (!Translations[i].length)
            Translations[i].length = strlen(Translations[i].prefix);
        if (!strncmp (Translations[i].prefix, url, Translations[i].length)) break;
    }
    if (!Translations[i].prefix) return 0; // Not matching any rule.

    char name[512];
    int houseprojectslength = Translations[i].length;
    snprintf (name, sizeof(name), "%s", url + houseprojectslength);
    name[size - houseprojectslength] = 0;
    const char *filename = strrchr (name, '/');
    if (!filename) return 0; // Keep links to a GitHub project itself as-is.
    filename += 1; // Skip the '/'

    char path[1024];
    snprintf (path, sizeof(path), "%s", HouseNoteContentRoot);
    int rootlength = HouseNoteContentRootLength;

    int j;
    for (j = 0; Translations[i].mid[j].text; ++j) {
        char *blob = strstr (name, Translations[i].mid[j].text);
        if (blob) {
            // This link is to a GitHub file. If the file is in the repository's
            // top folder, just use its name. Otherwise build a local path that
            // follows the convention below:
            //
            //    <HouseNote content root>/.<repository>/<relative path>
            //
            if (!Translations[i].mid[j].length)
                Translations[i].mid[j].length = strlen(Translations[i].mid[j].text);
            *blob = 0; // Separate the part of the URL before the mid text.
            char *relativepath = blob + Translations[i].mid[j].length;
            char *sep = strrchr (relativepath, '/');
            if (sep) {
                *sep = 0;
                snprintf (path, sizeof(path), "%s/extra/%s/%s",
                          HouseNoteContentRoot, name, relativepath);
                rootlength = strlen (path);
            }
            break;
        }
    }

    if (!housenote_storage_find (path, sizeof(path), rootlength, filename))
        return 0; // no URL change when the file was not found.

    char newurl[1024];
    snprintf (newurl, sizeof(newurl),
              "/note/content%s", path + HouseNoteContentRootLength);
    char *ext = strstr (newurl, ".md");
    if (ext) snprintf (ext, sizeof(newurl)-(ext-newurl), "%s", ".html");

    return strdup (newurl); // Success.
}

static char *housenote_storage_render_flags (const char *url, const int size, void *data) {
    int i;
    for (i = 0; Translations[i].prefix; ++i) {
        if (!Translations[i].length)
            Translations[i].length = strlen(Translations[i].prefix);
        if (!strncmp (Translations[i].prefix, url, Translations[i].length)) {
            if (Translations[i].flags) return strdup(Translations[i].flags);
            break;
        }
    }
    return 0;
}

static void housenote_storage_render_free (char *url, void *data) {
    free (url);
}

static int housenote_storage_render (const char *filename) {

    int fd;

    if (HouseNoteTranscodeChain) {
        fd = HouseNoteTranscodeChain (filename);
        if (fd >= 0) return fd;
    }

    if (strncmp (filename, HouseNoteWebRoot, HouseNoteWebRootLength)) {
        // Reject any URL that does not point to the cache.
        return -1;
    }

    char fullpath[1024];
    const char *base = filename + HouseNoteWebRootLength;

    if (!strstr (base, ".html")) {
        // Only render to HTML, but support other formats as-is.
        // In that case, we just pretend that the file was found
        // by opening it at its "installed" location. If the file does not
        // exists, open() will fail and a 404 status will be returned.
        //
        snprintf (fullpath, sizeof(fullpath), "%s%s", HouseNoteContentRoot, base);
        return open (fullpath, O_RDONLY);
    }

    FILE *in = 0;
    FILE *out = 0;

    // Build the source name. (The code reserves 3 bytes for the .md suffix.)
    snprintf (fullpath, sizeof(fullpath)-3, "%s%s", HouseNoteContentRoot, base);

    // If the HTML file was installed, use this one, no rendering.
    int html = open (fullpath, O_RDONLY);
    if (html >= 0) return html;

    // The HTML file was not installed: render from a markdown file.
    char *sep = strrchr (fullpath, '.');
    if (!sep) return -1;
    sep[1] = 'm'; sep[2] = 'd'; sep[3] = 0;

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

    // Install callbacks to translate "internal" URLs.
    mkd_e_url (doc, housenote_storage_render_url);
    mkd_e_flags (doc, housenote_storage_render_flags);
    mkd_e_free (doc, housenote_storage_render_free);

    // Write an HTML header so that we can force our own CSS style.
    static const char *htmlhead =
        "<html><head>"
           "<link rel=\"stylesheet\" href=\"/note/docstyle.css\">"
           "<title></title>"
        "</head><body>\n";
    static int htmlheadlength = 0;
    if (!htmlheadlength) htmlheadlength = strlen(htmlhead);
    fwrite (htmlhead, htmlheadlength, 1, out);

    if (markdown (doc, out, MKD_FENCEDCODE | MKD_GITHUBTAGS)) goto failure;

    // Write an HTML trailer that matches the header above.
    static const char *htmltrail = "\n</body></html>";
    static int htmltraillength = 0;
    if (!htmltraillength) htmltraillength = strlen(htmltrail);
    fwrite (htmltrail, htmltraillength, 1, out);

    fclose (in);
    fclose (out);
    return open (filename, O_RDONLY);

failure:
    if (in) fclose (in);
    if (out) fclose (out);
    return -1;
}

static void housenote_storage_title (char *title, int size, const char *path) {

    if (!strstr (path, ".md")) goto notitle; // Get titles from markdowns only.

    FILE *file = fopen (path, "r");
    if (!file) goto notitle;

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

notitle:

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
    HouseNoteContentRootLength = strlen (HouseNoteContentRoot);
    HouseNoteWebRootLength = strlen (HouseNoteWebRoot);
    echttp_static_route (rooturi, HouseNoteWebRoot);
    HouseNoteTranscodeChain =
       echttp_static_on_not_found (housenote_storage_render);
}

void housenote_storage_background (time_t now) {
}

int housenote_storage_browse (const char *path, char *buffer, int size) {

   // This generates a "browse" element that is an array of arrays. Each
   // subarray contains a boolean (is browsable--ie. a directory) followed
   // by an URI and then a display name.

   char fullpath[1024];
   snprintf (fullpath, sizeof(fullpath), "%s%s", HouseNoteContentRoot, path);
   DIR *dir = opendir (fullpath);
   if (!dir) return 0;

   int cursor = snprintf (buffer, size, ",\"browse\":");
   if (cursor >= size) goto overflow;

   char basename[512];
   char *end = basename+sizeof(basename);
   const char *sep = "[";
   for (;;) {
       struct dirent *p = readdir (dir);
       if (!p) break;
       if (p->d_name[0] == '.') continue;
       if (strstr (p->d_name, ".png")) continue; // Skip images
       if (strstr (p->d_name, ".jpg")) continue; // Skip images
       if (!strcmp (p->d_name, "extra")) continue; // Skip "extra" files.

       char fullchildpath[1300];
       struct stat fileinfo;
       snprintf (fullchildpath, sizeof(fullchildpath),
                 "%s/%s", fullpath, p->d_name);
       if (stat (fullchildpath, &fileinfo)) continue; // Ignore.


       char display[1300];
       snprintf (basename, sizeof(basename)-5, "%s", p->d_name);

       const char *baseuri;
       int filetype = fileinfo.st_mode & S_IFMT;
       if (filetype == S_IFDIR) {
           baseuri = "";
           memccpy (display, p->d_name, 0, sizeof(display));
       } else if (filetype == S_IFREG) {
           char *ext = strrchr (basename, '.');
           if (!ext) continue; // Cannot decide what this is..
           if (!strcmp (ext, ".md"))
               stpecpy (ext, end, ".html"); // Transcoded.
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
              "mkdir -p %s%s", HouseNoteContentRoot, path);
    char *sep = strrchr (fullpath, '/');
    if (sep) {
       *sep = 0;
       system (fullpath);
    }

    // Create the markdown file.
    snprintf (fullpath, sizeof(fullpath), "%s%s", HouseNoteContentRoot, path);
    int fd = open (fullpath, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0) return "cannot create";

    int written = write (fd, data, size);
    if (written != size) {
       close (fd);
       return "cannot write the data";
    }
    close (fd);

    // Delete the HTML file, if any. This will trigger a new rendering when
    // the HTML file is first requested.
    // (The code reserves 5 bytes for the ".html" suffix.)
    //
    snprintf (fullpath, sizeof(fullpath)-5, "%s%s", HouseNoteWebRoot, path);
    sep = strrchr (fullpath, '.');
    if (!sep) return "no suffix";
    sep[1] = 'h'; sep[2] = 't'; sep[3] = 'm'; sep[4] = 'l'; sep[5] = 0;

    unlink (fullpath);
    return 0;
}

