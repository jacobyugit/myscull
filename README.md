# Simple Character Utility for Loading Localities (Scull) as described in the third edition of "Linux Device Drivers"
# by Jonathan Corbet, Alessandro Rubini and Greg Kroah-Hartman

# To create "scull.ko" kernel module 
make

# To clean up "scull.ko" kernel module
make clean

# To insert "scull.ko" kernel module and create device nodes "/dev/scull[0-3]" for future access
sudo ./scull_load.sh 

# To review inserted "scull.ko" kernel module 
more /proc/modules		# Module name is "scull" defined in "main.c"
more /proc/devices		# Driver name is "scull" defined in "main.c"
ls /dev/scull[0-3]		# Device nodes are created by "scull_load.sh" script

# To test with "scull.ko" kernel module 
sudo ./scull_test

# To remove test device nodes "/dev/scull[0-3]"
sudo rm /dev/scull[0-3]

# To remove inserted "scull.ko" kernel module
# To verify with "/proc/modules" and "/proc/devices" after "remod" command
sudo rmmod ./scull.ko
