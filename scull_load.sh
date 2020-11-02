#!/bin/bash
#
# scull_load inserts "${module}" module and creates "/dev/${device}[0..3]" nodes

driver="scull"

module="scull"
device="scull"

# mode="664"

# Invoke "insmod" with all arguments we got and use a pathname, as newer modutils don't look in . by default.
# the first module parameter if any for "insmod" is an user-selected major number
#
/sbin/insmod ./${module}.ko $* || exit 1

# Remove stale nodes if any and then recreate nodes
rm -f /dev/${device}[0-3]

major=$(awk "\$2 == \"$driver\" {print \$1}" /proc/devices)

mknod /dev/${device}0 c $major 0
mknod /dev/${device}1 c $major 1
mknod /dev/${device}2 c $major 2
mknod /dev/${device}3 c $major 3

# Set appropriate group/permissions, and change the group.
# Not all distributions have staff, some have "wheel" instead.
# group="staff"
# grep -q '^staff:' /etc/group || group="wheel"

# chgrp $group /dev/${device}[0-3]
# chmod $mode  /dev/${device}[0-3]
