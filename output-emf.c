/*
**  Notes:
**
**  Since EMF files are binary files, their persistent
**  format have to deal with endianess problems
*/
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "spline.h"
#include "xmem.h"

/* external declarations */

extern char *version_string;

/* EMF record-number definitions */

#define ENMT_HEADER                1
#define ENMT_EOF                  14
#define ENMT_POLYLINETO            6
#define ENMT_MOVETO               27
#define ENMT_POLYBEZIERTO          5
#define ENMT_BEGINPATH            59
#define ENMT_ENDPATH              60
#define ENMT_STROKEANDFILLPATH    63
#define ENMT_CREATEPEN            38
#define ENMT_CREATEBRUSHINDIRECT  39
#define ENMT_SELECTOBJECT         37
#define ENMT_SETWORLDTRANSFORM    35

#define WDEVPIXEL 1280
#define HDEVPIXEL 1024
#define WDEVMLMTR 320
#define HDEVMLMTR 240

#define MAKE_COLREF(r,g,b) (((r) & 0x0FF) | (((g) & 0x0FF) << 8) | (((b) & 0x0FF) << 16))
#define MK_PEN(n) ((n) * 2 + 1)
#define MK_BRUSH(n) ((n) * 2 + 2)
#define FLOAT_TO_UI32(num) ((UI32)((num > 0 ? num + 0.005 : num < 0 ? num - 0.005 : 0) * 1000.0))

/* maybe these definitions be put into types.h 
   with some ifdefs ... */

typedef unsigned long int  UI32;
typedef unsigned short int UI16;
typedef unsigned char      UI8;

typedef signed long int  SI32;
typedef signed short int SI16;
typedef signed char      SI8;

typedef struct
{
  UI32 x;
  UI32 y;
} EMFPoint;

typedef struct EMFColorListType
{
  UI32 colref;
  struct EMFColorListType *next;
} EMFColorList;


typedef struct
{
  int ncolors;
  int nrecords;
  int filesize;
} EMFStats;

/* globals */

EMFColorList *color_list = NULL;
UI32 *color_table = NULL;
char *editor;

/* color list functions */

int SearchColor(EMFColorList *head, UI32 colref)
{
  while(head != NULL)
  {
    if(head->colref == colref)
      return 1;
    head = head->next;
  }
  return 0;
}

void AddColor(EMFColorList **head, UI32 colref)
{
  EMFColorList *temp;
  
  XMALLOC(temp, sizeof(EMFColorList));
  
  temp->colref = colref;
  temp->next = *head;
  *head = temp;
}

void ColorListToColorTable(EMFColorList **head, UI32 **table, int len)
{
  EMFColorList *temp;
  int i = 0;
  
  XMALLOC(*table, sizeof(UI32) * len);
  
  while(*head != NULL)
  {
    temp = *head;
    *head = (*head)->next;
    (*table)[i] = temp->colref;
    i++;
    free(temp);
  }
}

int ColorLookUp(UI32 colref, UI32 *table, int len)
{
  int i;
  
  for(i=0; i<len; i++)
  {
    if(colref == table[i])
      return i;
  }
  return 0;
}

/* endianess independent IO functions */

static boolean write32(FILE *fdes, UI32 data)
{
  int count = 0;
  UI8 outch;
  
  outch = (UI8) (data & 0x0FF);
  count += fwrite(&outch, 1, 1, fdes);

  outch = (UI8) ((data >> 8) & 0x0FF);
  count += fwrite(&outch, 1, 1, fdes);
  
  outch = (UI8) ((data >> 16) & 0x0FF);
  count += fwrite(&outch, 1, 1, fdes);
  
  outch = (UI8) ((data >> 24) & 0x0FF);
  count += fwrite(&outch, 1, 1, fdes);
  
  return (count == sizeof(UI32)) ? TRUE : FALSE;
}

static boolean write16(FILE *fdes, UI16 data)
{
  int count = 0;
  UI8 outch;

  outch = (UI8) (data & 0x0FF);
  count += fwrite(&outch, 1, 1, fdes);
  outch = (UI8) ((data >> 8) & 0x0FF);
  count += fwrite(&outch, 1, 1, fdes);

  return (count == sizeof(UI16)) ? TRUE : FALSE;
}

static boolean write8(FILE *fdes, UI8 data)
{
  return (fwrite(&data, 1, 1, fdes) == sizeof(UI8)) ? TRUE : FALSE;
}

/* EMF record-type definitions */

int WriteMoveTo(FILE* fdes, real_coordinate_type *pt)
{
  int recsize = sizeof(UI32) * 4;

  if(fdes != NULL)
  {
    write32(fdes, ENMT_MOVETO);
    write32(fdes, (UI32) recsize);
    write32(fdes, (UI32) FLOAT_TO_UI32(pt->x));
    write32(fdes, (UI32) FLOAT_TO_UI32(pt->y));
  }
  return recsize;
}

int WritePolyLineTo(FILE* fdes, spline_type *spl, int nlines)
{
  int i;
  int recsize = sizeof(UI32) * (7 + nlines * 2);

  if(fdes != NULL)
  {
    write32(fdes, ENMT_POLYLINETO);
    write32(fdes, (UI32) recsize);
    write32(fdes, (UI32) 0x0);
    write32(fdes, (UI32) 0x0);
    write32(fdes, (UI32) 0xFFFFFFFF);
    write32(fdes, (UI32) 0xFFFFFFFF);
    write32(fdes, (UI32) nlines);

    for(i=0; i<nlines; i++)
    {
      write32(fdes, (UI32) FLOAT_TO_UI32(END_POINT(spl[i]).x));
      write32(fdes, (UI32) FLOAT_TO_UI32(END_POINT(spl[i]).y));
    }
  }
  return recsize;
}

int WritePolyBezierTo(FILE *fdes, spline_type *spl, int ncurves)
{
  int i;
  int recsize = sizeof(UI32) * (7 + ncurves * 6);

  if(fdes != NULL)
  {
    write32(fdes, ENMT_POLYBEZIERTO);
    write32(fdes, (UI32) recsize);
    write32(fdes, (UI32) 0x0);
    write32(fdes, (UI32) 0x0);
    write32(fdes, (UI32) 0xFFFFFFFF);
    write32(fdes, (UI32) 0xFFFFFFFF);
    write32(fdes, (UI32) ncurves * 3);

    for(i=0; i<ncurves; i++)
    {
      write32(fdes, (UI32) FLOAT_TO_UI32(CONTROL1(spl[i]).x));
      write32(fdes, (UI32) FLOAT_TO_UI32(CONTROL1(spl[i]).y));
      write32(fdes, (UI32) FLOAT_TO_UI32(CONTROL2(spl[i]).x));
      write32(fdes, (UI32) FLOAT_TO_UI32(CONTROL2(spl[i]).y));
      write32(fdes, (UI32) FLOAT_TO_UI32(END_POINT(spl[i]).x));
      write32(fdes, (UI32) FLOAT_TO_UI32(END_POINT(spl[i]).y));
    }
  }
  return recsize;
}



int WriteBeginPath(FILE *fdes)
{
  int recsize = sizeof(UI32) * 2;

  if(fdes != NULL)
  {
    write32(fdes, ENMT_BEGINPATH);
    write32(fdes, (UI32) recsize);
  }
  return recsize;
}



int WriteEndPath(FILE *fdes)
{
  int recsize = sizeof(UI32) * 2;

  if(fdes != NULL)
  {
    write32(fdes, ENMT_ENDPATH);
    write32(fdes, (UI32) recsize);
  }
  return recsize;
}



int WriteStrokeAndFillPath(FILE *fdes)
{
  int recsize = sizeof(UI32) * 6;

  if(fdes != NULL)
  {
    write32(fdes, ENMT_STROKEANDFILLPATH);
    write32(fdes, (UI32) recsize);
    write32(fdes, (UI32) 0x0);
    write32(fdes, (UI32) 0x0);
    write32(fdes, (UI32) 0xFFFFFFFF);
    write32(fdes, (UI32) 0xFFFFFFFF);
  }
  return recsize;
}

int WriteSetWorldTransform(FILE *fdes, UI32 height)
{
  int recsize = sizeof(UI32) * 8;
  float fHeight;
  
  if(fdes != NULL)
  {
    /* conversion to float */
    fHeight = (float) height;

    /* binary copy for serialization */
    memcpy((void *) &height, (void *) &fHeight, sizeof(UI32));

    write32(fdes, ENMT_SETWORLDTRANSFORM);
    write32(fdes, (UI32) recsize);
    write32(fdes, (UI32) 0x3A83126F); /* hex val for float(0.001) */
    write32(fdes, (UI32) 0x0);
    write32(fdes, (UI32) 0x0);
    write32(fdes, (UI32) 0xBA83126F); /* hex val for float(-0.001) */
    write32(fdes, (UI32) 0x0);
    write32(fdes, height);
  }
  return recsize;
}

int WriteCreateSolidPen(FILE *fdes, int hndNum, UI32 colref)
{
  int recsize = sizeof(UI32) * 7;

  if(fdes != NULL)
  {
    write32(fdes, ENMT_CREATEPEN);
    write32(fdes, (UI32) recsize);
    write32(fdes, (UI32) hndNum);
    write32(fdes, (UI32) 0x0);  /* solid pen style */
    write32(fdes, (UI32) 0x0);  /* 1 pixel ...  */
    write32(fdes, (UI32) 0x0);  /* ... pen size */
    write32(fdes, (UI32) colref);
  }
  return recsize;
}



int WriteCreateSolidBrush(FILE *fdes, int hndNum, UI32 colref)
{
  int recsize = sizeof(UI32) * 6;

  if(fdes != NULL)
  {
    write32(fdes, ENMT_CREATEBRUSHINDIRECT);
    write32(fdes, (UI32) recsize);
    write32(fdes, (UI32) hndNum);
    write32(fdes, (UI32) 0x0);  /* solid brush style */
    write32(fdes, (UI32) colref); 
    write32(fdes, (UI32) 0x0);  /* ignored when solid */
  }
  return recsize;
}



int WriteSelectObject(FILE *fdes, int hndNum)
{
  int recsize = sizeof(UI32) * 3;

  if(fdes != NULL)
  {
    write32(fdes, ENMT_SELECTOBJECT);
    write32(fdes, (UI32) recsize);
    write32(fdes, (UI32) hndNum);
  }
  return recsize;
}

int WriteEndOfMetafile(FILE *fdes)
{
  int recsize = sizeof(UI32) * 5;

  if(fdes != NULL)
  {
    write32(fdes, ENMT_EOF);
    write32(fdes, (UI32) recsize);
    write32(fdes, (UI32) 0);
    write32(fdes, (UI32) recsize - sizeof(UI32));
    write32(fdes, (UI32) recsize);
  }
  return recsize;
}

int WriteHeader(FILE *fdes, string name, int width, int height, int fsize, int nrec, int nhand)
{
  int i, desclen, recsize;

  desclen = (strlen(editor) + strlen(name) + 3);
  recsize = sizeof(UI32) * 25 + (desclen * 2) + ((desclen * 2) % 4);

  if(fdes != NULL)
  {
    write32(fdes, ENMT_HEADER);
    write32(fdes, (UI32) recsize);

    /* pixel bounds */
    write32(fdes, (UI32) 0);
    write32(fdes, (UI32) 0);
    write32(fdes, (UI32) width);
    write32(fdes, (UI32) height);

    /* millimeter bounds */
    write32(fdes, (UI32) 0);
    write32(fdes, (UI32) 0);
    write32(fdes, (UI32) width * WDEVMLMTR * 100 / WDEVPIXEL);
    write32(fdes, (UI32) height * HDEVMLMTR * 100 / HDEVPIXEL);

    /* signature " EMF" */
    write32(fdes, (UI32) 0x464D4520);

    /* current version */
    write32(fdes, (UI32) 0x00010000);

    /* file size */
    write32(fdes, (UI32) fsize);

    /* number of records */
    write32(fdes, (UI32) nrec);

    /* number of handles */
    write16(fdes, (UI16) nhand);

    /* reserved */
    write16(fdes, (UI16) 0);

    /* size of description */
    write32(fdes, (UI32) desclen);

    /* description offset */
    write32(fdes, (UI32) 100);

    /* palette entries */
    write32(fdes, (UI32) 0);

    /* device width & height in pixel & millimeters */
    write32(fdes, (UI32) WDEVPIXEL);
    write32(fdes, (UI32) HDEVPIXEL);
    write32(fdes, (UI32) WDEVMLMTR);
    write32(fdes, (UI32) HDEVMLMTR);

    /* pixel format & opengl (not used) */
    write32(fdes, (UI32) 0);
    write32(fdes, (UI32) 0);
    write32(fdes, (UI32) 0);

    /* description string in Unicode */
    for(i=0;editor[i]; i++)
    {
      write16(fdes, ((UI16) (editor[i] & 0x07F)) );
    }
    write16(fdes, (UI16) 0);
    for(i=0;name[i]; i++)
    {
      write16(fdes, ((UI16) (name[i] & 0x07F)) );
    }
    write32(fdes, (UI32) 0);
    if((desclen * 2) % 4)
      write16(fdes, (UI16) 0);
  }
  return recsize;
}

void GetEmfStats(EMFStats *stats, string name, spline_list_array_type shape)

{
  int i, j;
  int ncolors = 0;
  int ncolorchng = 0;
  int nrecords = 0;
  int filesize = 0;
  UI32 last_color = 0xFFFFFFFF, curr_color;
  spline_list_type curr_list;
  spline_type curr_spline;
  int last_degree;
  int nlines;

  /* visit each spline-list */
  for(i=0; i<SPLINE_LIST_ARRAY_LENGTH(shape); i++)
  {
    curr_list = SPLINE_LIST_ARRAY_ELT(shape, i);
    curr_color = MAKE_COLREF(curr_list.color.r,curr_list.color.g,curr_list.color.b);
    if(curr_color != last_color)
    {
      ncolorchng++;
      if(!SearchColor(color_list, curr_color))
      {
        ncolors++;
        AddColor(&color_list, curr_color);
      }
      last_color = curr_color;
    }

    /* emf stats :: MoveTo */
    nrecords++;
    filesize += WriteMoveTo(NULL,NULL);

    /* emf stats :: BeginPath */
    nrecords++;
    filesize += WriteBeginPath(NULL);

    /* visit each spline */
    last_degree = -1;
    j = 0;

    /* the outer loop iterates through spline
       groups of the same degree */
    while(j<SPLINE_LIST_LENGTH(curr_list))
    {
      nlines = 0;
      curr_spline = SPLINE_LIST_ELT(curr_list, j);
      last_degree = ((int)SPLINE_DEGREE(curr_spline));

      /* the inner loop iterates through lists
         of spline having the same degree */
      while(last_degree == ((int)SPLINE_DEGREE(curr_spline)))
      {
        nlines++;
        j++;
        if(j>=SPLINE_LIST_LENGTH(curr_list))
          break;
        curr_spline = SPLINE_LIST_ELT(curr_list, j);
      }

      switch((polynomial_degree)last_degree)
      {
        case LINEARTYPE:
          /* emf stats :: PolyLineTo */
          nrecords++;
          filesize += WritePolyLineTo(NULL, NULL, nlines);
          break;

        default:
          /* emf stats :: PolyBezierTo */
          nrecords++;
          filesize += WritePolyBezierTo(NULL, NULL, nlines);
          break;
      }
    }

    /* emf stats :: EndPath */
    nrecords++;
    filesize += WriteEndPath(NULL);

    /* emf stats :: StrokeAndFillPath */
    nrecords++;
    filesize += WriteStrokeAndFillPath(NULL);
  }

  /* emf stats :: CreateSolidPen & CreateSolidBrush*/
  nrecords += ncolors * 2;
  filesize += (WriteCreateSolidPen(NULL, 0, 0) + WriteCreateSolidBrush(NULL, 0, 0)) * ncolors;

  /* emf stats :: SelectObject */
  nrecords += ncolorchng * 2;
  filesize += WriteSelectObject(NULL, 0) * ncolorchng * 2;

  /* emf stats :: header + footer */
  nrecords++;
  filesize += WriteSetWorldTransform(NULL, 0) + WriteEndOfMetafile(NULL) + WriteHeader(NULL, name, 0, 0, 0, 0, 0);
  stats->ncolors  = ncolors;
  stats->nrecords = nrecords;
  stats->filesize = filesize;
  ColorListToColorTable(&color_list, &color_table, ncolors);
}

void
OutputEmf(FILE* fdes, EMFStats *stats, string name, int width, int height, spline_list_array_type shape)
{
  int i, j;
  int color_index;
  int ncolorchng = 0;
  int nrecords = 0;
  int filesize = 0;
  UI32 last_color = 0xFFFFFFFF, curr_color;
  spline_list_type curr_list;
  spline_type curr_spline;
  int last_degree;
  int nlines;
  EMFPoint pt;

  /* output EMF header */
  WriteHeader(fdes, name, width, height, stats->filesize, stats->nrecords, (stats->ncolors * 2) +1);

  /* output world tranform */
  WriteSetWorldTransform(fdes, height);

  /* output pens & brushes */
  for(i=0; i<stats->ncolors; i++)
  {
    WriteCreateSolidPen(fdes, MK_PEN(i), color_table[i]);
    WriteCreateSolidBrush(fdes, MK_BRUSH(i), color_table[i]);
  }

  /* visit each spline-list */
  for(i=0; i<SPLINE_LIST_ARRAY_LENGTH(shape); i++)
  {
    curr_list = SPLINE_LIST_ARRAY_ELT(shape, i);

    /* output pen & brush selection */
    curr_color = MAKE_COLREF(curr_list.color.r,curr_list.color.g,curr_list.color.b);
    if(curr_color != last_color)
    {
      color_index = ColorLookUp(curr_color, color_table, stats->ncolors);
      WriteSelectObject(fdes, MK_PEN(color_index));
      WriteSelectObject(fdes, MK_BRUSH(color_index));
      last_color = curr_color;
    }

    /* output a BeginPath for current shape */
    WriteBeginPath(fdes);

    /* output MoveTo first point */
    curr_spline = SPLINE_LIST_ELT(curr_list, 0);
    WriteMoveTo(fdes, &(START_POINT(curr_spline)));

    /* visit each spline */
    j = 0;

    /* the outer loop iterates through spline
       groups of the same degree */
    while(j<SPLINE_LIST_LENGTH(curr_list))
    {
      nlines = 0;
      curr_spline = SPLINE_LIST_ELT(curr_list, j);
      last_degree = ((int)SPLINE_DEGREE(curr_spline));

      /* the inner loop iterates through lists
         of spline having the same degree */
      while(last_degree == ((int)SPLINE_DEGREE(curr_spline)))
      {
        nlines++;
        j++;
        if(j>=SPLINE_LIST_LENGTH(curr_list))
          break;
        curr_spline = SPLINE_LIST_ELT(curr_list, j);
      }
      switch((polynomial_degree)last_degree)
      {
        case LINEARTYPE:
          /* output PolyLineTo */
          WritePolyLineTo(fdes, &(SPLINE_LIST_ELT(curr_list, j - nlines)), nlines);
          break;
        default:
          /* output PolyBezierTo */
          WritePolyBezierTo(fdes, &(SPLINE_LIST_ELT(curr_list, j - nlines)), nlines);
          break;
      }
    }

    /* output EndPath */
    WriteEndPath(fdes);

    /* output StrokeAndFillPath */
    WriteStrokeAndFillPath(fdes);
  }

  /* output EndOfMetafile */
  WriteEndOfMetafile(fdes);
}

int output_emf_writer(FILE* file, string name,
		      int llx, int lly, int urx, int ury,
		      spline_list_array_type shape)
{
  EMFStats stats;

  file = freopen (name, "wb", file);
  editor = version_string;

  GetEmfStats(&stats, name, shape);
  OutputEmf(file, &stats, name, urx, ury, shape);

  return 0;
}


