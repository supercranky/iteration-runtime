/* Triangle-list equivalents of GL fans/strips, preserving winding and the last
 * (provoking) vertex. Reserve 0xffff: WebGL2 uses it for fixed-index restart. */
#ifndef NANOVG_INDICES_H
#define NANOVG_INDICES_H
#define NVG_INDEX_VERTEX_LIMIT 65535
#define NVG_INDEX_CAPACITY_LIMIT (NVG_INDEX_VERTEX_LIMIT * 3)
static int nvg__appendPathIndices(unsigned short* out, int offset, int capacity,
                                  int first, int count, int strip)
{
  int i, required;
  if (offset < 0 || capacity < offset || first < 0 || first > NVG_INDEX_VERTEX_LIMIT ||
      count < 0 || count > NVG_INDEX_VERTEX_LIMIT-first) return -1;
  if (count < 3) return offset;
  required=(count-2)*3;
  if (!out || required > capacity-offset) return -1;
  for (i=2;i<count;i++) {
    out[offset++]=(unsigned short)(strip ? first+i-2+(i&1) : first);
    out[offset++]=(unsigned short)(strip ? first+i-1-(i&1) : first+i-1);
    out[offset++]=(unsigned short)(first+i);
  }
  return offset;
}
#endif
