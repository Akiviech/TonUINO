
#include <DFMiniMp3.h>
#include <EEPROM.h>
#include <JC_Button.h>
#include <MFRC522.h>
#include <SPI.h>
#include <SoftwareSerial.h>

/****************************************************************
 * Compile Switches
 ***************************************************************/
/*
 * Debug output modes
 * Level based debug output on serial console
 *
 * Level:
 * 1 - Only State output
 * 2 - State and additional outputs
 * 3 - all outputs
 */
#define DEBUG           0

/*
 * Pololu Power Switch
 *
 * Switch is used to switch off Arduino completely and provide
 * a wakeup functionality via additional external switch
 */
#define POWER_SWITCH	0

/****************************************************************
 * Global defines
 ***************************************************************/
#define INITIAL_VOLUME  		7		/* Initial volume at Startup */
#define MAX_VOLUME      		15		/* Maximum allowed volume */

#define MAX_FOLDERS				99		/* Maximum folders on SD Card */

#define buttonPause     		A0
#define buttonUp        		A1
#define buttonDown      		A2
#define busyPin         		4

#if (POWER_SWITCH == 1)
#define powerSwitchPin 			A3		/* TODO check why A7 is not working */
#endif

#define LONG_PRESS      		1000

#define RST_PIN         		9		/* MFRC522 Reset Pin */
#define SS_PIN          		10		/* MFRC522 SS Pin */

#if (POWER_SWITCH == 1)
#define MAX_ON_TIME				(180 * 60)		/* maximum on time in seconds */
#define ON_TIME_AFTER_PAUSE		(20 * 60)		/* time to power off after paused */
#define ON_TIME_AFTER_STOP		(10 * 60)		/* time to power off after stopped or nothing happened */
#endif

/****************************************************************
 * Global variables
 ***************************************************************/
uint16_t switchOffTimer = 0;	/* Switch Off timer in seconds */
uint16_t lastTime = 0;	/* last time in seconds */
static uint16_t secondsRunning = 0;	/* Uptime */
uint16_t numTracksInFolder;
uint16_t currentTrack;
static uint16_t _lastTrackFinished;
uint8_t numberOfCards = 0;
uint8_t currentPlayerState;
byte sector = 1;
byte blockAddr = 4;
byte trailerBlock = 7;
bool successRead;
bool ignorePauseButton = false;
bool ignoreUpButton = false;
bool ignoreDownButton = false;
bool knownCard = false;

/****************************************************************
 * Create Instances and instance variables
 ***************************************************************/
MFRC522 mfrc522(SS_PIN, RST_PIN); /* Create MFRC522 */
MFRC522::MIFARE_Key key;
MFRC522::StatusCode status;
SoftwareSerial mySoftwareSerial(2, 3); /* RX, TX */
Button pauseButton(buttonPause);
Button upButton(buttonUp);
Button downButton(buttonDown);

/****************************************************************
 * Global enums
 ***************************************************************/
enum eModes
{
	Hoerspielmodus = 1,
	Albummodus,
	PartyModus,
	EinzelModus,
	HoerbuchModusSave,
	AdminModus
};

enum eModeHandling
{
	Idle = 1,
	NewCardDetected,
	PlayNextTrack,
	PlayPreviousTrack,
	VolumeUp,
	VolumeDown,
	StartPause,
	PlayAdvertisement,
	EraseCard,
	SetupCard
};

enum eStates
{
	Init = 1,
	CardSetupFolderSelection,
	CardSetupModeSelection,
	CardSetupFileSelection,
	Stop,
	Play,
	Pause,
	ReadyForShutdown
};

enum eAdvertisements
{
	AdvNewTag = 300,
	Adv310 =310,
	AdvModeRandomEpisode = 311,
	AdvModeAlbum = 312,
	AdvModeParty = 313,
	AdvModeSingleTrack = 314,
	AdvModeAudioBook = 315,
	AdvModeAdmin = 316,
	AdvSelectFile = 320,
	Adv330 = 330,
	Adv331 = 331,
	Adve332 = 332,
	Adv400 = 400,
	AdvError = 401,
	AdvResetTag = 800,
	AdvResetTagOk = 801,
	AdvResetAborted = 802,
	AdvResetOk = 999
};

/****************************************************************
 * Global structs
 ***************************************************************/
struct nfcTagObject
{
    uint32_t cookie;
    uint8_t version;
    uint8_t folder;
    uint8_t mode;
    uint8_t special;
}nfcTag;

/****************************************************************
 * Function Prototypes
 ***************************************************************/
bool isPlaying(void);

int voiceMenu(int numberOfOptions, int startMessage, int messageOffset,
              bool preview = false, int previewFromFolder = 0);

void initButtons(void);
void readButtons(void);
void handleButtons(void);

void modeHandler(uint8_t mode);

void initDFPlayer(void);

void initCardReader(void);
bool readCard(void);
void writeCard(void);
void setupCard(void);

#if (POWER_SWITCH ==1)
void InitPowerSwitch(void);
void ProcessPowerSwitch(void);
void ResetSwitchOffTimer(void);
#endif

/****************************************************************
 * MP3 notification class
 *
 * Notifications from MP3 player module
 ***************************************************************/
class Mp3Notify
{
    public:
    static void OnError(uint16_t errorCode)
    {
        // see DfMp3_Error for code meaning
        #if (DEBUG > 0)
        Serial.println();
        Serial.print("MP3 Com Error: ");
        switch(errorCode)
        {
        	case DfMp3_Error_Busy:
        		Serial.println("Busy Error");
        		break;
        	case DfMp3_Error_Sleeping:
        		Serial.println("Sleeping Error");
        		break;
        	case DfMp3_Error_SerialWrongStack:
        		Serial.println("SerialWrongStack Error");
        		break;
        	case DfMp3_Error_CheckSumNotMatch:
        		Serial.println("Checksum Error");
        		break;
        	case DfMp3_Error_FileIndexOut:
        		Serial.println("FileIndexOut Error");
        		break;
        	case DfMp3_Error_FileMismatch:
        		Serial.println("FileMismatch Error");
        		break;
        	case DfMp3_Error_Advertise:
        		Serial.println("Advertise Error");
        		break;
        	case DfMp3_Error_General:
        		Serial.println("General Error");
        		break;
        	default:
        		Serial.println("Unknown Error");
        		break;
        }
        #endif
    }
    static void OnPlayFinished(uint16_t track)
    {
        #if (DEBUG > 0)
        Serial.print("MP3 Track beendet");
        Serial.println(track);
        #endif
        delay(100);
        if ( (track == _lastTrackFinished)
			 || (knownCard == false))
		{
			/*
			 *  Letzter Track erreicht
			 *
			 *  Wenn eine neue Karte angelernt wird soll das Ende eines Tracks nicht verarbeitet werden
			*/
        	currentPlayerState = Stop;
		}
		else
		{
			modeHandler(PlayNextTrack);
		}
		/* aktuellen track zwischenspeichern */
		_lastTrackFinished = track;
    }
    static void OnCardOnline(uint16_t code)
    {
        #if (DEBUG > 0)
        Serial.println(F("SD Karte online "));
        #endif
    }
    static void OnCardInserted(uint16_t code)
    {
        #if (DEBUG > 0)
        Serial.println(F("SD Karte bereit "));
        #endif
    }
    static void OnCardRemoved(uint16_t code)
    {
        #if (DEBUG > 0)
        Serial.println(F("SD Karte entfernt "));
        #endif
    }
};

static DFMiniMp3<SoftwareSerial, Mp3Notify> mp3(mySoftwareSerial);

/****************************************************************
 * isPlaying
 *
 * Return actual player state
 ***************************************************************/
bool isPlaying(void)
{
	return (!digitalRead(busyPin));
}

/****************************************************************
 * voiceMenu
 *
 * Voice menu, overloaded function for playing voices for
 * configuration and informations.
 ***************************************************************/
int voiceMenu(int numberOfOptions, int startMessage, int messageOffset,
              bool preview = false, int previewFromFolder = 0)
{
    int returnValue = 0;
    if (startMessage != 0)
    {
        mp3.playMp3FolderTrack(startMessage);
    }

    while (true)
    {
    	/* Read button states */
        readButtons();

        mp3.loop();

        if (pauseButton.wasReleased())
		{
			if ((ignorePauseButton == false)
					&& (returnValue != 0))
			{
				return returnValue;
				delay(1000);
			}
			ignorePauseButton = false;
		}
		else if (pauseButton.pressedFor(LONG_PRESS)
					&& ignorePauseButton == false)
		{
			/* play advertisement also in card configuration setup mode */
			//TODO mark correct title first... modeHandler(PlayAdvertisement);
			ignorePauseButton = true;
		}

        if (upButton.pressedFor(LONG_PRESS))
        {
            returnValue = min(returnValue + 10, numberOfOptions);
            mp3.playMp3FolderTrack(messageOffset + returnValue);
            delay(1000);
            if (preview)
            {
            	while (isPlaying())
                {
                    delay(10);
                }

                if (previewFromFolder == 0)
                {
                    mp3.playFolderTrack(returnValue, 1);
                }
                else
                {
                    mp3.playFolderTrack(previewFromFolder, returnValue);
                }
            }
            ignoreUpButton = true;
        }
        else if (upButton.wasReleased())
        {
            if (!ignoreUpButton)
            {
                returnValue = min(returnValue + 1, numberOfOptions);
                mp3.playMp3FolderTrack(messageOffset + returnValue);
                delay(1000);
                if (preview)
                {
                	while (isPlaying())
                    {
                        delay(10);
                    }

                    if (previewFromFolder == 0)
                    {
                        mp3.playFolderTrack(returnValue, 1);
                    }
                    else
                    {
                        mp3.playFolderTrack(previewFromFolder, returnValue);
                    }
                }
            }
            else
            {
                ignoreUpButton = false;
            }
        }

        if (downButton.pressedFor(LONG_PRESS))
        {
            returnValue = max(returnValue - 10, 1);
            mp3.playMp3FolderTrack(messageOffset + returnValue);
            delay(1000);
            if (preview)
            {
            	while (isPlaying())
                {
                    delay(10);
                }

                if (previewFromFolder == 0)
                {
                    mp3.playFolderTrack(returnValue, 1);
                }
                else
                {
                    mp3.playFolderTrack(previewFromFolder, returnValue);
                }
            }
            ignoreDownButton = true;
        }
        else if (downButton.wasReleased())
        {
            if (!ignoreDownButton)
            {
                returnValue = max(returnValue - 1, 1);
                mp3.playMp3FolderTrack(messageOffset + returnValue);
                delay(1000);
                if (preview)
                {
                	while (isPlaying())
                    {
                        delay(10);
                    }

                    if (previewFromFolder == 0)
                    {
                        mp3.playFolderTrack(returnValue, 1);
                    }
                    else
                    {
                        mp3.playFolderTrack(previewFromFolder, returnValue);
                    }
                }
            }
            else
            {
                ignoreDownButton = false;
            }
        }
    }
}

/****************************************************************
 * setupCard
 *
 * Card setup function
 ***************************************************************/
void setupCard(void)
{
    mp3.pause();
    #if (DEBUG > 1)
    Serial.print(F("Neue Karte konfigurieren"));
    #endif

    // Ordner abfragen
    currentPlayerState = CardSetupFolderSelection;
    nfcTag.folder = voiceMenu(MAX_FOLDERS, AdvNewTag, 0, true);

    // Wiedergabemodus abfragen
    currentPlayerState = CardSetupModeSelection;
    nfcTag.mode = voiceMenu(6, Adv310, Adv310);

    // Hörbuchmodus -> Fortschritt im EEPROM auf 1 setzen
    currentPlayerState = CardSetupFileSelection;
    EEPROM.update(nfcTag.folder,1);

    // Einzelmodus -> Datei abfragen
    if (nfcTag.mode == EinzelModus)
    {
        nfcTag.special = voiceMenu(mp3.getFolderTrackCount(nfcTag.folder), AdvSelectFile, 0,
                                 true, nfcTag.folder);
    }

    // Admin Funktionen
    if (nfcTag.mode == AdminModus)
    {
        nfcTag.special = voiceMenu(3, AdvSelectFile, 320);
    }

    // Karte ist konfiguriert -> speichern
    writeCard();
    currentPlayerState = Init;
}

/****************************************************************
 * readCard
 *
 * Card read function
 ***************************************************************/
bool readCard(void)
{
    bool returnValue = true;
    // Show some details of the PICC (that is: the tag/card)
    #if (DEBUG > 2)
    Serial.print(F("Card UID:"));
    dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
    Serial.println();
    Serial.print(F("PICC type: "));
    MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    Serial.println(mfrc522.PICC_GetTypeName(piccType));
    #endif

    byte buffer[18];
    byte size = sizeof(buffer);

    // Authenticate using key A
    #if (DEBUG > 2)
    Serial.println(F("Authenticating using key A..."));
    #endif
    status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
    if (status != MFRC522::STATUS_OK)
    {
        returnValue = false;
        #if (DEBUG > 2)
        Serial.print(F("PCD_Authenticate() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
        #endif
        return returnValue;
    }

    // Show the whole sector as it currently is
    #if (DEBUG > 2)
    Serial.println(F("Current data in sector:"));
    #endif
    mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);
    #if (DEBUG > 2)
    Serial.println();
    #endif

    // Read data from the block
    #if (DEBUG > 2)
    Serial.print(F("Reading data from block "));
    Serial.print(blockAddr);
    Serial.println(F(" ..."));
    #endif
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(blockAddr, buffer, &size);
    if (status != MFRC522::STATUS_OK)
    {
        returnValue = false;
        #if (DEBUG > 2)
        Serial.print(F("MIFARE_Read() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
        #endif
    }
    #if (DEBUG > 2)
    Serial.print(F("Data in block "));
    Serial.print(blockAddr);
    Serial.println(F(":"));
    dump_byte_array(buffer, 16);
    Serial.println();
    Serial.println();
    #endif

    uint32_t tempCookie;
    tempCookie = (uint32_t)buffer[0] << 24;
    tempCookie += (uint32_t)buffer[1] << 16;
    tempCookie += (uint32_t)buffer[2] << 8;
    tempCookie += (uint32_t)buffer[3];

    nfcTag.cookie = tempCookie;
    nfcTag.version = buffer[4];
    nfcTag.folder = buffer[5];
    nfcTag.mode = buffer[6];
    nfcTag.special = buffer[7];

    return returnValue;
}

/****************************************************************
 * writeCard
 *
 * Card write function
 ***************************************************************/
void writeCard(void)
{
    byte buffer[16] = {0x13, 0x37, 0xb3, 0x47, // 0x1337 0xb347 magic cookie to
                                               // identify our nfc tags
                       0x01,                   // version 1
					   nfcTag.folder,          // the folder picked by the user
					   nfcTag.mode,    // the playback mode picked by the user
					   nfcTag.special, // track or function for admin cards
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // Authenticate using key B
    #if (DEBUG > 2)
    Serial.println(F("Authenticating again using key B..."));
    #endif
    status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_B, trailerBlock, &key, &(mfrc522.uid));
    if (status != MFRC522::STATUS_OK)
    {
        #if (DEBUG > 2)
        Serial.print(F("PCD_Authenticate() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
        #endif
        mp3.playMp3FolderTrack(AdvError);
        return;
    }

    // Write data to the block
    #if (DEBUG > 2)
    Serial.print(F("Writing data into block "));
    Serial.print(blockAddr);
    Serial.println(F(" ..."));
    #endif
    dump_byte_array(buffer, 16);
    Serial.println();
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(blockAddr, buffer, 16);
    if (status != MFRC522::STATUS_OK)
    {
        #if (DEBUG > 2)
        Serial.print(F("MIFARE_Write() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
        #endif
        mp3.playMp3FolderTrack(AdvError);
    }
    else
    {
        mp3.playMp3FolderTrack(Adv400);
    }
    Serial.println();
    delay(100);
}

/****************************************************************
 * initButtons
 *
 * Initialize buttons
 ***************************************************************/
void initButtons(void)
{
	pinMode(buttonPause, INPUT_PULLUP);
	pinMode(buttonUp, INPUT_PULLUP);
	pinMode(buttonDown, INPUT_PULLUP);
}
/****************************************************************
 * readButtons
 *
 * Read in button states
 ***************************************************************/
void readButtons(void)
{
	pauseButton.read();
	upButton.read();
	downButton.read();
}

/****************************************************************
 * handleButtons
 *
 * Handle buttons during runtime
 ***************************************************************/
void handleButtons(void)
{
	/* Pause Button handling */
	if (pauseButton.wasReleased())
	{
		if (ignorePauseButton == false)
		{
			modeHandler(StartPause);
		}
		ignorePauseButton = false;
	}
	else if (pauseButton.pressedFor(LONG_PRESS)
				&& ignorePauseButton == false)
	{
		if (currentPlayerState == Play)
		{
			modeHandler(PlayAdvertisement);
		}
		else
		{
			modeHandler(EraseCard);
		}
		ignorePauseButton = true;
	}

	/* Up Button Handling */
	if (upButton.pressedFor(LONG_PRESS))
	{
		if (ignoreUpButton == false)
		{
			/* volume up only in smaller steps than volume down */
			modeHandler(VolumeUp);
		}
		ignoreUpButton = true;
	}
	else if (upButton.wasReleased())
	{
		if (!ignoreUpButton)
		{
			modeHandler(PlayNextTrack);
		}
		else
		{
			ignoreUpButton = false;
		}
	}

	/* Down Button Handling */
	if (downButton.pressedFor(LONG_PRESS))
	{
		modeHandler(VolumeDown);
		ignoreDownButton = true;
	}
	else if (downButton.wasReleased())
	{
		if (!ignoreDownButton)
		{
			modeHandler(PlayPreviousTrack);
		}
		else
		{
			ignoreDownButton = false;
		}
	}
}

/****************************************************************
 * modeHandler
 *
 * Mode Handler - process actions
 ***************************************************************/
void modeHandler(uint8_t mode)
{
	ResetSwitchOffTimer();

	if (mode == NewCardDetected)
	{
		knownCard = true;
		_lastTrackFinished = 0;
		numTracksInFolder = mp3.getFolderTrackCount(nfcTag.folder);
		#if (DEBUG > 1)
		Serial.print(numTracksInFolder);
		Serial.print(F(" Dateien in Ordner "));
		Serial.println(nfcTag.folder);
		#endif
	}

	if ( (mode <= PlayPreviousTrack)
			&& (mode > Idle))
	{
		currentPlayerState = Play;

		switch(nfcTag.mode)
		{
			case Hoerspielmodus:
				if (mode == NewCardDetected)
				{
					currentTrack = random(1, numTracksInFolder + 1);
					#if (DEBUG > 1)
					Serial.println(F("Hoerspielmodus -> zufaelligen Track wiedergeben"));
					Serial.println(currentTrack);
					#endif
					mp3.playFolderTrack(nfcTag.folder, currentTrack);
				}
				else if (mode == PlayNextTrack)
				{
					#if (DEBUG > 1)
					Serial.println(F("Hoerspielmodus ist aktiv -> keinen neuen Track spielen"));
					#endif
					//    mp3.sleep(); // Je nach Modul kommt es nicht mehr zurück aus dem Sleep!
				}
				else if (mode == PlayPreviousTrack)
				{
					#if (DEBUG > 1)
					Serial.println(F("Hoerspielmodus ist aktiv -> Track von vorne spielen"));
					#endif
					mp3.playFolderTrack(nfcTag.folder, currentTrack);
				}
				else
				{

				}
				break;
			case Albummodus:
				currentPlayerState = Play;

				if (mode == NewCardDetected)
				{
					#if (DEBUG > 1)
					Serial.println(F("Album Modus -> kompletten Ordner wiedergeben"));
					#endif
					currentTrack = 1;
					mp3.playFolderTrack(nfcTag.folder, currentTrack);
				}
				else if (mode == PlayNextTrack)
				{
					if (currentTrack != numTracksInFolder)
					{
						currentTrack = currentTrack + 1;
						mp3.playFolderTrack(nfcTag.folder, currentTrack);
						#if (DEBUG > 1)
						Serial.print(F("Albummodus ist aktiv -> naechster Track: "));
						Serial.print(currentTrack);
						#endif
					}
					else
					{
						//mp3.sleep();   // Je nach Modul kommt es nicht mehr zurÃ¼ck aus dem Sleep!
						currentPlayerState = Stop;
					}
				}
				else if (mode == PlayPreviousTrack)
				{
					#if (DEBUG > 1)
					Serial.println(F("Albummodus ist aktiv -> vorheriger Track"));
					#endif
					if (currentTrack != 1)
					{
						currentTrack = currentTrack - 1;
					}
					mp3.playFolderTrack(nfcTag.folder, currentTrack);
				}
				else
				{

				}
				break;
			case PartyModus:
				currentPlayerState = Play;

				if (mode == NewCardDetected)
				{
					#if (DEBUG > 1)
					Serial.println(
						F("Party Modus -> Ordner in zufaelliger Reihenfolge wiedergeben"));
					#endif
					currentTrack = random(1, numTracksInFolder + 1);
				}
				else if (mode == PlayNextTrack)
				{
					currentTrack = random(1, numTracksInFolder + 1);
					#if (DEBUG > 1)
					Serial.print(F("Party Modus ist aktiv -> zufaelligen Track spielen: "));
					Serial.println(currentTrack);
					#endif
				}
				else if (mode == PlayPreviousTrack)
				{
					#if (DEBUG > 1)
					Serial.println(F("Party Modus ist aktiv -> Track von vorne spielen"));
					#endif
				}
				else
				{

				}
				mp3.playFolderTrack(nfcTag.folder, currentTrack);
				break;
			case EinzelModus:
				if (mode == NewCardDetected)
				{
					#if (DEBUG > 1)
					Serial.println(
						F("Einzel Modus -> eine Datei aus dem Ordner abspielen"));
					#endif
					currentTrack = nfcTag.special;
					mp3.playFolderTrack(nfcTag.folder, currentTrack);
				}
				else if (mode == PlayNextTrack)
				{
					#if (DEBUG > 1)
					Serial.println(F("Einzel Modus aktiv -> Strom sparen"));
					#endif
					//    mp3.sleep();      // Je nach Modul kommt es nicht mehr zurÃ¼ck aus dem Sleep!
				}
				else if (mode == PlayPreviousTrack)
				{
					#if (DEBUG > 1)
					Serial.println(F("Einzel Modus aktiv -> Track von vorne spielen"));
					#endif
					mp3.playFolderTrack(nfcTag.folder, currentTrack);
				}
				else
				{

				}
				break;
			case HoerbuchModusSave:
				currentPlayerState = Play;

				if (mode == NewCardDetected)
				{
					#if (DEBUG > 1)
					Serial.println(F("Hoerbuch Modus -> kompletten Ordner spielen und "
									 "Fortschritt merken"));
					#endif
					currentTrack = EEPROM.read(nfcTag.folder);
					mp3.playFolderTrack(nfcTag.folder, currentTrack);
				}
				else if (mode == PlayNextTrack)
				{
					if (currentTrack != numTracksInFolder)
					{
						currentTrack = currentTrack + 1;
						#if (DEBUG > 1)
						Serial.print(F("Hoerbuch Modus ist aktiv -> naechster Track und "
									   "Fortschritt speichern"));
						Serial.println(currentTrack);
						#endif
						mp3.playFolderTrack(nfcTag.folder, currentTrack);
						// Fortschritt im EEPROM abspeichern
						EEPROM.update(nfcTag.folder, currentTrack);
					}
					else
					{
						// mp3.sleep();  // Je nach Modul kommt es nicht mehr zurück aus dem Sleep!
						// Fortschritt zurück setzen
						EEPROM.update(nfcTag.folder, 1);
						currentPlayerState = Stop;
					}
				}
				else if (mode == PlayPreviousTrack)
				{
					#if (DEBUG > 1)
					Serial.println(F("Hoerbuch Modus ist aktiv -> vorheriger Track und "
									 "Fortschritt speichern"));
					#endif
					if (currentTrack != 1)
					{
						currentTrack = currentTrack - 1;
					}
					mp3.playFolderTrack(nfcTag.folder, currentTrack);
					// Fortschritt im EEPROM abspeichern
					EEPROM.update(nfcTag.folder, currentTrack);
				}
				else
				{

				}
				break;
			default:
				break;
		}
	}
	else if (mode <= VolumeDown)
	{
		if (mode == VolumeUp)
		{
			#if (DEBUG > 1)
			Serial.println(F("Volume Up"));
			#endif
			if (mp3.getVolume() < MAX_VOLUME)
			{
				  mp3.increaseVolume();
			}
			else
			{
				#if (DEBUG > 1)
				Serial.println(F("Maximal erlaubte Lautstaerke erreicht!"));
				Serial.println(mp3.getVolume());
				#endif
			}
		}
		else
		{
			#if (DEBUG > 1)
			Serial.println(F("Volume Down"));
			#endif
			mp3.decreaseVolume();
		}
	}
	else if (mode == StartPause)
	{
		if (isPlaying())
		{
			mp3.pause();
			currentPlayerState = Pause;
		}
		else
		{
			mp3.start();
			currentPlayerState = Play;
		}
	}
	else if (mode == PlayAdvertisement)
	{
		mp3.playAdvertisement(currentTrack);
	}
	else if (mode <= SetupCard)
	{
		//knownCard = false;
		currentPlayerState = Init;

		if (mode == EraseCard)
		{
			/* Set volume to standard level */
			mp3.setVolume(INITIAL_VOLUME);

			mp3.playMp3FolderTrack(AdvResetTag);
			#if (DEBUG > 1)
			Serial.println(F("Karte resetten..."));
			#endif
			while (!mfrc522.PICC_IsNewCardPresent())
			{
				readButtons();

				if (upButton.wasReleased()
						|| downButton.wasReleased())
				{
					#if (DEBUG > 1)
					Serial.print(F("Abgebrochen!"));
					#endif
					mp3.playMp3FolderTrack(AdvResetAborted);
					return;
				}
			}

			if (!mfrc522.PICC_ReadCardSerial())
			{
				return;
			}
			#if (DEBUG > 1)
			Serial.print(F("Karte wird neu Konfiguriert!"));
			#endif
			setupCard();
			mfrc522.PICC_HaltA();
			mfrc522.PCD_StopCrypto1();
		}
		else
		{
			setupCard();
		}
	}
	else
	{

	}

	#if (DEBUG > 0)
	Serial.print(F("Aktueller Status: "));
	Serial.println(currentPlayerState);
	#endif
}


/****************************************************************
 * initDFPlayer
 *
 * Initialize DFPlayer
 ***************************************************************/
void initDFPlayer(void)
{
	/* Initialize Busy Pin */
	pinMode(busyPin, INPUT);

	/* Initialize DFPlayer Mini */
	mp3.begin();
	mp3.setVolume(INITIAL_VOLUME);
}

/****************************************************************
 * initDFPlayer
 *
 * Initialize DFPlayer
 ***************************************************************/
void initCardReader(void)
{
	/* Init SPI */
	SPI.begin();
	/* Init MFRC522 */
    mfrc522.PCD_Init();
	#if (DEBUG > 2)
    Serial.println(F("MFRC522 Card Reader Version:"));
    mfrc522.PCD_DumpVersionToSerial(); /* Show details of PCD - MFRC522 Card Reader */
	#endif
    for (byte i = 0; i < 6; i++)
    {
        key.keyByte[i] = 0xFF;
    }
}

#if (POWER_SWITCH == 1)
/****************************************************************
 * InitPowerSwitch
 *
 * Initialize Power Switch functionality
 ***************************************************************/
void InitPowerSwitch(void)
{
	/* Initialize output for power off functionality */
	pinMode(powerSwitchPin,OUTPUT);
	digitalWrite(powerSwitchPin, false);
}

/****************************************************************
 * InitPowerSwitch
 *
 * Process Power Switch functionality
 * Power switch shall be disabled in the case no action from
 * user was performed for a longer time
 ***************************************************************/
void ProcessPowerSwitch(void)
{
	uint32_t currentTime;	/* current time in milliseconds */

	bool switchOff = false;

	secondsRunning = (uint32_t)(millis() / 1000);

	if ((uint32_t)secondsRunning > (uint32_t)(lastTime + switchOffTimer))
	{
		/* increase switch off timer every second */
		switchOffTimer++;
		#if (DEBUG > 1)
		if ((switchOffTimer % 30) == false)
		{
			Serial.print(F("Switch Off timer reached (seconds): "));
			Serial.println(switchOffTimer);
			Serial.print(F("Current Time (seconds): "));
			Serial.println(secondsRunning);
			Serial.print(F("Last action at (seconds): "));
			Serial.println(lastTime);
		}
		#endif
	}

	switch(currentPlayerState)
	{
		case Play:
			if (secondsRunning >= MAX_ON_TIME)
			{
				currentPlayerState = ReadyForShutdown;
				switchOff = true;
			}
			else
			{

			}
			break;
		case CardSetupFolderSelection: /* during learning a card, timeout to power off should be higher */
		case CardSetupFileSelection:
		case CardSetupModeSelection:
		case Pause:
			if (switchOffTimer == ON_TIME_AFTER_PAUSE)
			{
				currentPlayerState = ReadyForShutdown;
				switchOff = true;
			}
			else
			{

			}
			break;
		case Stop:
		default: /* e.g. Init */
			if ((switchOffTimer - lastTime) == ON_TIME_AFTER_STOP)
			{
				currentPlayerState = ReadyForShutdown;
				switchOff = true;
			}
			else
			{

			}
			break;
	}

	if (switchOff == true)
	{
		#if (DEBUG > 1)
		Serial.print(F("Aktueller Status: "));
		Serial.println(currentPlayerState);
		Serial.println(F("Switching off Tonuino now"));
		#endif
		digitalWrite(powerSwitchPin, true);
	}
}

/****************************************************************
 * ResetSwitchOffTimer
 *
 * Resets the switch off timer
 ***************************************************************/
void ResetSwitchOffTimer(void)
{
	switchOffTimer = 0;
	lastTime = secondsRunning;
}
#endif

/****************************************************************
 * Setup function
 *
 * Initialize Tonuino
 ***************************************************************/
void setup()
{
    #if (DEBUG > 0)
	/* Initialize debug interface */
    Serial.begin(115200);
    /* Write some informations to console */
    Serial.println(F("TonUINO Version 2.0"));
    Serial.println(F("(c) Thorsten Voß"));
    Serial.println(F("Modified by Christian Hermes v1.2"));
    #endif

    /* Initialize random seed */
    randomSeed(analogRead(A0));

	#if (POWER_SWITCH == 1)
    InitPowerSwitch();
    #endif

    /* Initialize buttons */
    initButtons();

    /* Initialize DFPlayer */
    initDFPlayer();

    /* Initialize Card Reader */
    initCardReader();

    /*
     * Special startup handling for resetting EEPROM contents
     * Press all buttons at startup to reset EEPROM
     */
    if (digitalRead(buttonPause) == LOW
        && digitalRead(buttonUp) == LOW
        && digitalRead(buttonDown) == LOW)
    {
        #if (DEBUG > 0)
        Serial.println(F("Reset -> EEPROM wird gelöscht"));
        #endif
        for (uint16_t i = 0; i < (uint16_t)EEPROM.length(); i++)
        {
            EEPROM.update(i, 0);
        }
    }

    currentPlayerState = Init;
}

/****************************************************************
 * Main function
 *
 * Endless loop
 ***************************************************************/
void loop()
{
	/* until no new card detected, play card content */
	while (!mfrc522.PICC_IsNewCardPresent())
    {
        mp3.loop();

        /* Read button states */
        readButtons();

        /* Process Button states */
        handleButtons();

		#if (POWER_SWITCH == 1)
        /* Process power switch */
        ProcessPowerSwitch();
		#endif
    }

    /* Check for new RFID Card */
    if (!mfrc522.PICC_ReadCardSerial())
    {
        return;
    }

    successRead = (bool) readCard();

    if (successRead == true)
    {
        if ((nfcTag.cookie == 322417479)
        		&& (nfcTag.folder != 0)
				&& (nfcTag.mode != 0))
        {
            modeHandler(NewCardDetected);
        }
        else
        {
        	/* Configure new card */
            modeHandler(SetupCard);
        }
    }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

	#if (POWER_SWITCH == 1)
	/* Process power switch */
	ProcessPowerSwitch();
	#endif
}


/****************************************************************
 * dump_byte_array
 *
 * Helper routine to dump a byte array as hex values to Serial.
 ***************************************************************/
void dump_byte_array(byte *buffer, byte bufferSize)
{
    for (byte i = 0; i < bufferSize; i++)
    {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
    }
}

