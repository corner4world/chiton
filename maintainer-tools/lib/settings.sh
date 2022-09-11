#!/bin/bash
set -e
#This is the key we pull and preconfigure all VMs to accept
SSH_KEY_PATH=~/.ssh/id_rsa.pub
MAINTAINER_DIR=$(pwd)/$(dirname -- "${BASH_SOURCE[0]}")/..
OS_BASE_DIR=$MAINTAINER_DIR/os-images
#blank to skip signing
SIGN_KEY=chiton@edman007.com
PATH=$PATH:/sbin:/usr/sbin
#this file is the settings for the maintainer tools
#VMs will use ports staring here
BASE_PORT=10000
SSH_OFFSET=0
HTTP_OFFSET=1
HTTPS_OFFSET=2

#offset per VM
VM_OFFSET=5
echo "Setting up $OS_TYPE/$OS_VERSION"
HOST_GPG=$(command -v gpg2 || echo gpg)
if [ "$OS_TYPE" != "none" ]; then
    #Settings for each VM kind
    #this script requires OS_TYPE and OS_VERSION is set
    if [ "$OS_TYPE" = "raspbian" ]; then
        TARGET_GPG=gpg
        if [ "$OS_VERSION" = "32" ]; then
            #raspbain32
            OS_NAME=raspbian-32
            OS_ID=0
            IMAGE_URL=https://downloads.raspberrypi.org/raspios_lite_armhf/images/raspios_lite_armhf-2022-04-07/2022-04-04-raspios-bullseye-armhf-lite.img.xz
        else
            #raspbain64
            OS_NAME=raspbian-64
            OS_ID=1
            IMAGE_URL=https://downloads.raspberrypi.org/raspios_lite_arm64/images/raspios_lite_arm64-2022-04-07/2022-04-04-raspios-bullseye-arm64-lite.img.xz
        fi
    elif [ "$OS_TYPE" = "debian" ]; then
        TARGET_GPG=gpg
        if [ "$OS_VERSION" = "11" ]; then
            #debian stable
            OS_NAME=debian-11
            OS_ID=2
            PACKAGE_URL=https://cdimage.debian.org/debian-cd/11.4.0/amd64/iso-cd/debian-11.4.0-amd64-netinst.iso
        elif [ "$OS_VERSION" = "testing" ]; then
            OS_NAME=debian-testing
            OS_ID=3
            #debian testing
            PACKAGE_URL=https://cdimage.debian.org/cdimage/daily-builds/daily/arch-latest/amd64/iso-cd/debian-testing-amd64-netinst.iso

        fi
    else
        echo "Unsupported OS - $OS_TYPE - $OS_VERSION"
    fi

    echo "Setup for $OS_NAME"
    #Setup variables determined by this script
    OS_DIR=$OS_BASE_DIR/$OS_NAME
    BASE_PORT=`expr $BASE_PORT + $OS_ID \* $VM_OFFSET`
    SSH_PORT=`expr $BASE_PORT + $SSH_OFFSET`
    HTTP_PORT=`expr $BASE_PORT + $HTTP_OFFSET`
    HTTPS_PORT=`expr $BASE_PORT + $HTTPS_OFFSET`
    SSH_OPTS="-o UserKnownHostsFile=/dev/null -o StrictHostKeychecking=no -oPort=$SSH_PORT"
fi

if [ "x$CHITON_FUNCTIONS" = "x" ]; then
    CHITON_FUNCTIONS=1
    run_remote_cmd() {
        ssh $SSH_OPTS chiton-build@localhost "$1"
    }

    run_remote_script() {
        scp $SSH_OPTS "$1" chiton-build@localhost:run.sh
        run_remote_cmd 'chmod +x ./run.sh && ./run.sh'
    }


    wait_to_boot() {
        while true; do
            if [ -r $OS_DIR/run.pid ]; then
                PID=$(cat $OS_DIR/run.pid)
                if ps -p $PID > /dev/null; then
                    if ssh $SSH_OPTS chiton-build@localhost 'echo Running' | grep Running ; then
                        return 0
                    fi
                fi
            fi
        done
        return 1
    }


    #CHITON_FUNCTIONS
fi