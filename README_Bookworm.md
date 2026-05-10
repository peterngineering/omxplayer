# OMXPlayer

# Bookworm branch builds tested. Last sync with master: Commit f312a53

OMXPlayer is a command-line video player for the Raspberry Pi. It plays
video directly from the command line and plays outside your
[desktop environment](https://en.wikipedia.org/wiki/Desktop_environment). OMXPlayer uses the
[OpenMAX](https://en.wikipedia.org/wiki/OpenMAX) API to access the hardware video decoder in the
[GPU](https://en.wikipedia.org/wiki/Graphics_processing_unit). Hardware
acceleration along with command-line use allows ultra low overhead, low power video playback. It
was originally developed as a testbed for [Kodi](https://en.wikipedia.org/wiki/Kodi_(software))
on the Raspberry Pi.

This fork adds the following features:

* **Position remembering**: If you stop playing a file, OMXPlayer will remember where you
left off and begin playing from that position next time you play the file.

* **Auto-playlists**: OMXPlayer will automatically play the next file in the folder when the
previous file finished.

* **Recently played folder**: OMXPlayer creates a folder called OMXPlayerRecent off your home
directory with links to 20 most recently played files.

* **Experimental DVD support**: OMXPlayer can play iso/dmg DVD files as well DVD block devices.

* **CEC Input**: OMXPlayer should respond to commands from your TV's remote control.

## LIMITATIONS

OMXPlayer does not support software video decoding. HEVC is not supported, and DVD video
will only play on systems with mpeg2 hardware decoding. This can be purchased on rpi3 but
isn't available on the rpi4.

DVD menus are not supported.

OMXPlayer will not work on pure 64bit systems, but will work with a 64bit kernel on a 32bit userland.

## COMPILING

To compile OMXPlayer natively on you Raspberry PI you will need around 230 MBs of RAM. You will
also need the following packages:

### Development packages

Raspios no longer comes with the required userland/firmware files but it should still be possible to
get the required files by compiling and installing RaspberryPi's
[userland](https://github.com/raspberrypi/userland) repo (requires cmake). All development files
will be in /opt/vc, as installed by cmake & 'buildme'

After installing the userland, then to compile omxplayer you will also need the following Development packages:
 
   Note: a few core raspios packages such as 'raspi-config' will be removed if installing the Development
   packages, they can be reinstalled later.

    #add development packages for rebuilding from source/git
    sudo apt install git libasound2-dev libpcre2-dev libboost-dev libcairo2-dev libdvdread-dev \
    libdbus-1-dev libraspberrypi-dev libraspberrypi0 libraspberrypi-bin \
    gcc g++ libstdc++-10-dev pkg-config binutils libc6-dev libfreetype6-dev \
    libavformat-dev perl


Once you have these installed you should be able to compile OMXPlayer with a `make`

## RUNNING

To run OMXPlayer need to disable the kms driver. You can do this by replacing it with the fake
kms driver or by disabling it completely. You can do this by changing the `dtoverlay` setting in
your system's `/boot/firmware/config.txt` file.

    # kms is enabled so omxplayer can't run
    dtoverlay=vc4-kms-v3d

    # the fake kms driver is enabled (note the f before kms)
    dtoverlay=vc4-fkms-v3d

    # the kms driver is completely disabled
    #dtoverlay=vc4-kms-v3d

To run OMXPlayer as a non-root user. For that user add them to the 'audio/video/input' groups.

    # add non root user for /dev/vchiq access
    usermod -aG audio,video,input non_root_user

You will also need the following packages for runtime on bookworm:

    # the following should pull in what is needed for raspios bookworm lite
    sudo apt install libavformat59 fonts-freefont-ttf 

Reboot to make sure the changes take effect.



### DVDs

Playing DVDs is experimental and not guaranteed to work. DVD menus are not supported.

To play DVDs you will need to purchase a [MPEG-2 licence](https://codecs.raspberrypi.com/mpeg-2-license-key/)
(only available for the Raspberry PI 3).

While `libdvdread8` comes installed with most version of Raspberry PI OS, to play most DVDs you
will need `libdvdcss2`. See [Videolan's install instructions](https://www.videolan.org/developers/libdvdcss.html)
on how to get it.

## COMMAND LINE OPTIONS AND DBUS

Please see the [manpage](omxplayer.pod) for command line options.

Please see [dbus.md](dbus.md) for details on OMXPlayer's dbus interface.

