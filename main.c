/* 
 * Simple Character Utility for Loading Localities (Scull)
 */

#include <linux/init.h>
#include <linux/module.h>			// THIS_MODULE
#include <linux/kernel.h>			// printk()
#include <linux/slab.h>				// kmalloc()
#include <linux/fs.h>				// file stuff, everything...
#include <linux/errno.h>			// error codes
#include <linux/types.h>			// size_t
#include <linux/fcntl.h>			// O_ACCMODE
#include <linux/cdev.h>				// for cdev struct
#include <asm/uaccess.h>			// copy_to_user()/copy_from_user()

MODULE_LICENSE("Dual BSD/GPL");

#define SCULL_MAJOR     (0)			// Dynamic alloc dev_t by default
#define SCULL_MINOR     (0)
#define SCULL_NDEVS     (4)

#define SCULL_QUANTUM   (4000)		// Size of a quantum (memory) area
#define SCULL_QSET      (1000)		// Length of quantum array

static dev_t	scull_major = (dev_t)SCULL_MAJOR;
static dev_t	scull_minor = (dev_t)SCULL_MINOR;

static int		scull_ndevs = SCULL_NDEVS;

static int		scull_quantum = (int)SCULL_QUANTUM;
static int		scull_qset    = (int)SCULL_QSET;

struct scull_qset {
	void				**data;
	struct scull_qset	*next;
};

struct scull_dev {
	struct scull_qset *data;		// Pointer to first quantum set
	int				quantum;		// The current quantum size
	int				qset;			// The current array size
	unsigned long	size;       	// Amount of data stored here
	unsigned int	access_key;		// Used by sculluid and scullpriv
	struct mutex	mutex;			// Mutual exclusion semaphore
	struct cdev		cdev;			// Char device structure
};

struct scull_dev *scull_devices;	// scull device arrays - allocated in "scull_init"


/*
 * Follow the list.
 */
struct scull_qset *
scull_follow( struct scull_dev *dev, int n )
{
	struct scull_qset *qs = dev->data;

	/* Allocate the first qset explicitly if need be. */
	if ( !qs ) {
		qs = dev->data = kmalloc( sizeof(struct scull_qset), GFP_KERNEL );
		if ( qs == NULL ) return( NULL );

		memset( qs, 0, sizeof(struct scull_qset) );
	}

	/* Then follow the list... */
	while ( n--) {
		if ( !qs->next ) {
			qs->next = kmalloc( sizeof(struct scull_qset), GFP_KERNEL );
			if ( qs->next == NULL ) return( NULL );

			memset( qs->next, 0, sizeof(struct scull_qset) );
		}
		qs = qs->next;
		continue;
	}

	return( qs );
}	/* scull_follow */


/*
 * Read and write
 */
ssize_t
scull_read( struct file *filp, char __user *buf, size_t count, loff_t *f_pos )
{
	struct scull_qset	*dptr;
	ssize_t				retval = 0;

	struct scull_dev	*dev = filp->private_data;
	int					quantum = dev->quantum, qset = dev->qset;

	int					item, s_pos, q_pos, rest;
	int					itemsize = quantum * qset;

	if ( mutex_lock_interruptible(&dev->mutex) ) return( -ERESTARTSYS );

	if ( *f_pos < dev->size ) {		// Not beyond the max size
		// Only read up to the currently available data
		if ( *f_pos + count > dev->size ) count = dev->size - *f_pos;

		// Find proper qset item, quantum index and offset in the quantum
		item = (long)*f_pos / itemsize;
		rest = (long)*f_pos % itemsize;
		s_pos = rest / quantum; q_pos = rest % quantum;

		// Follow the list up to the right position
		dptr = scull_follow( dev, item );

		if ( dptr == NULL || !dptr->data || ! dptr->data[s_pos] )	// don't fill holes
			;
		else {
			/* Read up to the end of this quantum */
			if ( count > quantum - q_pos ) count = quantum - q_pos;

			if ( !raw_copy_to_user(buf, dptr->data[s_pos] + q_pos, count) ) {
				*f_pos += count;
				retval = count;
			}
			else
				retval = -EFAULT;
		}
	}

	mutex_unlock( &dev->mutex );

	return( retval );
}	/* scull_read */


ssize_t
scull_write( struct file *filp, const char __user *buf, size_t count, loff_t *f_pos )
{
	struct scull_qset	*dptr;

	struct scull_dev	*dev = filp->private_data;
	int					quantum = dev->quantum, qset = dev->qset;

	int					item, s_pos, q_pos, rest;
	int					itemsize = quantum * qset;

	ssize_t				retval = -ENOMEM; /* Value used in "goto out" statements. */

	if ( mutex_lock_interruptible(&dev->mutex) ) return( -ERESTARTSYS );

	// Find proper qset item, quantum index and offset in the quantum
	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	// Follow the list up to the right position
	dptr = scull_follow( dev, item );
	if ( dptr == NULL ) goto out;

	if ( !dptr->data ) {
		dptr->data = kmalloc( qset * sizeof(char *), GFP_KERNEL );
		if ( !dptr->data ) goto out;

		memset( dptr->data, 0, qset * sizeof(char *) );
	}

	if ( !dptr->data[s_pos] ) {
		dptr->data[s_pos] = kmalloc( quantum, GFP_KERNEL );
		if ( !dptr->data[s_pos] ) goto out;
	}

	// Write only up to the end of this quantum
	if ( count > quantum - q_pos ) count = quantum - q_pos;

	if ( raw_copy_from_user(dptr->data[s_pos]+q_pos, buf, count) ) {
		retval = -EFAULT;
		goto out;
	}
	*f_pos	+= count;
	retval	= count;

	// Update the size
	if ( dev->size < *f_pos ) dev->size = *f_pos;

  out:
	mutex_unlock( &dev->mutex );

	return( retval );
}	/* scull_write */


/*
 * Empty out specific scull device - must be called with the device mutex held
 */
int
scull_trim( struct scull_dev *dev )
{
	struct scull_qset	*next, *dptr;
	int					i, qset = dev->qset;   /* "dev" is not-null */

	for ( dptr = dev->data ; dptr ; dptr = next ) { /* all the items on qset list */
		if ( dptr->data ) {
			for ( i = 0 ; i < qset ; i++ ) kfree( dptr->data[i] );
			kfree( dptr->data );
			dptr->data = NULL;
		}
		next = dptr->next;
		kfree( dptr );
	}

	// Reset the qset parameters
	dev->quantum	= scull_quantum;
	dev->qset		= scull_qset;
	dev->size		= (unsigned long)0;
	dev->data		= NULL;

	return( 0 );
}	/* scull_trim */


/*
 * Open and close
 */
int
scull_release( struct inode *inode, struct file *filp )
{
    // printk( KERN_DEBUG "scull: process %i (%s) success release minor(%u) file\n", current->pid, current->comm, iminor(inode) );
    printk( KERN_DEBUG "scull: process %i (%s) success release minor(%u) file\n", current->pid, current->comm, iminor(inode) );

	return( 0 );
}	/* scull_release */


int
scull_open( struct inode *inode, struct file *filp )
{
	struct scull_dev	*dev;

    printk( KERN_DEBUG "scull: open" );

	// "cdev" from "cdev_add()" is stored in "inode->i_cdev" when device inode is created
	dev = container_of( inode->i_cdev, struct scull_dev, cdev );
	filp->private_data = dev;	/* Save "dev" to be used by other methods - thread-safe*/

	// If the device is opened write-only, trim it to a length of 0.
	if ( (filp->f_flags & O_ACCMODE) == O_WRONLY ) {
		if ( mutex_lock_interruptible(&dev->mutex) ) return( -ERESTARTSYS );
		scull_trim( dev );

		mutex_unlock( &dev->mutex );
	}

    // printk( KERN_DEBUG "scull: process %i (%s) success open minor(%u) file\n", current->pid, current->comm, iminor(inode) );
    printk( KERN_DEBUG "scull: process %i (%s) success open minor(%u) file\n", current->pid, current->comm, iminor(inode) );

	return( 0 );
}	/* scull_open */


/*
 * The "extended" operations -- only seek.
 */
loff_t
scull_llseek( struct file *filp, loff_t off, int whence )
{
	loff_t				newpos;
	struct scull_dev	*dev = filp->private_data;

	switch( whence ) {
		case 0: /* SEEK_SET */
			newpos = off;
			break;

		case 1: /* SEEK_CUR */
			newpos = filp->f_pos + off;
			break;

		case 2: /* SEEK_END */
			newpos = dev->size + off;
			break;

		default: /* can't happen */
			return -EINVAL;
	}	/* switch */

	if ( newpos < 0 ) return -EINVAL;

	filp->f_pos = newpos;

	return( newpos );
}	/* scull_llseek */


/*
 * Set up the char_dev structure for this scull device.
 */
struct file_operations scull_fops = {
	.owner		= THIS_MODULE,
	.llseek 	= scull_llseek,
	.read		= scull_read,
	.write		= scull_write,
	// .unlocked_ioctl = scull_ioctl,
	.open		= scull_open,
	.release =  scull_release,
};

static void
scull_setup_cdev( struct scull_dev *dev, int index )
{
	int		err;
	dev_t	devno = MKDEV( scull_major, scull_minor + index );
    
	cdev_init( &(dev->cdev), &scull_fops );	// Initialize cdev structure
	dev->cdev.owner	= THIS_MODULE;
	dev->cdev.ops	= &scull_fops;

	err = cdev_add( &(dev->cdev), devno, 1 );
	if ( err )
		printk( KERN_NOTICE "Error %d adding scull%d", err, index );
	else
        printk( KERN_INFO "scull: %d add success\n", index );
}	/* scull_setup_cdev */


/*
 * Module cleanup
 */
void
scull_cleanup( void )
{
	int		i;
	dev_t	devno = MKDEV( scull_major, scull_minor );

	/* Get rid of our char dev entries. */
	if ( scull_devices ) {
		for ( i = 0 ; i < scull_ndevs ; i++ ) {
			scull_trim( scull_devices + i );
			cdev_del( &(scull_devices[i].cdev) );
		}
		kfree( scull_devices );
	}

	/* Cleanup_module is never called if registering failed. */
	unregister_chrdev_region( devno, scull_ndevs );
    printk( KERN_INFO "scull: cleanup success\n" );
}	/* scull_cleanup */


/*
 * Module initialization
 */
static int __init
scull_init( void )
{
	dev_t	dev = 0;
	int		result, i;

	/* Get a range of minor numbers to work with, asking for a dynamic major unless directed otherwise at load time. */
	if ( scull_major != (dev_t)SCULL_MAJOR ) {
		dev = MKDEV( scull_major, scull_minor );
		result = register_chrdev_region( dev, scull_ndevs, "scull" );
	}
	else {
		result = alloc_chrdev_region( &dev, scull_minor, scull_ndevs, "scull" );
		scull_major = MAJOR( dev );
	}

	if ( result < 0 ) {
		printk( KERN_WARNING "scull: can't get major %d\n", scull_major );
		return( result );
	}

	printk( KERN_INFO "scull: get major %d success\n", scull_major );

	/* Allocate & initialize devices - this must be dynamic as the device number can be specified at load time. */
	scull_devices = kmalloc( scull_ndevs * sizeof(struct scull_dev), GFP_KERNEL );
	if ( !scull_devices ) {
		result = -ENOMEM;
		scull_cleanup();

		return( result );
	}

	memset( scull_devices, 0, scull_ndevs * sizeof(struct scull_dev) );

	for ( i = 0 ; i < scull_ndevs ; i++ ) {
		scull_devices[i].quantum	= scull_quantum;
		scull_devices[i].qset		= scull_qset;

		mutex_init( &(scull_devices[i].mutex) );

		scull_setup_cdev( &(scull_devices[i]), i );
	}

	return( 0 ); /* succeed */
}	/* scull_init */


module_init( scull_init );
module_exit( scull_cleanup );
