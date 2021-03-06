/* MagOD version 2.2 */
/* Aug 2019 */
/* Tijmen Hageman, Jordi Hendrix, Hans Keizer, Leon Abelmann */
/* Based on original code, modified by Leon for readablity and ease of recipe change */

/* Class definitions */
#include "MagOD.h"

/* Version of electronics is defined in MagOD.h */
#if defined(_MAGOD1)
#warning "MAGOD1"
#elif defined(_MAGOD2)
#warning "MAGOD2"
#else
#error "No version defined"
#endif

#if (defined(_MAGOD1) && defined(ARDUINO_AVR_MEGA2560)) || \
    (defined(_MAGOD2) && defined(ARDUINO_ESP32_DEV))


/* ############## Global variables ########### */

/* Update frequencies */
float freq_meas = 7; //Measurement frequency in Hz
// For Arduion: tested up to 7 Hz. Can be 20 Hz without saving data. Can be 30 Hz if we optimize doMeasurement. Can be 100 Hz if we only read one adc. Can be 700 Hz theoretically...

float freq_screen = 2; //Screen update frequency in Hz

/* Program menu settings */
uint16_t program_cnt = 1; //Current program menu, default 1 
const uint16_t program_nmb = 1;//Total number of menus. Right now, there is only one measurement program. When you want more, make sure there is a good menu selection procedure. The joystick on the 1.8" screen suckcs...

/* State variables (global) used in this program */
bool doMeasurementFlag = false; //Read the ADC inputs
bool screenUpdateFlag  = false; //Update the screen
bool Updatecurrentflag = false; //Do a current calibration
bool Exit_program_1    = HIGH;  //The program should end

/* Status parameters */
bool isRecording       = false; //We are measuring
bool SDpresent         = false; //There is a readable SD card in the slot
uint8_t prevButton     = 0;     //Status of the button


/* Control parameters */
bool sendToSerial      = 0;     /*Do we send files during the
				   measurement over serial port? */
bool save_extra_parameter = 0;  /*set this to 1 when you want to store
				  an extra parameter in the header of
				  the .CVS file (this can be any
				  parameter which makes it easier for
				  dataprocessing, for instance which
				  fields are used) */
double extra_par = 0.0;         /*value of this extra parameter
				   (specifiy this in the
				   initialization function) */

/* Init classes */
adc myadc;
screen myscreen;
timer mytimer;
led myled;
buttons mybuttons;
field myfield;
fileandserial myfile;
recipes myrecipes;


/* Should this be here? LEON*/
#if defined(_MAGOD2)
hw_timer_t * timer1 = NULL;
hw_timer_t * timer3 = NULL;
hw_timer_t * timer4 = NULL;
#endif

/* The measured parameters */
diodes Vdiodes = {0,0,0};       /* Photodetector signals [V] */
double Temperature_degrees = 0; /*Temperature estimated from
				  temperature sensor*/
references Vrefs = {1,1,1,1};   /* Reference values of photodector
				  signals. Current value and three
				  different colors. Initialize to 1 to
				  get meaningful OD */
feedbacks Vfb = {0,0,0};        /* Current feedback loop voltages */

/* Calculated parameters */
double OD = 0;                 /*Optical Density. Calculated in
				 CaldOD() */

/* Time parameters */
unsigned long time_of_data_point = 0; /*Store time when datapoint was
					taken*/
unsigned long time_of_start = 0;      /*Time at which the measurement was
					started*/
unsigned long time_last_field_change = 0; /*Time since the
					    last field step */

/* LED parameters */
int LED_type = GREEN; //The color of the LED
int LEDs[3] = {RED, GREEN, BLUE};
#if defined(_MAGOD2)
int LED_intensity[3] = {15,65,95};//Values that don't saturate the photodiode
#endif
int LED_switch_cycles = 0; //The number of cycles after which the LED changes the frequency, when a 0 is entered, the LED keeps the beginning frequency during the complete measurement 
int Counter_cycles_led = 1; //counter used to store the amount of complete cycles the LED has had the same colour, to check when the colour has to change (after LED_switch_cycles)
bool ref_all_wavelength = 0; //Set this to 1 for specific programs where you work with multiple wavelengths in a single measurement (such that it stores the reference value of all 3 wavelengths

/* Declare variables to define the field sequence */
unsigned int B_nr_set = 1; //the number of elements in the array
long Nr_cycles = 0; //The number of cycles throught the array, a value of 0 means an infinite amound of cycles
unsigned int Looppar_1 = 0; //Looppar_1,2 track at which point of the field-array the program is
unsigned int Looppar_2 = 0;

/* Note, the definitions don't seem to work. Only the first element in the array is set to that value. Instead, I initialize the arrays in recipes.cpp */
double B_arrayfield_x[B_NR_MAX] = {0.0}; //an array containing B_Nr_set elements for the field in the x-direction, each element has to be an integer between -256 and 256 and negative numbers can be used for opposite directions
double B_arrayfield_y[B_NR_MAX] = {0.0}; //Same for y
double B_arrayfield_z[B_NR_MAX] = {0.0}; //Same for z
unsigned long Switching_time[B_NR_MAX] ={1000};

bool Gradient_x[B_NR_MAX]={1};  //determines whether both coils must be on or just one of them for the x-direction, 0 is both on, 1 is only one. When the set of coils is connected, set to 1. Note that MagOD2 no longer has relays to do this automatically, you need to manually route the currents through one coil only.
bool Gradient_y[B_NR_MAX] = {1};  //same for y
bool Gradient_z[B_NR_MAX] = {1};  //same for z


/*** Top level functions ***/

/** Timer triggers **/

/* Timer 1 triggers a measurement */
void measEvent()
{
  doMeasurementFlag=true;
}

/* Timer 3 triggers a field correction event */
void Updatecurrent()
{
  Updatecurrentflag=myfield.CheckUpdatedCurrentTime();
  Updatecurrentflag = false;
}

/* Timer 4 triggers a screenupdate */
void screenUpdateEvent()
{
  screenUpdateFlag=true;
}

// Calculate in which part of the B-field array the program currently is, when the program has a specified number of iterations (0 is infinite) this part will ensure that the program stops after that number.
//the program also calculates when to switch the wavelength of the lightsource when this is wanted
void SetBfield_array()
{
  Looppar_1++; //parameter of which element in the B-field array the program currently is
  if (Looppar_1 >= (B_nr_set)) //when larger than total number of values in the array, gos back to first value
    {
      Looppar_1 = 0;
      //this part is responsible for changing the LED color after the predefined number of cycles
      if ((Counter_cycles_led >= LED_switch_cycles) && (LED_switch_cycles != 0))
        {
          Counter_cycles_led = 1;
          LED_type++;
          if (LED_type >= 4)
            {
              LED_type = GREEN;
            }
#if defined(_MAGOD1)
          myled.Set_LED_color(LED_type);
#elif defined(_MAGOD2)
	  myled.Set_LED_color(LED_type,LED_intensity);
#endif
        }
      else if (LED_switch_cycles != 0)
        {
          Counter_cycles_led++;
        }
      
      Looppar_2++; // Counts number of times a cycle has completed
      //check whether the program should end if Nr_cylces is set:
      if (Nr_cycles != 0)
      {
        if (Looppar_2 >= Nr_cycles)
        {      
           //stop the recording
           stopRec();  
           
        }
      }
  }
  //Sets the field whenever the program should be running and stores the time at which the field is changed (for the waiting period of the current sensing)
  if (Exit_program_1 == LOW){
    myfield.SetBfieldFast(B_arrayfield_x[Looppar_1],
			  B_arrayfield_y[Looppar_1],
			  B_arrayfield_z[Looppar_1],
			  Gradient_x[Looppar_1],
			  Gradient_y[Looppar_1],
			  Gradient_z[Looppar_1]);
  }
}
 

void startRec()
//Start the measurement
{
    //checks whether SD card is present
  if (!SD.begin(SD_CS)) {
    //If card not present, continue without using
    strlcpy(myfile.fName_char,"NO SD CARD",myfile.fN_len);
    myscreen.updateFILE(myfile.fName_char);
    SDpresent = false;
  }
  else
  {
    strlcpy(myfile.fName_char,"SD CARD READY",myfile.fN_len);
    myscreen.updateFILE(myfile.fName_char);
    // Check if card is actually working
    
    SDpresent = true;
  }

  //only starts the program when an SD card is present
  if (SDpresent == true)
  //if(false) //For debugging when there is no card
  {
    //Initialize program
    Serial.println("Initializing ");
    delay(1000);
    Exit_program_1 = LOW;
    Serial.print("program, ");
    myrecipes.program_init();
    Serial.print("datafile and ");
    myfile.file_init(Vrefs, ref_all_wavelength,
		     save_extra_parameter, extra_par, program_cnt,
		     myscreen);
    Serial.print("current feedback. ");
    myfield.Init_current_feedback();
    Serial.println("Done");

    //start coil actuation
    Serial.println("Setting magnetic field");
    myfield.SetBfieldFast(B_arrayfield_x[Looppar_1],
			  B_arrayfield_y[Looppar_1],
			  B_arrayfield_z[Looppar_1],
			  Gradient_x[Looppar_1],
			  Gradient_y[Looppar_1],
			  Gradient_z[Looppar_1]);
    
  
    //Update display
    Serial.println("Updating display");
    myscreen.updateFILE(myfile.fName_char);
    myscreen.setRecButton(true);
  
    //Note starting time
    time_of_start = millis();
    Serial.print("Started measurement at:  ");
    Serial.println(time_of_start);
    
    //Activate recording mode
    isRecording = true;
  }
}


void stopRec()
//stop the measurement
{
  Exit_program_1 = HIGH;
  myscreen.setRecButton(false);
  strlcpy(myfile.fName_char,"Rec finished",myfile.fN_len);
  myscreen.updateFILE(myfile.fName_char);
  // reset globals
  Looppar_2 = 0;
  Looppar_1 = 0;
  // Reset file counters
  myfile.file_reset();
  // Switch off field
  myfield.Reset_Bfield();
  Serial.println("Stop rec");  
  //De-activate recording mode
  isRecording = false;
}

//analyse the button that is pressed and take the wanted action
void processButtonPress()
{
  //Check for new button press
  uint8_t buttonPress = mybuttons.readButton();  
  if (buttonPress!=prevButton)
    {
      //Start/stop measurement
      if (buttonPress==BUTTON_SELECT){
	if(!isRecording){
	  Serial.println("Start rec");
	  startRec();
	}
	else{       
	  stopRec();
	}
	delay(1000);//Give user time to release the button
      }
      
    //Set reference voltage
      if (buttonPress==BUTTON_LEFT ){
	if (!isRecording){
	  myadc.set_vrefs(Vrefs,ref_all_wavelength,myled);
	  // ToDo: Perhaps not here, Why don't we give set_Vref the current led color?
	  myrecipes.LED_init();
#if defined(_MAGOD1)
          myled.Set_LED_color(LED_type);
#elif defined(_MAGOD2)
	  myled.Set_LED_color(LED_type,LED_intensity);
#endif
        }
        //Update screen
        //TODO, make a screen where all refs are shown in case of 3 colour
        myscreen.updateV(Vdiodes, Vrefs, OD, Vfb);
#if defined(_MAGOD2)
	/* This should be a function in screen.cpp. Every button could
	   have 2-3 states (Start, Starting..., Stop), (Set Vrefs,
	   Setting...) */
	mybuttons.showButtonArea(2, (char *)"Set Vref",
		       TFTCOLOR_DARKGRAY, TFTCOLOR_YELLOW);
#endif
      }
    }
  
  // change between the programs
  if (buttonPress==BUTTON_RIGHT ){
    if (!isRecording){
      if (!SD.begin(SD_CS)) {
	//If card not present, continue without using
	strlcpy(myfile.fName_char,"NO SD CARD",myfile.fN_len);
	myscreen.updateFILE(myfile.fName_char);
	SDpresent = false;
      }
      else
        {
	strlcpy(myfile.fName_char,"SD CARD READY",myfile.fN_len);
	myscreen.updateFILE(myfile.fName_char);
	SDpresent = true;
        }
      
      program_cnt++;
      if (program_cnt>program_nmb){
	program_cnt=1;
      }
      myrecipes.LED_init();  //immediatelty change the LED color to led it stabilize
#if defined(_MAGOD1)
      myled.Set_LED_color(LED_type);
#elif defined(_MAGOD2)
      myled.Set_LED_color(LED_type,LED_intensity);
#endif      
      //Update display
      myscreen.updateInfo(Looppar_1, Looppar_2, program_cnt,
			  myfile.fName_char);
    }
    prevButton = buttonPress;
  } //end if buttonPress!=prevButton
}


//calculate the optical density value, whenever using three wavelengths, the right reference has to be chosen
double calcOD(struct references Vrefs, double Vdiode)
{
#if defined(_MAGOD1)
  int detector=1; // MagOD1 has a detector that has higher signal for higher intensity
#elif defined(_MAGOD2)
  int detector=-1; //MagOD2 has a detector that has lower signal for higher intensity
#endif

  if(Vdiode<=0){return 0;}    //Vdiode has to be positive

  
  /* I don't understand why we simply do not calculate OD for LED_type. Why the if statement? TODO Leon */
  if (ref_all_wavelength == 0)
  {
    if(Vrefs.Vref<=0)
      {
	return 0; //Vref has to be positive
      }
    else {
      return detector*log10(Vrefs.Vref/Vdiode);  //otherwise, return correct OD value
    }
  }
  else
  {
    switch (LED_type){
    case RED:
      if (Vrefs.Vred <= 0)
	{
	  return 0; //Vref has to be positive
	}
      else
	{
	  return detector*log10(Vrefs.Vred/Vdiode);
	}
      break;
    case GREEN:
      if (Vrefs.Vgreen <= 0)
	{
	  return 0; //Vref has to be positive
	}
      else
	{
	  return detector*log10(Vrefs.Vgreen/Vdiode);
	}
      break;
    case BLUE:
      if (Vrefs.Vblue <= 0)
	{
	  return 0; //Vref has to be positive
	}
      else
	{
	  return detector*log10(Vrefs.Vblue/Vdiode);
	}
      break;
    default:
      return 0;
    } 
  }
}



//measures all the wanted values (photodiode voltage and temperature) and stores them on the SD-card if a measurement is started
void doMeasurement()
{
  /* Read the DACs to get the signals on the fotodiodes */
  Vdiodes=myadc.readDiodes();
  /* Calculate OD */
  OD = calcOD(Vrefs, Vdiodes.Vdiode);

  //Read these at lower speed. LEON.
  Temperature_degrees = myadc.readTemp();
  Vfb = myadc.readFeedbacks();
  
  //Save results, if we are recording
  if (isRecording)
  {
    //Record time
    time_of_data_point = millis()-time_of_start;

    /* This is all not very elegant. Needs major overhaul
       Writing to file and writing to serial should be separated
       The many small files is really akward as well. There must be
       better ways to avoid lengthly open/close sequences */
  
    //Save data to file and serial port, if SD card present
    if (SDpresent)
    {
      /* Write measurements to datafile*/
      myfile.saveToFile(myfile.fName_char,time_of_data_point,
			Vdiodes,Temperature_degrees,OD,
			LED_type,Looppar_1,Vfb);
      /* update file length */
      myfile.SD_file_length_count = myfile.SD_file_length_count + 1;

      // This does not work, why? I think it should be B_nr_set-1
      //      if ((SD_file_length_count > SD_file_length_count_max) && (Looppar_1 >= (B_nr_set))){
      if (myfile.SD_file_length_count > myfile.SD_file_length_count_max){
	/* reset file length counter */
	myfile.SD_file_length_count = 0;
	/* Send the file to the serial port */
	if (sendToSerial)
	  {
	    myfile.sendFileToSerial(myfile.fName_char);
	  }
	/* Get a new filename */
	myfile.updateFileName(myfile.fName_char);
	/* for this new file again all the headers are stored*/
	myfile.writeHeader(myfile.fName_char);

	//Update display
	myscreen.updateFILE(myfile.fName_char);

      } //if (SD_file_length_count > SD_file_length_count_max)
    } //if (SDpresent)
  } // if (Isrecording)
  //writeDataLine(Serial); for debug
}



//Initialization of all the timers used in the program
void Init_timers()
{
  //setup timer 1 for measurement
  mytimer.initTimer(1,long(1000000/freq_meas));
  mytimer.attachInterrupt(1, measEvent);
  
  //Setup timer 3 for current sensing
  mytimer.initTimer(3,1000000);
  mytimer.attachInterrupt(3, Updatecurrent);
  
  //Setup timer 4 for screen updates
  mytimer.initTimer(4,long(1000000/freq_screen));
  mytimer.attachInterrupt(4, screenUpdateEvent);
}

void setup()
{ delay(1000);//Give serial monitor time
/* ######################### Initialize boards ########################### */
  // A lot of this should be done in the libraries. LEON. TODO.
  //starts the serial connection for debugging and file transfer
  Serial.begin(115200);
  Serial.println("Initializing all");
  //initialize timers 5, to not affect other timers
  #if defined(_MAGOD1)
  mytimer.initTimer(5, 100000);
  #endif  

  // Initialize field control
  Serial.println("Init field");
  myfield.Init_field();/*Sets the pins in the correct status*/
  myfield.Reset_Bfield();
 
  // Init led
  Serial.println("Init led");
  myrecipes.LED_init();
#if defined(_MAGOD1)
  myled.Set_LED_color(LED_type);
#elif defined(_MAGOD2)
  myled.Set_LED_color(LED_type,LED_intensity);
#endif
  
  //Initialize ADC(s)
  Serial.println("Init ADC");
  myadc.initADC();

  //setup the screen
  Serial.println("Initializing screen...");
  myscreen.setupScreen();
  delay(1000);

  //Setup the buttons or touchscreen
  mybuttons.initButton();


  // Setup the SD Card
  // see if the card is present and can be initialized
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card not found");
    strlcpy(myfile.fName_char,"NO SD CARD",myfile.fN_len);
    myscreen.updateFILE(myfile.fName_char);
    delay(1000);
#if defined(_MAGOD1)
    // More testing, works only for MagOD1 because card is defined.
    Serial.println("Retrying");
    if (!myfile.card.init(SPI_HALF_SPEED, SD_CS)) {
	Serial.println("card initialization failed");
	//If card not present, continue without using 
	strlcpy(myfile.fName_char,"NO SD CARD",myfile.fN_len);
	myscreen.updateFILE(myfile.fName_char);
	SDpresent = false;
      }
    else {
      if (!SD.begin(SD_CS)) {
	  Serial.println("card still not working");
	  //If card not present, continue without using 
	  strlcpy(myfile.fName_char,"NO SD CARD",myfile.fN_len);
	  myscreen.updateFILE(myfile.fName_char);
	  SDpresent = false;
	}
      else {
	Serial.println("ok!");
	strlcpy(myfile.fName_char,"SD CARD READY",myfile.fN_len);
	myscreen.updateFILE(myfile.fName_char);
	SDpresent = true;
      }
    }
#endif //defined(MAGOD1)
#if defined(_MAGOD2) // Card is not defined for EPS32
    Serial.println("card initialization failed");
    strlcpy(myfile.fName_char,"NO SD CARD",myfile.fN_len);
    myscreen.updateFILE(myfile.fName_char);
#endif
  }
  else
  {
    Serial.println("SD Card Ready");
    strlcpy(myfile.fName_char,"NO SD CARD",myfile.fN_len);
    myscreen.updateFILE(myfile.fName_char);
    SDpresent = true;

#if defined(_MAGOD2)
    uint8_t cardType = SD.cardType();

    if(cardType == CARD_NONE){
        Serial.println("Card Type not recognized");
	strlcpy(myfile.fName_char,"NO SD CARD",myfile.fN_len);
	myscreen.updateFILE(myfile.fName_char);
        return;
    }

    Serial.print("SD Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD %lluMB\n", cardSize);
    /* It would be nice to print the filesize on the screen. LEON */
    strlcpy(myfile.fName_char,"SD CARD READY",myfile.fN_len);
    myscreen.updateFILE(myfile.fName_char);

    /* Test if file can actually be opened for writing */
    File dataFile = SD.open("/test.txt", FILE_WRITE);
    if(!dataFile){
        Serial.println("Failed to open file for writing");
        return;
    }
    if(dataFile.print("Hello world")){
        Serial.println("test.txt written");
    } else {
        Serial.println("Write test.txt failed");
	strlcpy(myfile.fName_char,"FILE ERROR",myfile.fN_len);
    }
    dataFile.close();
    
#endif //Defined MAGOD2
  }

  
  Serial.println("Updating screen.");
  myscreen.updateInfo(Looppar_1, Looppar_2, program_cnt," ");
  
  Serial.println("Intializing current feedback");
  myfield.Init_current_feedback();
  Init_timers();
  Looppar_1 = 0;
  Looppar_2 = 0;
  //sets the initial field to start the measurement, depending on the program used
}

/* Main loop */
void loop()
{
  delay(1);
  /* Check for button pressed */
  processButtonPress();
  
  /* Perform measurement if timer1 has triggered flag */
  if(doMeasurementFlag)
    {
      doMeasurementFlag=false; // reset flag for next time
      doMeasurement();
    }

  /* For testing, Leon */
  //doMeasurementFlag=true;
  
  /* Update screen if timer4 has triggered flag */
  if(screenUpdateFlag)
    {
      screenUpdateFlag=false; // reset flag for next time
      myscreen.updateV(Vdiodes, Vrefs, OD, Vfb); //Update values
      myscreen.updateGraph(Vdiodes.Vdiode,LED_type); //Update graph
      myscreen.updateInfo(Looppar_1, Looppar_2, program_cnt,
      			  myfile.fName_char); //Update program status
    }
  
  /* Recalibrate current if timer 3 has set the flag */
  if(Updatecurrentflag == true)
    {
      //Reset flag
      Updatecurrentflag = false;
      //Perform current recalibration procedure. But only if the program has not exit yet and when the PWM value has not too recently changed
      if ((Exit_program_1 == LOW) && ((millis() - time_last_field_change) > myfield.Current_wait_time))
	{
	  myfield.Current_feedback();
	  Serial.println("Perform feedback on current");
	}
    }
  
  /* Check if it is time to go to the next step in the measurement loop */
  if (((millis() - time_last_field_change) >
       (Switching_time[Looppar_1]-1))
      && (Exit_program_1 == LOW))
    {
      time_last_field_change = millis();
      SetBfield_array();
      
      //prints for debugging
      Serial.print("The time of field change is: ");
      Serial.println(time_last_field_change);
      Serial.print("B_nr_set = ");Serial.println(B_nr_set);
      Serial.print("Looppar = ");Serial.println(Looppar_1);
      Serial.print("The magnetic field in the x-direction = ");
      Serial.println(B_arrayfield_x[Looppar_1]);
      Serial.print("The magnetic field in the y-direction = ");
      Serial.println(B_arrayfield_y[Looppar_1]);
      Serial.print("The magnetic field in the z-direction = ");
      Serial.println(B_arrayfield_z[Looppar_1]);
    }
}
//end of program

#else
 #error "Wrong board set in IDE! "
#endif
