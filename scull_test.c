#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <linux/stat.h>
#include <linux/types.h>

#define	DEVPATH	"/dev/scull0"
#define WRTPAT	"atomlzlzatomlzlz"

void
main()
{
	char s[10];
	int i, retval = -1;

	int fd = open( DEVPATH, O_RDWR );

	for ( i = 0 ; i < 10 ; i++ ) s[i] = '\0';

	if( fd < 0 ) {
		printf( "****open faile**********\n" );
		return;
	}
	printf( "****open succeed**********\n\n" );
	
	retval = write( fd, WRTPAT, 16 );
	if ( retval < 0 ) {
		printf( "E write error,errno = %d\n", retval );
		return;
	}
	printf( "write %d byte: %s\n\n", retval, WRTPAT );
	close( fd );

	fd = open( DEVPATH, O_RDWR );

	retval = read( fd, s, 4 );
	if ( retval < 0 ) {
		printf( "E read error,errno = %d\n", retval );
		return;
	}
	printf( "read %d bytes\n", retval );
	printf( "s = %s\n\n", s );

	retval = read( fd, s, 4 );
	if ( retval < 0 ) {
		printf( "E read error,errno = %d\n", retval );
		return;
	}
	printf( "read %d bytes\n", retval );
	printf( "s = %s\n", s );

	close( fd );
}
