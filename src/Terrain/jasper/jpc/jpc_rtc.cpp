#include "jasper/jpc_rtc.h"
#include "Terrain/RasterTileCache.hpp"

thread_local RasterTileCache *raster_tile_current = 0;

extern "C" {

  long jas_rtc_SkipMarkerSegment(long file_offset) {
    return raster_tile_current->SkipMarkerSegment(file_offset);
  }

  void jas_rtc_MarkerSegment(long file_offset, unsigned id) {
    return raster_tile_current->MarkerSegment(file_offset, id);
  }

  void jas_rtc_SetTile(unsigned index,
                       int xstart, int ystart,
                       int xend, int yend) {
    raster_tile_current->SetTile(index, xstart, ystart, xend, yend);
  }

  void jas_rtc_PutTileData(unsigned index, unsigned x, unsigned y,
                           const struct jas_matrix *data) {
    raster_tile_current->PutTileData(index, x, y, *data);
  }

  void jas_rtc_SetLatLonBounds(double lon_min, double lon_max,
                               double lat_min, double lat_max) {
    raster_tile_current->SetLatLonBounds(lon_min, lon_max, lat_min, lat_max);
  }

  void jas_rtc_SetSize(unsigned width, unsigned height,
                       unsigned tile_width, unsigned tile_height,
                       unsigned tile_columns, unsigned tile_rows) {
    raster_tile_current->SetSize(width, height,
                                 tile_width, tile_height,
                                 tile_columns, tile_rows);
  }
};
