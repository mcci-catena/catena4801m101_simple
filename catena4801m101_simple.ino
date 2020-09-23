/* 

Module:  catena4801m101_test01.ino

Function:
	Test Program for Catena 4801 M101 (with SHT31 breakout).

Copyright notice and License:
	See LICENSE file accompanying this project

Author:
	Terry Moore, MCCI	August 2020

*/

#include <Catena.h>
#include <Catena_Led.h>
#include <Catena_TxBuffer.h>
#include <Catena_ModbusRtu.h>
#include <Catena_Mx25v8035f.h>
#include <Catena-SHT3x.h>
#include <Catena_Timer.h>

#include <Wire.h>
#include <Arduino_LoRaWAN.h>
#include <lmic.h>
#include <hal/hal.h>
#include <mcciadk_baselib.h>

#include <cmath>
#include <type_traits>

/****************************************************************************\
|
|	Manifest constants & typedefs.
|
\****************************************************************************/

using namespace McciCatena;
using namespace McciCatenaSht3x;

constexpr uint8_t kUplinkPort = 4;

enum class FlagsSensorPort4 : uint8_t
        {
        FlagVbat = 1 << 0,
        FlagVcc = 1 << 1,
        FlagBoot = 1 << 2,
        FlagTH = 1 << 3,	// temperature, humidity
	FlagModbus = 1 << 4, 	// Modbus data
        };

using FlagsFormat = FlagsSensorPort4;

constexpr FlagsFormat operator| (const FlagsFormat lhs, const FlagsFormat rhs)
        {
        return FlagsFormat(uint8_t(lhs) | uint8_t(rhs));
        };

FlagsFormat operator|= (FlagsFormat &lhs, const FlagsFormat &rhs)
        {
        lhs = lhs | rhs;
        return lhs;
        };


/* adjustable timing parameters */
enum    {
        // set this to interval between transmissions, in seconds
        // Actual time will be a little longer because have to
        // add measurement and broadcast time, but we attempt
        // to compensate for the gross effects below.
        CATCFG_T_CYCLE = 1 * 60,        // every 6 minutes
        CATCFG_T_CYCLE_TEST = 30,       // every 30 seconds
        CATCFG_T_CYCLE_INITIAL = 30,    // every 30 seconds initially
        CATCFG_INTERVAL_COUNT_INITIAL = 10,     // repeat for 5 minutes
        CATCFG_T_REBOOT = 30 * 24 * 60 * 60,    // reboot every 30 days
        };

/* additional timing parameters; ususually you don't change these. */
enum    {
        CATCFG_T_WARMUP = 1,
        CATCFG_T_SETTLE = 5,
        CATCFG_T_OVERHEAD = (CATCFG_T_WARMUP + CATCFG_T_SETTLE + 4),
        CATCFG_T_MIN = CATCFG_T_OVERHEAD,
        CATCFG_T_MAX = CATCFG_T_CYCLE < 60 * 60 ? 60 * 60 : CATCFG_T_CYCLE,     // normally one hour max.
        CATCFG_INTERVAL_COUNT = 30,
        };

constexpr uint32_t CATCFG_GetInterval(uint32_t tCycle)
        {
        return (tCycle < CATCFG_T_OVERHEAD + 1)
                ? 1
                : tCycle - CATCFG_T_OVERHEAD
                ;
        }

enum    {
        CATCFG_T_INTERVAL = CATCFG_GetInterval(CATCFG_T_CYCLE),
        };

// forwards
void settleDoneCb(osjob_t *pSendJob);
void warmupDoneCb(osjob_t *pSendJob);
void txFailedDoneCb(osjob_t *pSendJob);
void sleepDoneCb(osjob_t *pSendJob);
Arduino_LoRaWAN::SendBufferCbFn sendBufferDoneCb;
static Arduino_LoRaWAN::ReceivePortBufferCbFn receiveMessage;

/****************************************************************************\
|
|	Read-only data.
|
\****************************************************************************/

static const char sVersion[] = "0.1.0";

/****************************************************************************\
|
|	VARIABLES:
|
\****************************************************************************/

// the framework object.
Catena gCatena;

//
// the LoRaWAN backhaul.  Note that we use the
// Catena version so it can provide hardware-specific
// information to the base class.
//
Catena::LoRaWAN gLoRaWAN;

//
// the LED
//
StatusLed gLed (Catena::PIN_STATUS_LED);

// The flash
Catena_Mx25v8035f gFlash;
bool fFlash;

SPIClass gSPI2(
		Catena::PIN_SPI2_MOSI,
		Catena::PIN_SPI2_MISO,
		Catena::PIN_SPI2_SCK
		);

cSHT3x gTempRh(Wire);
bool fFoundTempRh;

// the Modbus error for no device
#define EXCEPTION_CODE		5

//  the job that's used to synchronize us with the LMIC code
static osjob_t sensorJob;
void sensorJob_cb(osjob_t *pJob);

// have we printed the sleep info?
bool g_fPrintedSleeping = false;

// the cycle time to use
unsigned gTxCycle;
// remaining before we reset to default
unsigned gTxCycleCount;

// data array for modbus network sharing
uint16_t au16data[18];	//Size of array changed based on no. of coils
uint8_t u8state;
uint8_t u8addr;
uint8_t u8fct;
uint8_t u16RegAdd;
uint8_t u16CoilsNo;

/**
 *  Modbus object declaration
 *  u8id : node id = 0 for host, = 1..247 for device
 *         In this case, we're the host.
 *  u8txenpin : 0 for RS-232 and USB-FTDI
 *               or any pin number > 1 for RS-485
 *
 *  We also need a serial port object, based on a UART;
 *  we default to serial port 1.  Since the type of Serial1
 *  varies from platform to platform, we use decltype() to
 *  drive the template based on the type of Serial1,
 *  eliminating a complex and never-exhaustive series of
 *  #ifs.
 */
cCatenaModbusRtu host(0, D12); // this is host and RS-485 controlled by D12.
ModbusSerial<decltype(Serial2)> mySerial(&Serial2);

#define kPowerOn		D11
#define kFRAMPowerOn		D10
#define kBoostPowerOn		D5
#define kAnalogPin1    	A1

static inline void powerOn(void)
	{
	pinMode(kPowerOn, OUTPUT);
	digitalWrite(kPowerOn, HIGH);
	}

static inline void powerOff(void)
	{
	pinMode(kPowerOn, INPUT);
	digitalWrite(kPowerOn, LOW);
	}

static inline void FRAMpowerOn(void)
	{
	pinMode(kFRAMPowerOn, OUTPUT);
	digitalWrite(kFRAMPowerOn, HIGH);
	}

static inline void FRAMpowerOff(void)
	{
	pinMode(kFRAMPowerOn, INPUT);
	digitalWrite(kFRAMPowerOn, LOW);
	}

static inline void boostpowerOn(void)
	{
	pinMode(kBoostPowerOn, OUTPUT);
	digitalWrite(kBoostPowerOn, HIGH);
	}

static inline void boostpowerOff(void)
	{
	pinMode(kBoostPowerOn, INPUT);
	digitalWrite(kBoostPowerOn, LOW);
	}

/**
 * This is a struct which contains a message to a device
 */
modbus_t datagram;

unsigned long u32wait;

/*

Name:	setup()

Function:
	Arduino setup function.

Definition:
	void setup(
		void
		);

Description:
	This function is called by the Arduino framework after
	basic framework has been initialized. We initialize the sensors
	that are present on the platform, set up the LoRaWAN connection,
	and (ultimately) return to the framework, which then calls loop()
	forever.

Returns:
	No explicit result.

*/

static constexpr const char *filebasename(const char *s, const char *p) {
    return p[0] == '\0'                     ? s                             :
           (p[0] == '/' || p[0] == '\\')    ? filebasename(p + 1, p + 1)    :
                                              filebasename(s, p + 1)        ;
}

static constexpr const char *filebasename(const char *s)
    {
    return filebasename(s, s);
    }

void setup(void)
	{
	FRAMpowerOn();		// FRAM/Flash Power On
	powerOn();		// turn on the transceiver.
	boostpowerOff();	// turn off the boost regulator
	gCatena.begin();	// set up the framework
	setup_platform();	// set up platform
	setup_flash();		// set up flash
	setup_modbus();		// set up Modbus Master
	setup_sht3x();		// set up the t/rh sensor.
  	setup_uplink();		// set up uplink
	}

void setup_uplink(void)
        {
        LMIC_setClockError(10*65536/100);

        /* trigger a join by sending the first packet */
        if (!(gCatena.GetOperatingFlags() &
                static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fManufacturingTest)))
                {
                if (! gLoRaWAN.IsProvisioned())
                        gCatena.SafePrintf("LoRaWAN not provisioned yet. Use the commands to set it up.\n");
                else
                        {
                        gLed.Set(LedPattern::Joining);

                        /* trigger a join by sending the first packet */
                        startSendingUplink();
                        }
                }
        }

/*

Name:	setup_flash()

Function:
	Setup flash for low power mode. (Not app specific.)

Definition:
	void setup_flash(
		void
		);

Description:
	This function only setup the flash in low power mode.

Returns:
	No explicit result.

*/

void setup_flash(void)
	{
	/* initialize the FLASH */
	if (gFlash.begin(&gSPI2, Catena::PIN_SPI2_FLASH_SS))
		{
		fFlash = true;
		gFlash.powerDown();
		gCatena.SafePrintf("FLASH found, put power down\n");
		}
	else
		{
		fFlash = false;
		gFlash.end();
		gSPI2.end();
		gCatena.SafePrintf("No FLASH found: check board\n");
		}
	}

/*

Name:	setup_platform()

Function:
	Setup everything related to the Catena framework. (Not app specific.)

Definition:
	void setup_platform(
		void
		);

Description:
	This function only exists to make clear what has to be done for
	the framework (as opposed to the actual application). It can be argued
	that all this should be part of the gCatena.begin() function.

Returns:
	No explicit result.

*/

void setup_platform(void)
	{
	if (!(gCatena.GetOperatingFlags() &
		static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fUnattended)))
		{
		while (!Serial)
			/* wait for Serial attach */
			yield();
		}

	gCatena.SafePrintf("\n");
	gCatena.SafePrintf("-------------------------------------------------------------------------------\n");
	gCatena.SafePrintf("This is %s V%s.\n", filebasename(__FILE__), sVersion);
		{
		char sRegion[16];
		gCatena.SafePrintf("Target network: %s / %s\n",
				gLoRaWAN.GetNetworkName(),
				gLoRaWAN.GetRegionString(sRegion, sizeof(sRegion))
				);
		}
	gCatena.SafePrintf("Enter 'help' for a list of commands.\n");
	gCatena.SafePrintf("(remember to select 'Line Ending: Newline' at the bottom of the monitor window.)\n");
	gCatena.SafePrintf("--------------------------------------------------------------------------------\n");
	gCatena.SafePrintf("\n");

#ifdef CATENA_CFG_SYSCLK
	gCatena.SafePrintf("SYSCLK: %d MHz\n", CATENA_CFG_SYSCLK);
#endif

	gLed.begin();
	gCatena.registerObject(&gLed);
	gLed.Set(LedPattern::FastFlash);

	gCatena.SafePrintf("LoRaWAN init: ");
	if (!gLoRaWAN.begin(&gCatena))
		{
		gCatena.SafePrintf("failed\n");
		gCatena.registerObject(&gLoRaWAN);
		}
	else
		{
		gCatena.SafePrintf("OK\n");
		gCatena.registerObject(&gLoRaWAN);
		}

    gLoRaWAN.SetReceiveBufferBufferCb(receiveMessage);
    setTxCycleTime(CATCFG_T_CYCLE_INITIAL, CATCFG_INTERVAL_COUNT_INITIAL);

	Catena::UniqueID_string_t CpuIDstring;

	gCatena.SafePrintf(
		"CPU Unique ID: %s\n",
		gCatena.GetUniqueIDstring(&CpuIDstring)
		);

	/* find the platform */
	const Catena::EUI64_buffer_t *pSysEUI = gCatena.GetSysEUI();

	uint32_t flags;
	const CATENA_PLATFORM * const pPlatform = gCatena.GetPlatform();

	if (pPlatform)
		{
		gCatena.SafePrintf("EUI64: ");
		for (unsigned i = 0; i < sizeof(pSysEUI->b); ++i)
			{
			gCatena.SafePrintf(
				"%s%02x", i == 0 ? "" : "-",
				pSysEUI->b[i]
				);
			}
		gCatena.SafePrintf("\n");
		flags = gCatena.GetPlatformFlags();
		gCatena.SafePrintf(
			"Platform Flags:  %#010x\n",
			flags
			);
		gCatena.SafePrintf(
			"Operating Flags:  %#010x\n",
			gCatena.GetOperatingFlags()
			);
		}
	else
		{
		gCatena.SafePrintf("**** no platform, check provisioning ****\n");
		flags = 0;
		}

	/* is it modded? */
	uint32_t modnumber = gCatena.PlatformFlags_GetModNumber(flags);


	if (modnumber != 0)
		{
		gCatena.SafePrintf("%s-M%u\n", gCatena.CatenaName(), modnumber);
		}
	else
		{
		gCatena.SafePrintf("No mods detected\n");
		}
	}

/*

Name:	setup_modbus()

Function:
	Set up the modbus we intend to use (app specific).

Definition:
	void setup_modbus(
		void
		);

Description:
	This function only exists to make clear what has to be done for
	the actual application. This is the code that cannot be part of
	the generic gCatena.begin() function.

Returns:
	No explicit result.

*/

void setup_modbus(void)
	{
	// set up the fsm
	u32wait = millis() + 1000;	// when to exit state 0
	
	/* Modbus slave parameters */
	u8addr = 01;			// current target device: 01
	u8fct = 03;			// Function code
	u16RegAdd = 0;			// start address in device
	u16CoilsNo = 6;			// number of elements (coils or registers) to read
	host.begin(&mySerial, 19200);	// baud-rate at 19200
	
	host.setTimeOut(1000);		// if there is no answer in 1000 ms, roll over
	host.setTxEnableDelay(100);	// wait 100ms before each tx
	gCatena.registerObject(&host);	// enroll the host object in the poll list.
		
	gCatena.SafePrintf("In this example Catena has been configured as Modbus host with:\n");
	gCatena.SafePrintf("- Target ID: %d\n", u8addr);
	gCatena.SafePrintf("- Function Code: %d\n", u8fct);
	gCatena.SafePrintf("- Start Register: %d\n", u16RegAdd);
	gCatena.SafePrintf("- Number of Registers: %d\n", u16CoilsNo);
	}

/*

Name:	setup_sht3x()

Function:
	Set up the temperature/humidity sensor we intend to use (app specific).

Definition:
	void setup_sht3x(
		void
		);

Description:
	This function only exists to make clear what has to be done for
	the sensor.

Returns:
	No explicit result.

*/

void setup_sht3x()
	{
	if (! gTempRh.begin())
		{
		Serial.println("gTempRH.begin() failed\n");
		fFoundTempRh = false;
		}
	else
		fFoundTempRh = true;
	}

void loop()
	{
	gCatena.poll();
	}

void startSendingUplink(void)
	{
	TxBuffer_t b;
	LedPattern savedLed = gLed.Set(LedPattern::Measuring);

	uint8_t nIndex;
	uint8_t errorStatus;
	ERR_LIST lastError;
	uint8_t flagModBus;

	flagModBus = 0;
	errorStatus = 1;
	u8state = 0;

	b.begin();
	FlagsFormat flag;

	flag = FlagsFormat(0);

	uint8_t * const pFlag = b.getp();
	b.put(0x00); /* will be set to the flags */

	// vBat is sent as 5000 * v
	float vBat = gCatena.ReadVbat();
	gCatena.SafePrintf("vBat:    %d mV\n", (int) (vBat * 1000.0f));
	b.putV(vBat);
	flag |= FlagsFormat::FlagVbat;
	
	if((int) (vBat * 1000.0f) < 2500)
		{
		boostpowerOn();
		gCatena.SafePrintf("Turning ON the Boost Regulator!!\n");
		delay(100);
		}
	
	else
		{
		boostpowerOff();
		}

	uint32_t bootCount;
	if (gCatena.getBootCount(bootCount))
		{
		b.putBootCountLsb(bootCount);
		flag |= FlagsFormat::FlagBoot;
		}

	cSHT3x::Measurements m;
	if (! gTempRh.getTemperatureHumidity(m))
		{
		Serial.println("can't read T/RH");
		}
	else
		{
		Serial.print("T(F)=");
		Serial.print(m.Temperature * 1.8 + 32);
		Serial.print("  RH=");
		Serial.print(m.Humidity);
		Serial.println("%");

		flag |= FlagsFormat::FlagTH;

		b.putT(m.Temperature);
		// no method for 2-byte RH, direct encode it.
		b.put2uf((m.Humidity / 100.0f) * 65535.0f);
		}


	while (u8state != EXCEPTION_CODE)
		{
		switch( u8state )
			{
			case 0:
				if (long(millis() - u32wait) > 0) u8state++; // wait state
				break;

			case 1:
				datagram.u8id = u8addr; // device address
				datagram.u8fct = u8fct; // function code (this one is registers read)
				datagram.u16RegAdd = u16RegAdd; // start address in device
				datagram.u16CoilsNo = u16CoilsNo; // number of elements (coils or registers) to read
				datagram.au16reg = au16data; // pointer to a memory array in the Arduino

				host.setLastError(ERR_SUCCESS);
				host.query( datagram ); // send query (only once)
				u8state++;
				break;

			case 2:
				host.McciCatena::Modbus::poll();
				if (host.getState() == COM_IDLE)
					{
					flagModBus = 1;
					u8state = EXCEPTION_CODE;	//Exception to break out of current WHILE loop.

					ERR_LIST lastError = host.getLastError();

					if (host.getLastError() != ERR_SUCCESS) 
						{
						Serial.print("Error: ");
						Serial.println(int(lastError));
						flagModBus = 0;
						break;
						}
					else 
						{
						Serial.print("Registers: ");
						for (nIndex = 0; nIndex < u16CoilsNo; ++nIndex)
							{
							Serial.print(" ");
							Serial.print(au16data[nIndex], 16);
							b.put2((uint32_t)au16data[nIndex]);
							}
						}
					Serial.println("");

					flag |= FlagsFormat::FlagModbus;

					u32wait = millis() + 100;
					}
				else
					{
					if(errorStatus == 1)
						{
						gCatena.SafePrintf("Please wait while searching for MODBUS SLAVE\n");
						errorStatus = 0;
						} 
					}
				break;
			}
		}

	if(flagModBus == 0)
		{
		if (lastError == -4)
			{
			gCatena.SafePrintf("ERROR: BAD CRC\n");
			}
		if (lastError == -6)
			{
			gCatena.SafePrintf("ERROR: NO REPLY (OR) MODBUS SLAVE UNAVAILABLE\n");
			}
		if (lastError == -7)
			{
			gCatena.SafePrintf("ERROR: RUNT PACKET\n");
			}
		}

	*pFlag = uint8_t(flag);
	
	if (savedLed != LedPattern::Joining)
		gLed.Set(LedPattern::Sending);
	else
		gLed.Set(LedPattern::Joining);

	gLoRaWAN.SendBuffer(b.getbase(), b.getn(), sendBufferDoneCb, NULL, false, kUplinkPort);
	}

static void sendBufferDoneCb(
        void *pContext,
        bool fStatus
        )
        {
        osjobcb_t pFn;

        gLed.Set(LedPattern::Settling);

        pFn = settleDoneCb;
        if (! fStatus)
                {
                if (!gLoRaWAN.IsProvisioned())
                        {
                        // we'll talk about it at the callback.
                        pFn = txNotProvisionedCb;

                        // but prevent join attempts now.
                        gLoRaWAN.Shutdown();
                        }
                else
                        gCatena.SafePrintf("send buffer failed\n");
                }

        os_setTimedCallback(
                &sensorJob,
                os_getTime()+sec2osticks(CATCFG_T_SETTLE),
                pFn
                );
        }

static void txNotProvisionedCb(
        osjob_t *pSendJob
        )
        {
        gCatena.SafePrintf("LoRaWAN not provisioned yet. Use the commands to set it up.\n");
        gLoRaWAN.Shutdown();
        gLed.Set(LedPattern::NotProvisioned);
        }

static void settleDoneCb(
	osjob_t *pSendJob
	)
	{
	const bool fDeepSleep = checkDeepSleep();

	if (! g_fPrintedSleeping)
		doSleepAlert(fDeepSleep);

	/* count what we're up to */
	updateSleepCounters();

	if (fDeepSleep)
		doDeepSleep(pSendJob);
	else
		doLightSleep(pSendJob);
	}

bool checkDeepSleep(void)
	{
	bool const fDeepSleepTest = gCatena.GetOperatingFlags() &
	static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fDeepSleepTest);
	bool fDeepSleep;

	if (fDeepSleepTest)
		{
		fDeepSleep = true;
		}
	else if (gCatena.GetOperatingFlags() &
	static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fDisableDeepSleep))
		{
		fDeepSleep = false;
		}
	else if ((gCatena.GetOperatingFlags() &
	static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fUnattended)) != 0)
		{
		fDeepSleep = true;
		}
	else
		{
		fDeepSleep = false;
		}

	return fDeepSleep;
	}

void doSleepAlert(const bool fDeepSleep)
	{
	g_fPrintedSleeping = true;

	if (fDeepSleep)
		{
		bool const fDeepSleepTest = gCatena.GetOperatingFlags() &
		static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fDeepSleepTest);
		const uint32_t deepSleepDelay = fDeepSleepTest ? 10 : 30;

		gCatena.SafePrintf("using deep sleep in %u secs",
				deepSleepDelay
				);

		// sleep and print
		gLed.Set(LedPattern::TwoShort);

		for (auto n = deepSleepDelay; n > 0; --n)
			{
			uint32_t tNow = millis();

			while (uint32_t(millis() - tNow) < 1000)
				{
				gCatena.poll();
				yield();
				}
			gCatena.SafePrintf(".");
			}
		gCatena.SafePrintf("\nStarting deep sleep.\n");
		uint32_t tNow = millis();
		while (uint32_t(millis() - tNow) < 100)
			{
			gCatena.poll();
			yield();
			}
		}
	else
		gCatena.SafePrintf("using light sleep\n");
	}

void updateSleepCounters(void)
	{
	// update the sleep parameters
	if (gTxCycleCount > 1)
		{
		// values greater than one are decremented and ultimately reset to default.
		--gTxCycleCount;
		}
	else if (gTxCycleCount == 1)
		{
		// it's now one (otherwise we couldn't be here.)
		gCatena.SafePrintf("resetting tx cycle to default: %u\n", CATCFG_T_CYCLE);

		gTxCycleCount = 0;
		gTxCycle = CATCFG_T_CYCLE;
		}
	else
		{
		// it's zero. Leave it alone.
		}
	}

void doDeepSleep(osjob_t *pJob)
	{
	bool const fDeepSleepTest = gCatena.GetOperatingFlags() &
	static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fDeepSleepTest);
	uint32_t const sleepInterval = CATCFG_GetInterval(
			fDeepSleepTest ? CATCFG_T_CYCLE_TEST : gTxCycle
			);

	/* ok... now it's time for a deep sleep */
	gLed.Set(LedPattern::Off);
	deepSleepPrepare();

	/* sleep */
	gCatena.Sleep(sleepInterval);

	/* recover from sleep */
	deepSleepRecovery();

	/* and now... we're awake again. trigger another measurement */
	sleepDoneCb(pJob);
	}

void doLightSleep(osjob_t *pJob)
	{
	uint32_t interval = sec2osticks(CATCFG_GetInterval(gTxCycle));

	gLed.Set(LedPattern::Sleeping);

	if (gCatena.GetOperatingFlags() &
	static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fQuickLightSleep))
		{
		interval = 1;
		}

	gLed.Set(LedPattern::Sleeping);
	os_setTimedCallback(
		&sensorJob,
		os_getTime() + interval,
		sleepDoneCb
		);
	}

void deepSleepPrepare()
	{
	Serial.end();
	Wire.end();
	SPI.end();
	if (fFlash)
		{
		gSPI2.end();
		}
	FRAMpowerOff();	// FRAM/Flash Power off, specific to 4801.
	powerOff();	// turn off the transceiver, specific to 4801.
	boostpowerOff();
	}

void deepSleepRecovery()
	{
	FRAMpowerOn();	// FRAM/Flash PowerOn, specific to 4801.
	powerOn();	// turn on the transceiver, specific to 4801.
	Serial.begin();
	Wire.begin();
	SPI.begin();
	if (fFlash)
		{
		gSPI2.begin();
		}
	}

void sleepDoneCb(
	osjob_t *pJob
	)
	{
	gLed.Set(LedPattern::WarmingUp);

	os_setTimedCallback(
		&sensorJob,
		os_getTime() + sec2osticks(CATCFG_T_WARMUP),
		warmupDoneCb
		);
	}

void warmupDoneCb(
	osjob_t *pJob
	)
	{
	startSendingUplink();
	}

static void receiveMessage(
	void *pContext,
	uint8_t port,
	const uint8_t *pMessage,
	size_t nMessage
	)
	{
	unsigned txCycle;
	unsigned txCount;
	unsigned controlGpio;

	if (port == 0)
		{
		gCatena.SafePrintf("MAC message:");
		for (unsigned i = 0; i < LMIC.dataBeg; ++i)
			{
			gCatena.SafePrintf(" %02x", LMIC.frame[i]);
			}
		gCatena.SafePrintf("\n");
		return;
		}

	else if (port == 2)
		{
		controlGpio = pMessage[0];

		if (controlGpio && 0x01)
			{
			gCatena.SafePrintf("GPIO turned ON\n");
			pinMode(kAnalogPin1, OUTPUT);
			digitalWrite(kAnalogPin1, HIGH);
			}

		else
			{
			gCatena.SafePrintf("GPIO turned OFF\n");
			pinMode(kAnalogPin1, INPUT);
			}
		return;
		}

	else if (! (port == 1 && 2 <= nMessage && nMessage <= 4))
		{
		gCatena.SafePrintf("invalid message port(%02x)/length(%x)\n",
			port, nMessage
			);
		return;
		}

	txCycle = (pMessage[0] << 8) | pMessage[1];

	if (txCycle < CATCFG_T_MIN || txCycle > CATCFG_T_MAX)
		{
		gCatena.SafePrintf("tx cycle time out of range: %u\n", txCycle);
		return;
		}

	// byte [2], if present, is the repeat count.
	// explicitly sending zero causes it to stick.
	txCount = CATCFG_INTERVAL_COUNT;
	if (nMessage >= 3)
		{
		txCount = pMessage[2];
		}

	setTxCycleTime(txCycle, txCount);
	}

void setTxCycleTime(
unsigned txCycle,
unsigned txCount
)
	{
	if (txCount > 0)
		gCatena.SafePrintf(
			"message cycle time %u seconds for %u messages\n",
			txCycle, txCount
			);
	else
		gCatena.SafePrintf(
			"message cycle time %u seconds indefinitely\n",
			txCycle
			);

	gTxCycle = txCycle;
	gTxCycleCount = txCount;
	}