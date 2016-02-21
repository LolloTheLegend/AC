#ifndef JPEGENC_H
#define JPEGENC_H

#include <stdio.h>

// Toca: JPEG encoder, jpeglib independent  ***EXPERIMENTAL***
// Based on work by Cristian Cuturicu
// http://www.wotsit.org/download.asp?f=jpeg&sc=335837440

typedef unsigned char BYTE;
typedef signed char SBYTE;
typedef signed short int SWORD;
typedef unsigned short int WORD;
typedef unsigned long int DWORD;
typedef signed long int SDWORD;

typedef struct
{
	BYTE B, G, R;
} colorRGB;
typedef struct
{
	BYTE length;
	WORD value;
} bitstring;

#define  Y(R,G,B) ((BYTE)( (YRtab[(R)]+YGtab[(G)]+YBtab[(B)])>>16 ) - 128);
#define Cb(R,G,B) ((BYTE)( (CbRtab[(R)]+CbGtab[(G)]+CbBtab[(B)])>>16 ));
#define Cr(R,G,B) ((BYTE)( (CrRtab[(R)]+CrGtab[(G)]+CrBtab[(B)])>>16 ));

#define writebyte(b) fputc((b),fp_jpeg_stream);
#define writeword(w) writebyte((w)/256);writebyte((w)%256);

struct APP0infotype
{
	WORD marker;
	WORD length;
	BYTE JFIFsignature[5];
	BYTE versionhi;
	BYTE versionlo;
	BYTE xyunits;
	WORD xdensity;
	WORD ydensity;
	BYTE thumbnwidth;
	BYTE thumbnheight;
	APP0infotype ()
	{
		marker = 0xFFE0;
		length = 16;
		JFIFsignature[0] = 'J';
		JFIFsignature[1] = 'F';
		JFIFsignature[2] = 'I';
		JFIFsignature[3] = 'F';
		JFIFsignature[4] = 0;
		versionhi = 1;
		versionlo = 1;
		xyunits = 0;
		xdensity = 1;
		ydensity = 1;
		thumbnwidth = 0;
		thumbnheight = 0;
	}
	;
};

struct SOF0infotype
{
	WORD marker;
	WORD length;
	BYTE precision;
	WORD height;
	WORD width;
	BYTE nrofcomponents;
	BYTE IdY;
	BYTE HVY;
	BYTE QTY;
	BYTE IdCb;
	BYTE HVCb;
	BYTE QTCb;
	BYTE IdCr;
	BYTE HVCr;
	BYTE QTCr;

	SOF0infotype ()
	{
		marker = 0xFFC0;
		length = 17;
		precision = 8;
		height = 0;
		width = 0;
		nrofcomponents = 3;
		IdY = 1;
		HVY = 0x11;
		QTY = 0;
		IdCb = 2;
		HVCb = 0x11;
		QTCb = 1;
		IdCr = 3;
		HVCr = 0x11;
		QTCr = 1;
	}
};

struct DQTinfotype
{
	WORD marker;
	WORD length;
	BYTE QTYinfo;
	BYTE Ytable[64];
	BYTE QTCbinfo;
	BYTE Cbtable[64];
	DQTinfotype ()
	{
		marker = 0xFFDB;
		length = 132;
		QTYinfo = 0;
		Ytable[0] = 0;
		QTCbinfo = 1;
		Cbtable[0] = 0;
	}
};

struct DHTinfotype
{
	WORD marker;
	WORD length;
	BYTE HTYDCinfo;
	BYTE YDC_nrcodes[16];
	BYTE YDC_values[12];
	BYTE HTYACinfo;
	BYTE YAC_nrcodes[16];
	BYTE YAC_values[162];
	BYTE HTCbDCinfo;
	BYTE CbDC_nrcodes[16];
	BYTE CbDC_values[12];
	BYTE HTCbACinfo;
	BYTE CbAC_nrcodes[16];
	BYTE CbAC_values[162];
	DHTinfotype ()
	{
		marker = 0xFFC4;
		length = 0x01A2;
		HTYDCinfo = 0;
		YDC_nrcodes[0] = 0;
		YDC_values[0] = 0;
		HTYACinfo = 0x10;
		YAC_nrcodes[0] = 0;
		YAC_values[0] = 0;
		HTCbDCinfo = 1;
		CbDC_nrcodes[0] = 0;
		CbDC_values[0] = 0;
		HTCbACinfo = 0x11;
		CbAC_nrcodes[0] = 0;
		CbAC_values[0] = 0;
	}
};

struct SOSinfotype
{
	WORD marker;
	WORD length;
	BYTE nrofcomponents;
	BYTE IdY;
	BYTE HTY;
	BYTE IdCb;
	BYTE HTCb;
	BYTE IdCr;
	BYTE HTCr;
	BYTE Ss, Se, Bf;
	SOSinfotype ()
	{
		marker = 0xFFDA;
		length = 12;
		nrofcomponents = 3;
		IdY = 1;
		HTY = 0;
		IdCb = 2;
		HTCb = 0x11;
		IdCr = 3;
		HTCr = 0x11;
		Ss = 0;
		Se = 0x3F;
		Bf = 0;
	}
};

class jpegenc
{
private:
	static BYTE zigzag[];
	static BYTE std_luminance_qt[];
	static BYTE std_chrominance_qt[];
	static BYTE std_dc_luminance_nrcodes[];
	static BYTE std_dc_luminance_values[];
	static BYTE std_dc_chrominance_nrcodes[];
	static BYTE std_dc_chrominance_values[];
	static BYTE std_ac_luminance_nrcodes[];
	static BYTE std_ac_luminance_values[];
	static BYTE std_ac_chrominance_nrcodes[];
	static BYTE std_ac_chrominance_values[];
	static WORD mask[];

	APP0infotype APP0info;
	SOF0infotype SOF0info;
	DQTinfotype DQTinfo;
	SOSinfotype SOSinfo;
	DHTinfotype DHTinfo;

	BYTE bytenew;
	SBYTE bytepos;

	void write_APP0info ( void );
	void write_SOF0info ( void );
	void write_DQTinfo ( void );
	void set_quant_table ( BYTE *basic_table,
	                       BYTE scale_factor,
	                       BYTE *newtable );
	void set_DQTinfo ( int quality );
	void write_DHTinfo ( void );
	void set_DHTinfo ( void );
	void write_SOSinfo ( void );
	void write_comment ( BYTE *comment );
	void writebits ( bitstring bs );
	void compute_Huffman_table ( BYTE *nrcodes,
	                             BYTE *std_table,
	                             bitstring *HT );
	void init_Huffman_tables ( void );
	void exitmessage ( const char *error_message );
	void set_numbers_category_and_bitcode ();
	void precalculate_YCbCr_tables ();
	void prepare_quant_tables ();
	void fdct_and_quantization ( SBYTE *data, float *fdtbl, SWORD *outdata );

	void process_DU ( SBYTE *ComponentDU,
	                  float *fdtbl,
	                  SWORD *DC,
	                  bitstring *HTDC,
	                  bitstring *HTAC );
	void load_data_units_from_RGB_buffer ( WORD xpos, WORD ypos );

	void main_encoder ( void );
	void init_all ( int qfactor );

public:
	jpegenc ()
	{
		bytenew = 0;
		bytepos = 7;
	}

	bitstring YDC_HT[12];
	bitstring CbDC_HT[12];
	bitstring YAC_HT[256];
	bitstring CbAC_HT[256];

	BYTE *category_alloc;
	BYTE *category;
	bitstring *bitcode_alloc;
	bitstring *bitcode;

	SDWORD YRtab[256], YGtab[256], YBtab[256];
	SDWORD CbRtab[256], CbGtab[256], CbBtab[256];
	SDWORD CrRtab[256], CrGtab[256], CrBtab[256];
	float fdtbl_Y[64];
	float fdtbl_Cb[64];

	SBYTE YDU[64];
	SBYTE CbDU[64];
	SBYTE CrDU[64];
	SWORD DU_DCT[64];
	SWORD DU[64];

	colorRGB *RGB_buffer;   //image to be encoded
	WORD width, height;     //image dimensions divisible by 8

	FILE *fp_jpeg_stream;

	int encode ( const char *filename,
	             colorRGB *pixels,
	             int iw,
	             int ih,
	             int jpegquality );
};

#endif

