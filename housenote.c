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
 * housenote.c - Main loop of the HouseNote program.
 *
 * SYNOPSYS:
 *
 * This program serves and renders markdown notes on a web UI.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "echttp.h"
#include "echttp_cors.h"
#include "echttp_static.h"

#include "houseportalclient.h"
#include "housediscover.h"

#include "housenote_storage.h"

static char HostName[256];

static int HouseNoteBrowsePrefix = 0;
static int HouseNotePublishPrefix = 0;

static char HttpContentBuffer[65537];

static const char *housenote_browse (const char *method, const char *uri,
                                     const char *data, int length) {

    uri += HouseNoteBrowsePrefix; // Skip the portion that got us here.

    char *buffer = HttpContentBuffer;
    int cursor;
    time_t now = time(0);

    cursor = snprintf (buffer, sizeof(HttpContentBuffer),
                       "{\"host\":\"%s\","
                           "\"timestamp\":%lld,\"note\":",
                       HostName, (long long)now);

    int start = cursor;
    cursor += housenote_storage_browse
                  (uri, buffer+cursor, sizeof(HttpContentBuffer)-cursor);
    buffer[start] = '{';
    snprintf (buffer+cursor, sizeof(HttpContentBuffer)-cursor, "}}");
    echttp_content_type_json ();
    return buffer;
}

static const char *housenote_publish (const char *method, const char *uri,
                                      const char *data, int length) {

    uri += HouseNotePublishPrefix; // Skip the portion that got us here.

    echttp_content_type_text ();

    const char *error = housenote_storage_publish (uri, data, length);
    if (error) {
       echttp_error (500, error);
       return error;
    }
    return "";
}

static void housenote_background (int fd, int mode) {

    time_t now = time(0);

    houseportal_background (now);

    housenote_storage_background(now);
}

static void housenote_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

int main (int argc, const char **argv) {

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    signal(SIGPIPE, SIG_IGN);

    gethostname (HostName, sizeof(HostName));

    echttp_default ("-http-service=dynamic");
    echttp_static_default ("-http-root=/usr/local/share/house/public");

    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        static const char *path[] = {"note:/note"};
        houseportal_initialize (argc, argv);
        houseportal_declare (echttp_port(4), path, 1);
    }
    echttp_static_initialize (argc, argv);

    echttp_cors_allow_method("GET");
    echttp_protect (0, housenote_protect);

    housenote_storage_initialize (argc, argv, "/note/content");

    const char *browseuri = "/note/browse";
    echttp_route_match (browseuri, housenote_browse);
    HouseNoteBrowsePrefix = strlen(browseuri);

    const char *publishuri = "/note/publish";
    echttp_route_match (publishuri, housenote_publish);
    HouseNotePublishPrefix = strlen(publishuri);

    echttp_background (&housenote_background);
    echttp_loop();
}

