/*
 *  OpenSlide, a library for reading whole slide image files
 *
 *  Copyright (c) 2007-2013 Carnegie Mellon University
 *  Copyright (c) 2011 Google, Inc.
 *  Copyright (c) 2012 Pathomation
 *  All rights reserved.
 *
 *  OpenSlide is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, version 2.1.
 *
 *  OpenSlide is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with OpenSlide. If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

/*
 * Olympus (vsi) support
 *
 *
 */

#include <config.h>

#include "openslide-private.h"
#include "openslide-decode-gdkpixbuf.h"
#include "openslide-decode-jpeg.h"
#include "openslide-decode-png.h"

#include <glib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <zlib.h>

#include "openslide-hash.h"

static const char VSI_EXT[] = ".vsi";
static const char ETS_EXT[] = ".ets";

static char* biggestETS;
static int nbTiles = 0;

// TODO struc tile
// TODO struc level
// TODO ops with paint_region & destroy

static bool olympus_detect(const char *filename, struct _openslide_tifflike *tl,
                         GError **err) {
g_debug("RH OLYMPUS DETECT");
// based on mrax detect.
//file is a vsi
// 1. is a TIFF
// 2. verify filename (suffix)
// 3. verify existence

  // ensure we have a TIFF
  if (!tl) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "Not a TIFF file");
    return false;
  }
g_debug("RH OLYMPUS is a tiff");

  // verify filename
  if (!g_str_has_suffix(filename, VSI_EXT)) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "File does not have %s extension", VSI_EXT);
    return false;
  }
g_debug("RH OLYMPUS suffix is vsi");

  // verify existence
  if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "File does not exist");
    return false;
  }
g_debug("RH OLYMPUS exists");

  return true;

}



static int read_ets_header(const char *filename){

  FILE * stream = _openslide_fopen(filename, "rb", NULL);

  static const char SIS_MAGIC[4] = "SIS";
  static const char ETS_MAGIC[4] = "ETS";

/* 4 */char magic[4]; // SIS0
/* 4 */uint32_t ntiles;


  fread( magic, 1, sizeof(magic), stream );
  printf("%s\n", magic);
  g_assert_cmpstr(magic, ==, SIS_MAGIC);


  fseeko(stream, 40, SEEK_SET);
//nUsedChunks
  fread( (char*)&ntiles, 1, sizeof(ntiles), stream ); // number of tiles
printf("%d\n", ntiles);

  fseeko(stream, 64, SEEK_SET);
  fread( magic, 1, sizeof(magic), stream );
  printf("%s\n", magic);
  g_assert_cmpstr(magic, ==, ETS_MAGIC);

  fclose( stream );

  return ntiles; 
}

static void parse_ets(const char *filename, const int ntiles){

  FILE * stream = _openslide_fopen(filename, "rb", NULL);


  uint64_t offsettiles;

  fseeko(stream, 32, SEEK_SET);
  fread( (char*)&offsettiles, 1, sizeof(offsettiles), stream );

  printf("offsettiles : %ld\n", offsettiles);

  uint64_t n;
  uint32_t maxLevel;
  uint32_t maxX=0;
  uint32_t maxY=0;
  uint32_t level=0;
  uint32_t tx=0;
  uint32_t ty=0;

  n = offsettiles;
  n += 4;

  for(int i=0;i<ntiles;i++){
    fseeko( stream, n, SEEK_SET );
    fread( (char*)&tx, 1, sizeof(tx), stream );
    fread( (char*)&ty, 1, sizeof(ty), stream );
    if(tx > maxX) maxX = tx;
    if(ty > maxY) maxY = ty;
    fread( (char*)&level, 1, sizeof(level), stream );
    fread( (char*)&level, 1, sizeof(level), stream );
    if(level > 0) break;
    n+=36;
  }
  printf("maxX is %d\n",maxX);
  printf("maxY is %d\n",maxY);
  printf("level is %d\n",level);

  uint64_t offsetTileLevel = n-4;
  // not sizeof tile because https://en.wikipedia.org/wiki/Data_structure_alignment#Typical_alignment_of_C_structs_on_x86
  n = offsettiles + ((ntiles-1) * 36);
  n += 4;
  n += 4*3;
  fseeko( stream, n, SEEK_SET );
  fread( (char*)&maxLevel, 1, sizeof(maxLevel), stream );

  printf("maxLevel : %d\n", maxLevel);


  uint32_t nb_x = maxX+1;
  uint32_t nb_y = maxY+1;

  //level0 already added
    printf("offset of level 0 is %d\n\n",offsettiles);
  for(int currentLevel=1;currentLevel<=maxLevel;currentLevel++){

    n = offsetTileLevel+4;
    nb_x = (nb_x+1)/2;
    nb_y = (nb_y+1)/2;
    fseeko( stream, n, SEEK_SET );
    fread( (char*)&tx, 1, sizeof(tx), stream );
    fread( (char*)&ty, 1, sizeof(ty), stream );
    fread( (char*)&level, 1, sizeof(level), stream );
    fread( (char*)&level, 1, sizeof(level), stream );

  printf("tx is %d\n",tx);
  printf("ty is %d\n",ty);
  printf("level is %d\n",level);

//if we have a hole in our tiles
    // too far i must go back
    if(level > currentLevel){
      n = offsettiles + (((nb_x*nb_y)-1) * 36)+ 4 - ((tx+(ty*nb_y)))*36;
      fseeko( stream, n, SEEK_SET );
      fread( (char*)&tx, 1, sizeof(tx), stream );
      fread( (char*)&ty, 1, sizeof(ty), stream );
      fread( (char*)&level, 1, sizeof(level), stream );
      fread( (char*)&level, 1, sizeof(level), stream );
    }
    printf("offset de level i is %d\n\n",offsetTileLevel);
    offsetTileLevel += (nb_x * nb_y  * 36);
  }
  

  fclose( stream );

  return 0; 
}





static bool olympus_open(openslide_t *osr, const char *filename,
                       struct _openslide_tifflike *tl G_GNUC_UNUSED,
                       struct _openslide_hash *quickhash1, GError **err) {
g_debug("RH OLYMPUS OPEN : INITIALIZATION");

  char *dirname = NULL;

g_debug("RH OLYMPUS OPEN : BEGIN");
// As ETS are not detected, the opened file is a vsi


  // open TIFF
  struct _openslide_tiffcache *tc = _openslide_tiffcache_create(filename);
  TIFF *tiff = _openslide_tiffcache_get(tc, err);
  if (!tiff) {
g_debug("RH OLYMPUS OPEN : FAILED !!!");
    //goto FAIL;
  }

  // following doesn't work. Image Width is not in any of the directories & the TIFFTAG_IMAGEWIDTH
  /*// walk directories from ndpi
  int64_t directories = _openslide_tifflike_get_directory_count(tl);
  g_debug("RH OLYMPUS OPEN : nb directories : %d", directories);
  int64_t min_width = INT64_MAX;
  int64_t unknown1 = 0;
  int64_t unknown2 = 0;
  for (int64_t dir = 0; dir < directories; dir++) {
    // read tags
    int64_t width, height, rows_per_strip, start_in_file, num_bytes;

printf("IMAGEWIDTH : %d\n", TIFFTAG_IMAGEWIDTH);
printf("IMAGELENGTH : %d\n", TIFFTAG_IMAGELENGTH);
    TIFF_GET_UINT_OR_FAIL(tl, dir, TIFFTAG_IMAGEWIDTH, width);
    TIFF_GET_UINT_OR_FAIL(tl, dir, TIFFTAG_IMAGELENGTH, height);
    TIFF_GET_UINT_OR_FAIL(tl, dir, TIFFTAG_ROWSPERSTRIP, rows_per_strip);
    TIFF_GET_UINT_OR_FAIL(tl, dir, TIFFTAG_STRIPOFFSETS, start_in_file);
    TIFF_GET_UINT_OR_FAIL(tl, dir, TIFFTAG_STRIPBYTECOUNTS, num_bytes);
    g_debug("RH OLYMPUS OPEN : width : %d", width);
    g_debug("RH OLYMPUS OPEN : height : %d", height);
    g_debug("RH OLYMPUS OPEN : start_in_file : %d", start_in_file);
    g_debug("RH OLYMPUS OPEN : num_bytes : %d", num_bytes);
    // check results
    if (height != rows_per_strip) {
      g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                  "Unexpected rows per strip %"PRId64" (height %"PRId64")",
                  rows_per_strip, height);
      //goto FAIL;
      g_debug("RH OLYMPUS OPEN : FAILED !!! Strip row not equal to height");
    }
    
  g_debug("\n");
  }*/

  // TODO set hash ?

g_debug("RH OLYMPUS OPEN : current path : %s",filename);

  // get directory from filename
// true filename
char* name = basename(filename);
  dirname = g_strndup(filename, strlen(filename) - strlen(name));


g_debug("RH OLYMPUS OPEN : dirname %s",dirname);
g_debug("RH OLYMPUS OPEN : realfilename %s",name);

//ets directory
dirname = g_strconcat (dirname, "_", g_strndup(name, strlen(name) - strlen(VSI_EXT)), "_/", NULL);

g_debug("RH OLYMPUS OPEN : dirname %s",dirname);


char *tmp;
int num_ets;
char **ets_filenames;


GDir *dir;
GError *error = NULL;
const char *filenameTmp;


dir = g_dir_open(dirname, 0, &error);

int nbEtsFiles = 0;
while ((filenameTmp = g_dir_read_name(dir))) {
  filenameTmp = g_build_filename(dirname, filenameTmp, "/", NULL);

  GDir *subDir;
  subDir = g_dir_open(filenameTmp, 0, &error);
  if( error != NULL ) {
    g_clear_error (&error);
    error = NULL;
  } else {
    while ((filenameTmp = g_dir_read_name(subDir))) {
      // verify filename
      if (g_str_has_suffix(filenameTmp, ETS_EXT)) {
         nbEtsFiles++;
      }
    }
    g_dir_close (subDir);
  }
}

// TODO if nbETs = 0 ?
g_dir_rewind (dir);


num_ets = nbEtsFiles;
printf("%d\n", num_ets);
ets_filenames = g_new0(char *, num_ets);




const char *dirnameTmp;
nbEtsFiles = 0;
while ((dirnameTmp = g_dir_read_name(dir))){
  dirnameTmp = g_build_filename(dirname, dirnameTmp, "/", NULL);

  GDir *subDir;
  subDir = g_dir_open(dirnameTmp, 0, &error);
  if( error != NULL ) {
    g_clear_error (&error);
    error = NULL;
  } else {
    while ((filenameTmp = g_dir_read_name(subDir))) {
      if (g_str_has_suffix(filenameTmp, ETS_EXT)) {
         filenameTmp = g_build_filename(dirnameTmp, filenameTmp, NULL);
         ets_filenames[nbEtsFiles++] = filenameTmp;
      }
    }
    g_dir_close (subDir);
  }
}

g_dir_close (dir);


for (int i = 0; i < num_ets; i++) {
   int currentNbTiles = 0;
   g_debug("RH OLYMPUS OPEN : ETS%d %s",i, ets_filenames[i]);
   currentNbTiles = read_ets_header(ets_filenames[i]);
   if(currentNbTiles > nbTiles) {
      nbTiles = currentNbTiles;
      biggestETS = ets_filenames[i];
   }
}

printf("biggest is :%s, %d\n", biggestETS, nbTiles);


g_debug("RH OLYMPUS OPEN : SET LEVEL ARRAY");
parse_ets(biggestETS, nbTiles);


g_debug("RH OLYMPUS OPEN : READ PROPERTIES");
g_debug("RH OLYMPUS OPEN : END");



return false;

}

const struct _openslide_format _openslide_format_olympus = {
  .name = "olympus",
  .vendor = "olympus",
  .detect = olympus_detect,
  .open = olympus_open,
};
