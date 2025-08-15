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
 * housenote_storage.h - Manage the markdown notes storage and rendering
 */

void housenote_storage_initialize (int argc, const char **argv,
                                   const char *rooturi);
void housenote_storage_background (time_t now);
int housenote_storage_browse (const char *path, char *buffer, int size);
const char *housenote_storage_publish (const char *path,
                                       const char *data, int size);

