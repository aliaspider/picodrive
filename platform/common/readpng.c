#include <stdio.h>
#include <string.h>
#include <png.h>
#include "readpng.h"
#include "lprintf.h"

#ifdef PSP
#define BG_WIDTH  480
#define BG_HEIGHT 272
#else
#define BG_WIDTH  320
#define BG_HEIGHT 240
#endif

struct png_info_def
{
   /* the following are necessary for every PNG file */
   png_uint_32 width;  /* width of image in pixels (from IHDR) */
   png_uint_32 height; /* height of image in pixels (from IHDR) */
   png_uint_32 valid;  /* valid chunk data (see PNG_INFO_ below) */
   png_size_t rowbytes; /* bytes needed to hold an untransformed row */
   png_colorp palette;      /* array of color values (valid & PNG_INFO_PLTE) */
   png_uint_16 num_palette; /* number of color entries in "palette" (PLTE) */
   png_uint_16 num_trans;   /* number of transparent palette color (tRNS) */
   png_byte bit_depth;      /* 1, 2, 4, 8, or 16 bits/channel (from IHDR) */
   png_byte color_type;     /* see PNG_COLOR_TYPE_ below (from IHDR) */
   /* The following three should have been named *_method not *_type */
   png_byte compression_type; /* must be PNG_COMPRESSION_TYPE_BASE (IHDR) */
   png_byte filter_type;    /* must be PNG_FILTER_TYPE_BASE (from IHDR) */
   png_byte interlace_type; /* One of PNG_INTERLACE_NONE, PNG_INTERLACE_ADAM7 */

   /* The following is informational only on read, and not used on writes. */
   png_byte channels;       /* number of data channels per pixel (1, 2, 3, 4) */
   png_byte pixel_depth;    /* number of bits per pixel */
   png_byte spare_byte;     /* to align the data, and for future use */
   png_byte signature[8];   /* magic bytes read by libpng from start of file */

   /* The rest of the data is optional.  If you are reading, check the
    * valid field to see if the information in these are valid.  If you
    * are writing, set the valid field to those chunks you want written,
    * and initialize the appropriate fields below.
    */

#if defined(PNG_gAMA_SUPPORTED)
   /* The gAMA chunk describes the gamma characteristics of the system
    * on which the image was created, normally in the range [1.0, 2.5].
    * Data is valid if (valid & PNG_INFO_gAMA) is non-zero.
    */
   png_fixed_point gamma;
#endif

#ifdef PNG_sRGB_SUPPORTED
    /* GR-P, 0.96a */
    /* Data valid if (valid & PNG_INFO_sRGB) non-zero. */
   png_byte srgb_intent; /* sRGB rendering intent [0, 1, 2, or 3] */
#endif

#ifdef PNG_TEXT_SUPPORTED
   /* The tEXt, and zTXt chunks contain human-readable textual data in
    * uncompressed, compressed, and optionally compressed forms, respectively.
    * The data in "text" is an array of pointers to uncompressed,
    * null-terminated C strings. Each chunk has a keyword that describes the
    * textual data contained in that chunk.  Keywords are not required to be
    * unique, and the text string may be empty.  Any number of text chunks may
    * be in an image.
    */
   int num_text; /* number of comments read or comments to write */
   int max_text; /* current size of text array */
   png_textp text; /* array of comments read or comments to write */
#endif /* PNG_TEXT_SUPPORTED */

#ifdef PNG_tIME_SUPPORTED
   /* The tIME chunk holds the last time the displayed image data was
    * modified.  See the png_time struct for the contents of this struct.
    */
   png_time mod_time;
#endif

#ifdef PNG_sBIT_SUPPORTED
   /* The sBIT chunk specifies the number of significant high-order bits
    * in the pixel data.  Values are in the range [1, bit_depth], and are
    * only specified for the channels in the pixel data.  The contents of
    * the low-order bits is not specified.  Data is valid if
    * (valid & PNG_INFO_sBIT) is non-zero.
    */
   png_color_8 sig_bit; /* significant bits in color channels */
#endif

#if defined(PNG_tRNS_SUPPORTED) || defined(PNG_READ_EXPAND_SUPPORTED) || \
defined(PNG_READ_BACKGROUND_SUPPORTED)
   /* The tRNS chunk supplies transparency data for paletted images and
    * other image types that don't need a full alpha channel.  There are
    * "num_trans" transparency values for a paletted image, stored in the
    * same order as the palette colors, starting from index 0.  Values
    * for the data are in the range [0, 255], ranging from fully transparent
    * to fully opaque, respectively.  For non-paletted images, there is a
    * single color specified that should be treated as fully transparent.
    * Data is valid if (valid & PNG_INFO_tRNS) is non-zero.
    */
   png_bytep trans_alpha;    /* alpha values for paletted image */
   png_color_16 trans_color; /* transparent color for non-palette image */
#endif

#if defined(PNG_bKGD_SUPPORTED) || defined(PNG_READ_BACKGROUND_SUPPORTED)
   /* The bKGD chunk gives the suggested image background color if the
    * display program does not have its own background color and the image
    * is needs to composited onto a background before display.  The colors
    * in "background" are normally in the same color space/depth as the
    * pixel data.  Data is valid if (valid & PNG_INFO_bKGD) is non-zero.
    */
   png_color_16 background;
#endif

#ifdef PNG_oFFs_SUPPORTED
   /* The oFFs chunk gives the offset in "offset_unit_type" units rightwards
    * and downwards from the top-left corner of the display, page, or other
    * application-specific co-ordinate space.  See the PNG_OFFSET_ defines
    * below for the unit types.  Valid if (valid & PNG_INFO_oFFs) non-zero.
    */
   png_int_32 x_offset; /* x offset on page */
   png_int_32 y_offset; /* y offset on page */
   png_byte offset_unit_type; /* offset units type */
#endif

#ifdef PNG_pHYs_SUPPORTED
   /* The pHYs chunk gives the physical pixel density of the image for
    * display or printing in "phys_unit_type" units (see PNG_RESOLUTION_
    * defines below).  Data is valid if (valid & PNG_INFO_pHYs) is non-zero.
    */
   png_uint_32 x_pixels_per_unit; /* horizontal pixel density */
   png_uint_32 y_pixels_per_unit; /* vertical pixel density */
   png_byte phys_unit_type; /* resolution type (see PNG_RESOLUTION_ below) */
#endif

#ifdef PNG_hIST_SUPPORTED
   /* The hIST chunk contains the relative frequency or importance of the
    * various palette entries, so that a viewer can intelligently select a
    * reduced-color palette, if required.  Data is an array of "num_palette"
    * values in the range [0,65535]. Data valid if (valid & PNG_INFO_hIST)
    * is non-zero.
    */
   png_uint_16p hist;
#endif

#ifdef PNG_cHRM_SUPPORTED
   /* The cHRM chunk describes the CIE color characteristics of the monitor
    * on which the PNG was created.  This data allows the viewer to do gamut
    * mapping of the input image to ensure that the viewer sees the same
    * colors in the image as the creator.  Values are in the range
    * [0.0, 0.8].  Data valid if (valid & PNG_INFO_cHRM) non-zero.
    */
   png_fixed_point x_white;
   png_fixed_point y_white;
   png_fixed_point x_red;
   png_fixed_point y_red;
   png_fixed_point x_green;
   png_fixed_point y_green;
   png_fixed_point x_blue;
   png_fixed_point y_blue;
#endif

#ifdef PNG_pCAL_SUPPORTED
   /* The pCAL chunk describes a transformation between the stored pixel
    * values and original physical data values used to create the image.
    * The integer range [0, 2^bit_depth - 1] maps to the floating-point
    * range given by [pcal_X0, pcal_X1], and are further transformed by a
    * (possibly non-linear) transformation function given by "pcal_type"
    * and "pcal_params" into "pcal_units".  Please see the PNG_EQUATION_
    * defines below, and the PNG-Group's PNG extensions document for a
    * complete description of the transformations and how they should be
    * implemented, and for a description of the ASCII parameter strings.
    * Data values are valid if (valid & PNG_INFO_pCAL) non-zero.
    */
   png_charp pcal_purpose;  /* pCAL chunk description string */
   png_int_32 pcal_X0;      /* minimum value */
   png_int_32 pcal_X1;      /* maximum value */
   png_charp pcal_units;    /* Latin-1 string giving physical units */
   png_charpp pcal_params;  /* ASCII strings containing parameter values */
   png_byte pcal_type;      /* equation type (see PNG_EQUATION_ below) */
   png_byte pcal_nparams;   /* number of parameters given in pcal_params */
#endif

/* New members added in libpng-1.0.6 */
   png_uint_32 free_me;     /* flags items libpng is responsible for freeing */

#if defined(PNG_UNKNOWN_CHUNKS_SUPPORTED) || \
 defined(PNG_HANDLE_AS_UNKNOWN_SUPPORTED)
   /* Storage for unknown chunks that the library doesn't recognize. */
   png_unknown_chunkp unknown_chunks;
   int unknown_chunks_num;
#endif

#ifdef PNG_iCCP_SUPPORTED
   /* iCCP chunk data. */
   png_charp iccp_name;     /* profile name */
   png_bytep iccp_profile;  /* International Color Consortium profile data */
   png_uint_32 iccp_proflen;  /* ICC profile data length */
   png_byte iccp_compression; /* Always zero */
#endif

#ifdef PNG_sPLT_SUPPORTED
   /* Data on sPLT chunks (there may be more than one). */
   png_sPLT_tp splt_palettes;
   png_uint_32 splt_palettes_num;
#endif

#ifdef PNG_sCAL_SUPPORTED
   /* The sCAL chunk describes the actual physical dimensions of the
    * subject matter of the graphic.  The chunk contains a unit specification
    * a byte value, and two ASCII strings representing floating-point
    * values.  The values are width and height corresponsing to one pixel
    * in the image.  Data values are valid if (valid & PNG_INFO_sCAL) is
    * non-zero.
    */
   png_byte scal_unit;         /* unit of physical scale */
   png_charp scal_s_width;     /* string containing height */
   png_charp scal_s_height;    /* string containing width */
#endif

#ifdef PNG_INFO_IMAGE_SUPPORTED
   /* Memory has been allocated if (valid & PNG_ALLOCATED_INFO_ROWS)
      non-zero */
   /* Data valid if (valid & PNG_INFO_IDAT) non-zero */
   png_bytepp row_pointers;        /* the image bits */
#endif

};
int readpng(void *dest, const char *fname, readpng_what what)
{
	FILE *fp;
	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;
	png_bytepp row_ptr = NULL;
	int ret = -1;

	if (dest == NULL || fname == NULL)
	{
		return -1;
	}

	fp = fopen(fname, "rb");
	if (fp == NULL)
	{
		lprintf(__FILE__ ": failed to open: %s\n", fname);
		return -1;
	}

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr)
	{
		lprintf(__FILE__ ": png_create_read_struct() failed\n");
		fclose(fp);
		return -1;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		lprintf(__FILE__ ": png_create_info_struct() failed\n");
		goto done;
	}

	// Start reading
	png_init_io(png_ptr, fp);
	png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_STRIP_ALPHA | PNG_TRANSFORM_PACKING, NULL);
	row_ptr = png_get_rows(png_ptr, info_ptr);
	if (row_ptr == NULL)
	{
		lprintf(__FILE__ ": png_get_rows() failed\n");
		goto done;
	}

	// lprintf("%s: %ix%i @ %ibpp\n", fname, (int)info_ptr->width, (int)info_ptr->height, info_ptr->pixel_depth);

	switch (what)
	{
		case READPNG_BG:
		{
			int height, width, h;
			unsigned short *dst = dest;
			if (info_ptr->pixel_depth != 24)
			{
				lprintf(__FILE__ ": bg image uses %ibpp, needed 24bpp\n", info_ptr->pixel_depth);
				break;
			}
			height = info_ptr->height;
			if (height > BG_HEIGHT) height = BG_HEIGHT;
			width = info_ptr->width;
			if (width > BG_WIDTH) width = BG_WIDTH;

			for (h = 0; h < height; h++)
			{
				unsigned char *src = row_ptr[h];
				int len = width;
				while (len--)
				{
#ifdef PSP
					*dst++ = ((src[2]&0xf8)<<8) | ((src[1]&0xf8)<<3) | (src[0] >> 3); // BGR
#else
					*dst++ = ((src[0]&0xf8)<<8) | ((src[1]&0xf8)<<3) | (src[2] >> 3); // RGB
#endif
					src += 3;
				}
				dst += BG_WIDTH - width;
			}
			break;
		}

		case READPNG_FONT:
		{
			int x, y, x1, y1;
			unsigned char *dst = dest;
			if (info_ptr->width != 128 || info_ptr->height != 160)
			{
				lprintf(__FILE__ ": unexpected font image size %ix%i, needed 128x160\n",
					(int)info_ptr->width, (int)info_ptr->height);
				break;
			}
			if (info_ptr->pixel_depth != 8)
			{
				lprintf(__FILE__ ": font image uses %ibpp, needed 8bpp\n", info_ptr->pixel_depth);
				break;
			}
			for (y = 0; y < 16; y++)
			{
				for (x = 0; x < 16; x++)
				{
					for (y1 = 0; y1 < 10; y1++)
					{
						unsigned char *src = row_ptr[y*10 + y1] + x*8;
						for (x1 = 8/2; x1 > 0; x1--, src+=2)
							*dst++ = ((src[0]^0xff) & 0xf0) | ((src[1]^0xff) >> 4);
					}
				}
			}
			break;
		}

		case READPNG_SELECTOR:
		{
			int x1, y1;
			unsigned char *dst = dest;
			if (info_ptr->width != 8 || info_ptr->height != 10)
			{
				lprintf(__FILE__ ": unexpected selector image size %ix%i, needed 8x10\n",
					(int)info_ptr->width, (int)info_ptr->height);
				break;
			}
			if (info_ptr->pixel_depth != 8)
			{
				lprintf(__FILE__ ": selector image uses %ibpp, needed 8bpp\n", info_ptr->pixel_depth);
				break;
			}
			for (y1 = 0; y1 < 10; y1++)
			{
				unsigned char *src = row_ptr[y1];
				for (x1 = 8/2; x1 > 0; x1--, src+=2)
					*dst++ = ((src[0]^0xff) & 0xf0) | ((src[1]^0xff) >> 4);
			}
			break;
		}

		case READPNG_320_24:
		case READPNG_480_24:
		{
			int height, width, h;
			int needw = (what == READPNG_480_24) ? 480 : 320;
			unsigned char *dst = dest;
			if (info_ptr->pixel_depth != 24)
			{
				lprintf(__FILE__ ": image uses %ibpp, needed 24bpp\n", info_ptr->pixel_depth);
				break;
			}
			height = info_ptr->height;
			if (height > 240) height = 240;
			width = info_ptr->width;
			if (width > needw) width = needw;

			for (h = 0; h < height; h++)
			{
				int len = width;
				unsigned char *src = row_ptr[h];
				dst += (needw - width) * 3;
				for (len = width; len > 0; len--, dst+=3, src+=3)
					dst[0] = src[2], dst[1] = src[1], dst[2] = src[0];
			}
			break;
		}
	}


	ret = 0;
done:
	png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : NULL, (png_infopp)NULL);
	fclose(fp);
	return ret;
}


