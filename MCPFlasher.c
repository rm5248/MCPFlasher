#include <stdio.h>
#include <cserial/c_serial.h>
#include <stdint.h>

#ifdef _WIN32
#define msleep(x) Sleep(x)
#else
#define msleep(x) usleep( x * 1000 )
#endif

//frame formats
#define FRAME_BEGIN 0x01
#define SOH FRAME_BEGIN
#define FRAME_END 0x04
#define EOT FRAME_END
#define DATA_ESCAPE 0x10
#define DLE DATA_ESCAPE

// commands
#define READ_BOOTLOADER_VERSION 0x01
#define ERASE_FLASH 0x02
#define PROGRAM_FLASH 0x03
#define READ_CRC 0x04
#define JUMP_TO_APP 0x05

/**
* Static table used for the table_driven implementation.
*****************************************************************************/
static const uint16_t crc_table[ 16 ] =
{
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
	0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef
};

/****************************************************************************
* Update the crc value with new data.
*
* \param crc      The current crc value.
* \param data     Pointer to a buffer of \a data_len bytes.
* \param len		Number of bytes in the \a data buffer.
* \return         The updated crc value.
*****************************************************************************/
static unsigned short CalculateCrc( uint8_t *data, uint32_t len )
{
	uint32_t i;
	uint16_t crc = 0;

	while( len-- )
	{
		i = (crc >> 12) ^ (*data >> 4);
		crc = crc_table[i & 0x0F] ^ (crc << 4);
		i = (crc >> 12) ^ (*data >> 0);
		crc = crc_table[i & 0x0F] ^ (crc << 4);
		data++;
	}

	return (crc & 0xFFFF);
}

static int is_byte_control( uint8_t byte ){
	if( byte == FRAME_BEGIN || byte == FRAME_END || byte == DATA_ESCAPE ){
		return 1;
	}

	return 0;
}

static int write_with_escape( c_serial_port_t* port, uint8_t* data, int data_len ){
	int x;
	int status;

	for( x = 0; x < data_len; x++ ){
		uint8_t byte_to_write = data[ x ];
		int len = 1;

		if( is_byte_control( byte_to_write ) && 
			( x != data_len - 1  ) &&
			( x != 0 ) ){
			/* The byte to write is a control byte - escape it,
			 * unless we are at the last byte
			 */
			uint8_t escape = DATA_ESCAPE;
			
			status = c_serial_write_data( port, &escape, &len );

			if( status != CSERIAL_OK ){
				return status;
			}
		}

		len = 1;
		status = c_serial_write_data( port, &byte_to_write, &len );

		if( status != CSERIAL_OK ){
			return status;
		}
	}

	return 0;
}

/**
 * Remove escape characters from the given array.  This modifies the array
 *
 * @param arr The array to remove characters from
 * @param len The length of the array
 * @return The new length of the array
 */
static int remove_escape_chars( uint8_t* arr, int len ){
	int x;
	int replaced = 0;
	int y;
	int orig_len = len;

	/* We don't have to remove all escape characters, just the ones in the middle */
	for( x = 0; x < len - 1; x++ ){
		uint8_t byte = arr[ x ];
		uint8_t nextbyte = arr[ x + 1 ];
		if( byte == DATA_ESCAPE && is_byte_control( nextbyte ) ){
			/* Move the rest of the bytes down */
			for( y = x; y < len; y++ ){
				arr[ y ] = arr[ y + 1 ];
			}
			len--;
			replaced++;
		}
	}

	return orig_len - replaced;
}

static void erase_flash( c_serial_port_t* port ){
	uint8_t buffer1[10];
	uint8_t buffer2[10];
	uint16_t crc;
	int length = 10;

	buffer1[0] = SOH;
	buffer1[1] = ERASE_FLASH;
	crc = CalculateCrc( buffer1 + 1, 1 );
	buffer1[2] = (crc & 0x00FF) >> 0;
	buffer1[3] = (crc & 0xFF00) >> 8;
	buffer1[4] = FRAME_END;

	write_with_escape( port, buffer1, 5 );

	msleep( 500 );

	c_serial_read_data( port, buffer2, &length, NULL );

	length = remove_escape_chars( buffer2, length );
	if( memcmp( buffer1, buffer2, 5 ) != 0 ){
		fprintf( stderr, "ERROR: Unable to erase flash!\n" );
	}
}

static void flash( c_serial_port_t* port, const char* filename ){
	FILE* hex_file;
	char line[50];
	int numLines = 0;
	int currentLineNum = 0;
	uint8_t* lineToWrite;
	char hexString[5] = { '0', 'x', '0', '0', 0 };
	int lineToWriteLen;
	char response[10];
	int response_pos;

	hex_file = fopen( filename, "r" );
	if( hex_file == NULL ){
		fprintf( stderr, "ERROR: Can't flash using hex file %s\n", filename );
		return;
	}

	while( fgets( line, 50, hex_file ) ){
		numLines++;
	}

	printf( "File is %d lines.  Beginning flash\n", numLines );

	fseek( hex_file, 0, 0 );

	lineToWrite = malloc( 200 );
	lineToWrite[0] = SOH;
	lineToWrite[1] = PROGRAM_FLASH;

	while( fgets( line, 50, hex_file ) != NULL ){
		int linePosition = 1;
		uint16_t crc;
		lineToWriteLen = 2;

		if( line[0] != ':' ){
			continue;
		}

		/* read in the hex data and convert the ASCII to a byte */
		while( line[linePosition] != 0 && line[linePosition] != '\r' && line[linePosition] != '\n' ){
			hexString[2] = line[linePosition];
			hexString[3] = line[linePosition + 1];
			uint8_t byte = strtol( hexString, NULL, 16 );
			lineToWrite[lineToWriteLen] = byte;
			lineToWriteLen++;
			linePosition += 2;
		}

		/* Calculate our CRC and send out the data */
		crc = CalculateCrc( lineToWrite + 1, lineToWriteLen - 1 );
		lineToWrite[lineToWriteLen++] = crc & 0x00FF;
		lineToWrite[lineToWriteLen++] = (crc & 0xFF00) >> 8;
		lineToWrite[lineToWriteLen++] = FRAME_END;

		write_with_escape( port, lineToWrite, lineToWriteLen );

		/* Wait for a response back from the PIC */
		response_pos = 0;
		do{
			uint8_t byte;
			int len = 1;
			int status = c_serial_read_data( port, &byte, &len, NULL );

			if( status != CSERIAL_OK ){
				fprintf( stderr, "ERROR: Status not good!\n" );
				exit( 1 );
			}

			response[response_pos++] = byte;
		} while( response[response_pos - 1] != FRAME_END );

		currentLineNum++;
		printf( "\r%d%%", (int)(( (double)currentLineNum / (double)numLines ) * 100 ) );
		fflush( stdout );
	}

	printf( "\nDone with flash\n" );
	free( lineToWrite );
}

static void run_program( c_serial_port_t* port ){
	uint8_t data[5];
	uint16_t crc;

	data[0] = FRAME_BEGIN;
	data[1] = JUMP_TO_APP;
	crc = CalculateCrc( data + 1, 1 );
	data[2] = (crc & 0x00FF) >> 0;
	data[3] = (crc & 0xFF00) >> 8;
	data[4] = FRAME_END;

	write_with_escape( port, data, 5 );
}

static void printHelp(){
	printf( "Usage: MCPFlasher <com-port> [--flash-file file] [--run]\n" );
	printf( "    This program flashes a PIC device according to Microchip's bootloader spec.\n" );
}

int main( int argc, char** argv ){
	c_serial_port_t* serial_port;
	int status;
	uint8_t serial_buffer[ 128 ];
	uint16_t crc;
	int length;
	int escaped_len;
	int available;
	int x;
	char* flash_file = NULL;
	int run = 0;

	/* Simple arg parsing */
	for( x = 1; x < argc; x++ ){
		if( strcmp( argv[x], "-h" ) == 0 ){
			printHelp();
			return 0;
		}else if( strcmp( argv[x], "--help" ) == 0 ){
			printHelp();
			return 0;
		}
		else if( strcmp( argv[x], "--flash-file" ) == 0 ){
			if( argc == x ){
				printHelp();
				return 1;
			}
			flash_file = argv[x + 1];
			x++;
		}
		else if( strcmp( argv[x], "--run" ) == 0 ){
			run = 1;
		}
	}

	if( argc == 1 ){
		fprintf( stderr, "ERROR: Need COM port to open!\n" );
		return 1;
	}

	c_serial_new( &serial_port, NULL );
	c_serial_set_port_name( serial_port, argv[ 1 ] );
	c_serial_set_baud_rate( serial_port, CSERIAL_BAUD_115200 );

	status = c_serial_open( serial_port );
	if( status != CSERIAL_OK ){
		fprintf( stderr, "ERROR: Unable to open port: %s\n", c_serial_get_error_string( status ) );
		return 1;
	}

	/* discard all data currently in buffer. */
	do{
		int discard;
		length = sizeof( discard );
		c_serial_get_available( serial_port, &available );

		if( available == 0 ){
			break;
		}
		c_serial_read_data( serial_port,&discard, &length, NULL );
	} while( available );

	//ask for the version
	serial_buffer[0] = FRAME_BEGIN;
	serial_buffer[1] = READ_BOOTLOADER_VERSION;
	crc = CalculateCrc( serial_buffer + 1, 1 );
	serial_buffer[2] = (crc & 0x00FF) >> 0;
	serial_buffer[3] = (crc & 0xFF00) >> 8;
	serial_buffer[4] = FRAME_END;

	length = 5;

	if( write_with_escape( serial_port, serial_buffer, length ) != 0 ){
		fprintf( stderr, "ERROR: Can't write!\n" );
		return 1;
	}

	msleep( 500 );

	status = c_serial_get_available( serial_port, &available );
	if( available == 0 ){
		fprintf( stderr, "ERROR: No response from PIC, exiting\n" );
		return 1;
	}

	length = 20;
	c_serial_read_data( serial_port, serial_buffer, &length, NULL );

	length = remove_escape_chars( serial_buffer, length );

	if( length != 7 ){
		fprintf( stderr, "ERROR: Wrong length for bootloader, expected  bytes got back %d\n", length );
		return 1;
	}

	printf( "Bootloader version is %d.%d\n", serial_buffer[2], serial_buffer[3] );

	if( run ){
		run_program( serial_port );
		return 0;
	}

	if( flash_file != NULL ){
		erase_flash( serial_port );
		flash( serial_port, flash_file );
	}

	return 0;
}