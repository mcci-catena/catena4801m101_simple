# catena4801m101_simple

Demo of Catena 4801m101, which is a Catena 4801 with Adafruit SHT31 breakout board, set up for development.

<!-- TOC depthFrom:2 updateOnSave:true -->

- [Introduction](#introduction)
- [Getting Started](#getting-started)
	- [Clone this repository into a suitable directory on your system](#clone-this-repository-into-a-suitable-directory-on-your-system)
	- [Install the MCCI STM32 board support library](#install-the-mcci-stm32-board-support-library)
	- [Select your desired band](#select-your-desired-band)
	- [Installing the required libraries](#installing-the-required-libraries)
		- [List of required libraries](#list-of-required-libraries)
	- [Build and Download](#build-and-download)
	- [Load the sketch into the Catena](#load-the-sketch-into-the-catena)
- [If needed, set the identity of your Catena 4801](#if-needed-set-the-identity-of-your-catena-4801)
- [Notes](#notes)
	- [Downloading images](#downloading-images)
	- [Downlink Data Format](#downlink-data-format)
	- [Data Format](#data-format)

<!-- /TOC -->
## Introduction

This sketch demonstrates the MCCI Catena&reg; 4601 M101 as a remote temperature/humidity/light sensor using a LoRaWAN&reg;-techology network to transmit to a remote server.

The Catena 4801 M101 is a breadboard LoRaWAN-enabled sensor device with the following sensors:

- Sensirion SHT-31-DIS-F temperature and humidity sensor on an Adafruit breakout board
- Modbus-capable RS485 interface.

Documents on the MCCI Catena 4601 are at https://github.com/mcci-catena/HW-Designs/tree/master/Boards/Catena-4801.

## Getting Started

In order to use this code, you must do several things:

1. Clone this repository into a suitable directory on your system.
2. Install the MCCI Arduino board support package (BSP).
3. Install the required Arduino libraries using `git`.
4. Build the sketch and download to your Catena 4801 M101.

After you have loaded the firmware, you have to set up the Catena 4801.

This sketch uses the Catena-Arduino-Platform library to store critical information on the integrated FRAM. There are several kinds of information. Some things only have to be programmed once in the life of the board; other things must be programmed whenever you change network connections. Entering this information this involves entering USB commands via the Arduino serial monitor.

- We call information about the 4801 M101 that (theoretically) never changes "identity".
- We call information about the LoRaWAN "provisioning".

### Clone this repository into a suitable directory on your system

This is best done from a command line. You can use a number of techniques, but since you'll need a working git shell, we recommend using the command line.

On Windows, we strongly recommend use of "git bash", available from [git-scm.org](https://git-scm.com/download/win). Then use the "git bash" command line system that's installed by the download.

The goal of this process is to create a directory called `{somewhere}/Catena-Sketches`. You get to choose `{somewhere}`. Everyone has their own convention; the author typically has a directory in his home directory called `sandbox`, and then puts projects there.

Once you have a suitable command line open, you can enter the following commands. In the following, change `{somewhere}` to the directory path where you want to put `catena4801m101_simple`.

```console
$ cd {somewhere}
$ git clone https://github.com/mcci-catena/catena4801m101_simple
Cloning into 'catena4801m101_simple'...
...

$ # get to the right subdirectory
$ cd catena4801m101_simple

$ # confirm that you're in the right place.
$ ls
catena4801m101_simple.ino  git-repos.dat  README.md
```

### Install the MCCI STM32 board support library

Open the Arduino IDE. Go to `File>Preferences>Settings`. Add `https://github.com/mcci-catena/arduino-boards/raw/master/BoardManagerFiles/package_mcci_index.json` to the list in `Additional Boards Manager URLs`.

If you already have entries in that list, use a comma (`,`) to separate the entry you're adding from the entries that are already there.

Next, open the board manager. `Tools>Board:...`, and get up to the top of the menu that pops out -- it will give you a list of boards. Search for `MCCI` in the search box and select `MCCI Catena STM32 Boards`. An `[Install]` button will appear to the right; click it.

Then go to `Tools>Board:...` and scroll to the bottom. You should see `MCCI Catena 4801`; select that.  From the IDE's point of view, the Catena 4801 and the Catena 4801 M101 are identical.

### Select your desired band

When you select a board, the default LoRaWAN region is set to US-915, which is used in North America and much of South America. If you're elsewhere, you need to select your target region. You can do it in the IDE:

![Select Band Plan](extra/assets/select-band-plan.gif)

As the animation shows, use `Tools>LoRaWAN Region...` and choose the appropriate entry from the menu.

### Installing the required libraries

This sketch uses several sensor libraries.

The script [`git-boot.sh`](./git-boot.sh) in the top directory of this repo will get all the things you need.

It's easy to run, provided you're on Windows, macOS, or Linux, and provided you have `git` installed. We tested on Windows with git bash from https://git-scm.org, on macOS 10.11.3 with the git and bash shipped by Apple, and on Ubuntu 16.0.4 LTS (64-bit) with the built-in bash and git from `apt-get install git`.

You can make sure your library directory is populated using `git-boot.sh`.

```console
$ cd catena4801m101_simple
$ ./git-boot.sh
Cloning into 'Catena-Arduino-Platform'...
remote: Counting objects: 1201, done.
remote: Compressing objects: 100% (36/36), done.
remote: Total 1201 (delta 27), reused 24 (delta 14), pack-reused 1151
Receiving objects: 100% (1201/1201), 275.99 KiB | 0 bytes/s, done.
Resolving deltas: 100% (900/900), done.
...

==== Summary =====
No repos with errors
No repos skipped.
*** no repos were pulled ***
Repos downloaded:      Catena-Arduino-Platform arduino-lorawan Catena-mcciadk arduino-lmic MCCI_FRAM_I2C MCCI-Catena-HS300x
```

It has a number of advanced options; use `./git-boot.sh -h` to get help, or look at the source code [here](./git-boot.sh).

**Beware of Catena-Sketches issue #18**.  If you happen to already have libraries installed with the same names as any of the libraries in `git-repos.dat`, `git-boot.sh` will silently use the versions of the library that you already have installed. (We hope to soon fix this to at least tell you that you have a problem.)

#### List of required libraries

This sketch depends on the following libraries.

* https://github.com/mcci-catena/Adafruit_FRAM_I2C
* https://github.com/mcci-catena/Catena-Arduino-Platform
* https://github.com/mcci-catena/arduino-lorawan
* https://github.com/mcci-catena/Catena-mcciadk
* https://github.com/mcci-catena/arduino-lmic
* https://github.com/mcci-catena/Modbus-for-Arduino
* https://github.com/mcci-catena/MCCI-Catena-SHT3x

### Build and Download

Shutdown the Arduino IDE and restart it, just in case.

If you haven't already, use File>Open to load the `catena4801_simple.ino` sketch.

Ensure selected board is 'MCCI Catena 4801' (in the GUI, check that `Tools`>`Board "..."` says `"MCCI Catena 4801"`.

Make sure the Serial Interface is set to "Two HW Serial". For initial testing, we recommend setting the system clock to 16 MHz.  Make sure your target network is selected.

Follow normal Arduino IDE procedures to build the sketch: `Sketch`>`Verify/Compile`. If there are no errors, go to the next step.

### Load the sketch into the Catena

Make sure the correct port is selected in `Tools`>`Port`.

Load the sketch into the Catena using `Sketch`>`Upload` and move on to provisioning.

## If needed, set the identity of your Catena 4801

We did this at MCCI. But if you need to do it again, contact us for assistance.

## Notes

### Downloading images

An STLINK-V2 is used to download code to the 4801 M101. We attached posts, for easy use with the MCCI debug starter kit.

Because the 4801 M101 is essentially identical to the 4801 other than sensors, you may also refer to the detailed procedures that are part of the Catena 4801 user manual. 

### Downlink data Format

Data received via 'port 1' is used to change the Time interval (first two bytes) and TxCount (third byte).

Example: 00 F0 0A
	
	Time interval - 240
	TxCount - 10
Bytes | Length of corresponding field (bytes) | Description
:---:|:---:|:----
00 F0 | 2 | Time interval
0A | 1 | TxCount

Downlink data received via 'port 2' is used to control the 'GPIO pins A1', here only one byte is used for current application to enable/disable GPIO pin A1. Also can be extended to other features by using remaining 7 bits of the byte.

Byte values | Operation performed
:---:|:---
00 | Turns OFF GPIO pin A1
01 | Turns ON GPIO pin A1

### Data Format

Refer to the [Protocol Description](extra/catena-message-port4-format.md) in the `extras` directory for information on how data is encoded.
