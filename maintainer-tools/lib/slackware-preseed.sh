#!/bin/bash
 ##########################################################################
 #
 #     This file is part of Chiton.
 #
 #   Chiton is free software: you can redistribute it and/or modify
 #   it under the terms of the GNU General Public License as published by
 #   the Free Software Foundation, either version 3 of the License, or
 #   (at your option) any later version.
 #
 #   Chiton is distributed in the hope that it will be useful,
 #   but WITHOUT ANY WARRANTY; without even the implied warranty of
 #   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 #   GNU General Public License for more details.
 #
 #   You should have received a copy of the GNU General Public License
 #   along with Chiton.  If not, see <https://www.gnu.org/licenses/>.
 #
 #   Copyright 2022 Ed Martin <edman007@edman007.com>
 #
 ##########################################################################

#This is run after booting a slackware install image to configure it
echo 'Peeseed in progress...'

echo 'Setting up ssh'
dhcpcd eth0
/etc/rc.d/rc.dropbear start

echo 'Formatting'
dd if=/dev/zero bs=1M count=1 of=/dev/sda
echo 'type=83' | sfdisk /dev/sda
mkfs.ext4 /dev/sda1
mount /dev/sda1 /mnt/

echo 'Installing'
mount /dev/sr0 /cdrom

#pkg sets
#  ap/                f/                 l/                 tcl/               xfce/
#  d/                 k/                 n/                 x/                 y/
#  a/                 e/                 kde/               t/                 xap/


INSTALL_LOCATION=/cdrom/slackware64
#sets we need a ap d l n tcl x
#xine-lib is linked against ffmpeg, so it's required
for PKG in a ap d l n tcl x xap/xine-lib*; do
    installpkg --root /mnt --infobox --threads 4 $INSTALL_LOCATION/$PKG/*
done

#generate
geninitrd
#the installer runs these, I don't think we need it, not sure what parameters are passed during install to it
#mkfontscale
#mkfontdir
#fc-cache
cat << EOF > /mnt/etc/lilo.conf
# LILO configuration file
# generated by 'liloconfig'
#
# Start LILO global section
boot = /dev/sda
# This option loads the kernel and initrd much faster:
compact

# Boot BMP Image.
# Bitmap in BMP format: 640x480x8
  bitmap = /boot/slack.bmp
# Menu colors (foreground, background, shadow, highlighted
# foreground, highlighted background, highlighted shadow):
  bmp-colors = 255,0,255,0,255,0
# Location of the option table: location x, location y, number of
# columns, lines per column (max 15), "spill" (this is how many
# entries must be in the first column before the next begins to
# be used. We don't specify it here, as there's just one column.
  bmp-table = 60,6,1,16
# Timer location x, timer location y, foreground color,
# background color, shadow color.
  bmp-timer = 65,27,0,255

# Standard menu.
# Or, you can comment out the bitmap menu above and
# use a boot message with the standard menu:
#message = /boot/boot_message.txt

# Wait until the timeout to boot (if commented out, boot the
# first entry immediately):
prompt
# Timeout before the first entry boots.
# This is given in tenths of a second, so 600 for every minute:
timeout = 100
# Override dangerous defaults that rewrite the partition table:
change-rules
  reset

# Normal VGA console
vga = normal
# Ask for video mode at boot (time out to normal in 30s)
#vga = ask
image = /boot/vmlinuz
root = /dev/sda1
label = Linux
read-only
EOF

echo 'chiton-build' > /mnt/etc/HOSTNAME
cat << EOF > /mnt/etc/hosts
#
# hosts		This file describes a number of hostname-to-address
#		mappings for the TCP/IP subsystem. It is mostly
#		used at boot time, when no name servers are running.
#		On small systems, this file can be used instead of a
#		"named" name server.  Just add the names, addresses
#		and any aliases to this file...
#

# For loopbacking.
127.0.0.1		localhost chiton-build
::1			localhost chiton-build
EOF

sed -i 's/USE_DHCP\[0\]=""/USE_DHCP\[0\]="yes"/' /mnt/etc/rc.d/rc.inet1.conf

chmod +x /mnt/etc/rc.d/rc.{crond,httpd,messagebus,mysqld,syslog,sshd}
rm /mnt/etc/localtime || true
ln -s /usr/share/zoneinfo/US/Eastern /mnt/etc/localtime
cat <<EOF > /mnt/etc/fstab
/dev/sda1		/			ext4    defaults                        1   1
usbfs				/proc/bus/usb		usbfs	devgid=87,devmode=0664		0   0
devpts                          /dev/pts		devpts	gid=5,mode=620			0   0
proc                            /proc			proc	defaults			0   0
tmpfs                           /dev/shm		tmpfs	defaults			0   0
EOF

echo 'Preparing to chroot'
mount --bind /dev /mnt/dev/
mount --bind /sys /mnt/sys
mount --bind /proc /mnt/proc
cp /id_rsa.pub /mnt/tmp/
cp /etc/resolv.conf /mnt/etc

cat <<EOF > /mnt/tmp/chroot-setup.sh
#!/bin/bash
export PATH=/usr/bin:/bin:/bin:/sbin:/usr/sbin
echo 'Running chroot setup'
useradd -m -G users,adm,audio,video chiton-build
mkdir -p /home/chiton-build/.ssh
mv /tmp/id_rsa.pub /home/chiton-build/.ssh/authorized_keys
chmod 600 /home/chiton-build/.ssh/authorized_keys
chmod 700 /home/chiton-build/.ssh/
chown -R chiton-build:users /home/chiton-build/.ssh/
echo '%adm ALL=(ALL:ALL) NOPASSWD: ALL' > /etc/sudoers.d/nopass
chmod 440 /etc/sudoers.d/nopass
echo '##PACKAGE_MIRROR##' >> /etc/slackpkg/mirrors
#update
echo YES | slackpkg update gpg
slackpkg -batch=on -default_answer=y update
lilo
EOF
chmod +x /mnt/tmp/chroot-setup.sh

chroot /mnt /tmp/chroot-setup.sh
rm /mnt/tmp/chroot-setup.sh
