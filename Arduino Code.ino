
#if defined(__AVR_ATmega2560__)
#error This code is not meant for Arduino MEGA or RUMBA boards.
#endif

#define MOTHERBOARD 1

#if MOTHERBOARD == 2
#define SHIELD_ADDRESS (0x60)
#endif

#define POLARGRAPH2  
#define VERBOSE         (0)


#define M1_PIN          (1)
#define M2_PIN          (2)

#define L_PIN           (3)
#define R_PIN           (5)

#define STEPPER_STEPS_PER_TURN    (400.0)

#define MICROSTEPPING_MULTIPLIER  (8.0)
#define STEPS_PER_TURN            (STEPPER_STEPS_PER_TURN*MICROSTEPPING_MULTIPLIER)


#define NUM_TOOLS  (6)

#define MAKELANGELO_HARDWARE_VERSION 2

#define SWITCH_HALF     (2000)

#define PEN_UP_ANGLE    (85.0)
#define PEN_DOWN_ANGLE  (15.0)
#define PEN_DELAY       (800)  

#define MAX_STEPS_S     (STEPS_PER_TURN*MAX_RPM/500.0)  

#define MAX_FEEDRATE    (2000.0)
#define MIN_FEEDRATE    (5.0) 

#define ARC_CW          (1)
#define ARC_CCW         (-1)
#define MM_PER_SEGMENT  (1)

#define BAUD            (57600)

#define MAX_BUF         (64)
#define SERVO_PIN1      (10)
#define SERVO_PIN2      (9)
#define SERVO_PIN       SERVO_PIN1  
#define TIMEOUT_OK      (1000)  
#define TIMEOUT_MOTORS  (10000)

#ifndef USE_SD_CARD
#define File int
#endif



#if MOTHERBOARD == 1
#define M1_ONESTEP(x)  m1.onestep(x)//,MICROSTEP)
#define M2_ONESTEP(x)  m2.onestep(x)//,MICROSTEP)
#endif
#if MOTHERBOARD == 2
#define M1_ONESTEP(x)  m1->onestep(x,MICROSTEP)
#define M2_ONESTEP(x)  m2->onestep(x,MICROSTEP)
#endif

#define EEPROM_VERSION    7                   
#define ADDR_VERSION      0                  
#define ADDR_UUID        (ADDR_VERSION+1)     
#define ADDR_PULLEY_DIA1 (ADDR_UUID+4)        
#define ADDR_PULLEY_DIA2 (ADDR_PULLEY_DIA1+4) 
#define ADDR_LEFT        (ADDR_PULLEY_DIA2+4)
#define ADDR_RIGHT       (ADDR_LEFT+4)      
#define ADDR_TOP         (ADDR_RIGHT+4)   
#define ADDR_BOTTOM      (ADDR_TOP+4)      
#define ADDR_INVL        (ADDR_BOTTOM+4)     
#define ADDR_INVR        (ADDR_INVL+1)      
#define ADDR_HOMEX       (ADDR_INVR+1)        
#define ADDR_HOMEY       (ADDR_HOMEX+4)       

#if MOTHERBOARD == 1
#include <SPI.h>  
#include "AFMotorDrawbot.h"
#endif

#if MOTHERBOARD == 2
#include <Wire.h>
#include "Adafruit_MotorShield/Adafruit_MotorShield.h"
#endif

#include <Servo.h>

#ifdef USE_SD_CARD
#include <SD.h>
#endif

#include <EEPROM.h>
#include <Arduino.h>  

#include "Vector3.h"

#if MOTHERBOARD == 1
static AF_Stepper m1((int)STEPPER_STEPS_PER_TURN, M1_PIN);
static AF_Stepper m2((int)STEPPER_STEPS_PER_TURN, M2_PIN);
#endif
#if MOTHERBOARD == 2
Adafruit_MotorShield AFMS0 = Adafruit_MotorShield(SHIELD_ADDRESS);
Adafruit_StepperMotor *m1;
Adafruit_StepperMotor *m2;
#endif

static Servo s1;

int robot_uid=0;

static float limit_top = 0;  
static float limit_bottom = 0; 
static float limit_right = 0;  
static float limit_left = 0;  

static float homeX=0;
static float homeY=0;

char m1d='L';
char m2d='R';

char m1i = 1;
char m2i = 1;


int M1_REEL_IN  = FORWARD;
int M1_REEL_OUT = BACKWARD;
int M2_REEL_IN  = BACKWARD;
int M2_REEL_OUT = FORWARD;


float pulleyDiameter = 4.0f/PI;  
float threadPerStep = 4.0f/STEPS_PER_TURN


static float posx;
static float posy;
static float posz;  
static float feed_rate=0;
static long step_delay;


static long laststep1, laststep2;

static char absolute_mode=1;  
static float mode_scale;   
static char mode_name[3];

static char serialBuffer[MAX_BUF+1];  
static int sofar;               
static long last_ready_time;    
static long last_cmd_time;      
static bool motors_engaged = false;

Vector3 tool_offset[NUM_TOOLS];
int current_tool=0;

long line_number;

void adjustPulleyDiameter(float diameter1) {
  pulleyDiameter = diameter1;
  float PULLEY_CIRC = pulleyDiameter*PI;  
  threadPerStep = PULLEY_CIRC/STEPS_PER_TURN;  

#if VERBOSE > 2
  Serial.print(F("PulleyDiameter = "));  Serial.println(pulleyDiameter,3);
  Serial.print(F("threadPerStep="));  Serial.println(threadPerStep,3);
#endif
}

float atan3(float dy,float dx) {
  float a=atan2(dy,dx);
  if(a<0) a=(PI*2.0)+a;
  return a;
}
char readSwitches() {
#ifdef USE_LIMIT_SWITCH
  // get the current switch state
  return ( (analogRead(L_PIN) < SWITCH_HALF) | (analogRead(R_PIN) < SWITCH_HALF) );
#else
  return 0;
#endif  // USE_LIMIT_SWITCH
}

void setFeedRate(float v) {
  if(feed_rate==v) return;
  feed_rate=v;

  if(v > MAX_FEEDRATE) v = MAX_FEEDRATE;
  if(v < MIN_FEEDRATE) v = MIN_FEEDRATE;

  step_delay = 1000000.0 / v;
#if MOTHERBOARD == 1
  m1.setSpeed(v);
  m2.setSpeed(v);
#endif
#if MOTHERBOARD == 2
  m1->setSpeed(v);
  m2->setSpeed(v);
#endif

#if VERBOSE > 1
  Serial.print(F("feed_rate="));  Serial.println(feed_rate);
  Serial.print(F("step_delay="));  Serial.println(step_delay);
#endif
}

void printFeedRate() {
  Serial.print(F("F"));
  Serial.println(feed_rate);
}


// Change pen state.
void setPenAngle(int pen_angle) {
  if(posz!=pen_angle) {
#if VERBOSE > 1
    Serial.print(F("pen_angle="));  Serial.println(pen_angle);
#endif
    posz=pen_angle;

    if(posz<PEN_DOWN_ANGLE) posz=PEN_DOWN_ANGLE;
    if(posz>PEN_UP_ANGLE  ) posz=PEN_UP_ANGLE;

    s1.write( (int)posz );
    delay(PEN_DELAY);
  }
}

void IK(float x, float y, long &l1, long &l2) {
#ifdef COREXY
  l1 = lround((x+y) / threadPerStep);
  l2 = lround((x-y) / threadPerStep);
#endif
#ifdef TRADITIONALXY
  l1 = lround((x) / threadPerStep);
  l2 = lround((y) / threadPerStep);
#endif
#ifdef POLARGRAPH2
  // find length to M1
  float dy = y - limit_top;
  float dx = x - limit_left;
  l1 = lround( sqrt(dx*dx+dy*dy) / threadPerStep );
  dx = limit_right - x;
  l2 = lround( sqrt(dx*dx+dy*dy) / threadPerStep );
#endif
}
void FK(float l1, float l2,float &x,float &y) {
#ifdef COREXY
  l1 *= threadPerStep;
  l2 *= threadPerStep;

  x = (float)( l1 + l2 ) / 2.0;
  y = x - (float)l2;
#endif
#ifdef TRADITIONALXY
  l1 *= threadPerStep;
  l2 *= threadPerStep;
  x = l1;
  y = l2;
#endif
#ifdef POLARGRAPH2
  float a = (float)l1 * threadPerStep;
  float b = (limit_right-limit_left);
  float c = (float)l2 * threadPerStep;
  float theta = ((a*a+b*b-c*c)/(2.0*a*b));
  x = theta * a + limit_left;
  y = limit_top - (sqrt( 1.0 - theta * theta ) * a);
#endif
}

void pause(long us) {
  delay(us/1000);
  delayMicroseconds(us%1000);
}

void line(float x,float y,float z) {
  motors_engaged = true;
  long l1,l2;
  IK(x,y,l1,l2);
  long d1 = l1 - laststep1;
  long d2 = l2 - laststep2;

  long ad1=abs(d1);
  long ad2=abs(d2);
  int dir1=d1<0?M1_REEL_IN:M1_REEL_OUT;
  int dir2=d2<0?M2_REEL_IN:M2_REEL_OUT;
  long over;
  long i;

  long ad = max(ad1,ad2);


  setPenAngle((int)z);

  // bresenham's line algorithm.
  if(ad1>ad2) {
    over = ad1/2;
    for(i=0;i<ad1;++i) {
      M1_ONESTEP(dir1);
      over+=ad2;
      if(over>ad1) {
        over-=ad1;
        M2_ONESTEP(dir2);
      }

      //if(i<accelerate_until) d--;
      //if(i>=decelerate_after) d++;
      pause(step_delay);
#ifdef USE_LIMIT_SWITCH
      if(readSwitches()) return;
#endif
    }
  } else {
    over = ad2/2;
    for(i=0;i<ad2;++i) {
      M2_ONESTEP(dir2);
      over+=ad1;
      if(over>ad2) {
        over-=ad2;
        M1_ONESTEP(dir1);
      }
      pause(step_delay);
#ifdef USE_LIMIT_SWITCH
      if(readSwitches()) return;
#endif
    }
  }

  laststep1=l1;
  laststep2=l2;
  // I hope this prevents rounding errors.  Small fractions of lines
  // over a long time could lead to lost steps and drawing problems.
  FK(l1,l2,posx,posy);
  posz=z;
}

void line_safe(float x,float y,float z) {
  // split up long lines to make them straighter?
  float dx=x-posx;
  float dy=y-posy;
  float dz=z-posz;

  float x0=posx;
  float y0=posy;
  float z0=posz;
  float a;

  float len=sqrt(dx*dx+dy*dy);
  // too long!
  long pieces=ceil(len / MM_PER_SEGMENT);

  for(long j=1;j<pieces;++j) {
    a=(float)j/(float)pieces;

    line(dx*a+x0,
         dy*a+y0,
         dz*a+z0);
  }
  line(x,y,z);
}

void arc(float cx,float cy,float x,float y,float z,char clockwise) {
  // get radius
  float dx = posx - cx;
  float dy = posy - cy;
  float sr=sqrt(dx*dx+dy*dy);

  // find angle of arc (sweep)
  float sa=atan3(dy,dx);
  float ea=atan3(y-cy,x-cx);
  float er=sqrt(dx*dx+dy*dy);
  
  float da=ea-sa;
  if(clockwise!=0 && da<0) ea+=2*PI;
  else if(clockwise==0 && da>0) sa+=2*PI;
  da=ea-sa;
  float dr = er-sr;

  // get length of arc
  // float circ=PI*2.0*radius;
  // float len=theta*circ/(PI*2.0);
  // simplifies to
  float len1 = abs(da) * sr;
  float len = sqrt( len1 * len1 + dr * dr );

  int i, segments = ceil( len / MM_PER_SEGMENT );

  float nx, ny, nz, angle3, scale;
  float a,r;
  for(i=0;i<=segments;++i) {
    // interpolate around the arc
    scale = ((float)i)/((float)segments);

    a = ( da * scale ) + sa;
    r = ( dr * scale ) + sr;
    
    nx = cx + cos(a) * r;
    ny = cy + sin(a) * r;
    nz = ( z - posz ) * scale + posz;
    // send it to the planner
    line(nx,ny,nz);
  }
}


void teleport(float x,float y) {
  posx=x;
  posy=y;

  // @TODO: posz?
  long L1,L2;
  IK(posx,posy,L1,L2);
  laststep1=L1;
  laststep2=L2;
}

void help() {
  Serial.print(F("\n\nHELLO WORLD! I AM DRAWBOT #"));
  Serial.println(robot_uid);
  sayVersionNumber();
  Serial.println(F("M100 - display this message"));
  Serial.println(F("M101 [Tx.xx] [Bx.xx] [Rx.xx] [Lx.xx]"));
  Serial.println(F("       - display/update board dimensions."));
  Serial.println(F("As well as the following G-codes (http://en.wikipedia.org/wiki/G-code):"));
  Serial.println(F("G00,G01,G02,G03,G04,G28,G90,G91,G92,M18,M114"));
}


void sayVersionNumber() {
  int versionNumber = loadVersion();
  
  Serial.print(F("Firmware v"));
  Serial.println(versionNumber,DEC);
}


// touch some limit switches, then go to the home position.
void findHome() {
#ifdef USE_LIMIT_SWITCH
  Serial.println(F("Homing..."));

  motors_engaged = true;

  if(readSwitches()) {
    Serial.println(F("** ERROR **"));
    Serial.println(F("Problem: Plotter is already touching switches."));
    Serial.println(F("Solution: Please unwind the strings a bit and try again."));
    return;
  }

  int home_step_delay=300;
  int safe_out=50;

  Serial.println(F("Find left..."));
  do {
    M1_ONESTEP(M1_REEL_IN );
    M2_ONESTEP(M2_REEL_OUT);
    delayMicroseconds(home_step_delay);
  } while(!readSwitches());
  laststep1=0;

  int i;
  for(i=0;i<safe_out;++i) {
    M1_ONESTEP(M1_REEL_OUT);
    delayMicroseconds(home_step_delay);
  }
  laststep1=safe_out;

  Serial.println(F("Find right..."));
  do {
    M1_ONESTEP(M1_REEL_OUT);
    M2_ONESTEP(M2_REEL_IN );
    delay(step_delay);
    laststep1++;
  } while(!readSwitches());
  laststep2=0;

  for(i=0;i<safe_out;++i) {
    M2_ONESTEP(M2_REEL_OUT);
    delay(step_delay);
  }
  laststep2=safe_out;

  Serial.println(F("Centering..."));
  line(homeX,homeY,posz);
#endif // USE_LIMIT_SWITCH
}


//------------------------------------------------------------------------------
void where() {
  Serial.print(F("X"));  Serial.print(posx);
  Serial.print(F(" Y"));  Serial.print(posy);
  Serial.print(F(" Z"));  Serial.print(posz);
  Serial.print(' ');  printFeedRate();
  Serial.print(F("\n"));
}


//------------------------------------------------------------------------------
void printConfig() {
  Serial.print(m1d);              Serial.print(F("="));
  Serial.print(limit_top);        Serial.print(F(","));
  Serial.print(limit_left);       Serial.print(F("\n"));
  Serial.print(m2d);              Serial.print(F("="));
  Serial.print(limit_top);        Serial.print(F(","));
  Serial.print(limit_right);      Serial.print(F("\n"));
  Serial.print(F("Bottom="));     Serial.println(limit_bottom);
  where();
}


//------------------------------------------------------------------------------
void EEPROM_writeLong(int ee, long value) {
  byte* p = (byte*)(void*)&value;
  for (int i = 0; i < sizeof(value); i++)
  EEPROM.write(ee++, *p++);
}


//------------------------------------------------------------------------------
float EEPROM_readLong(int ee) {
  long value = 0;
  byte* p = (byte*)(void*)&value;
  for (int i = 0; i < sizeof(value); i++)
  *p++ = EEPROM.read(ee++);
  return value;
}


//------------------------------------------------------------------------------
void saveUID() {
  Serial.println(F("Saving UID."));
  EEPROM_writeLong(ADDR_UUID,(long)robot_uid);
}


//------------------------------------------------------------------------------
void savePulleyDiameter() {
  EEPROM_writeLong(ADDR_PULLEY_DIA1,pulleyDiameter*10000);
  //EEPROM_writeLong(ADDR_PULLEY_DIA2,pulleyDiameter*10000);
}


//------------------------------------------------------------------------------
void saveDimensions() {
  Serial.println(F("Saving dimensions."));
  EEPROM_writeLong(ADDR_LEFT,limit_left*100);
  EEPROM_writeLong(ADDR_RIGHT,limit_right*100);
  EEPROM_writeLong(ADDR_TOP,limit_top*100);
  EEPROM_writeLong(ADDR_BOTTOM,limit_bottom*100);
  EEPROM_writeLong(ADDR_HOMEX,homeX*100);
  EEPROM_writeLong(ADDR_HOMEY,homeY*100);
}


//------------------------------------------------------------------------------
void loadDimensions() {
  limit_left   = (float)EEPROM_readLong(ADDR_LEFT)/100.0f;
  limit_right  = (float)EEPROM_readLong(ADDR_RIGHT)/100.0f;
  limit_top    = (float)EEPROM_readLong(ADDR_TOP)/100.0f;
  limit_bottom = (float)EEPROM_readLong(ADDR_BOTTOM)/100.0f;
}


//------------------------------------------------------------------------------
void saveHome() {
  Serial.println(F("Saving home."));
  EEPROM_writeLong(ADDR_HOMEX,homeX*100);
  EEPROM_writeLong(ADDR_HOMEY,homeY*100);
}


void loadHome() {
  Serial.print(F("Load home."));
  homeX = (float)EEPROM_readLong(ADDR_HOMEX)/100.0f;
  homeY = (float)EEPROM_readLong(ADDR_HOMEY)/100.0f;
  Serial.print(F(" x="));  Serial.print(homeX);
  Serial.print(F(" y="));  Serial.print(homeY);
}
void saveInversions() {
  Serial.println(F("Saving inversions."));
  EEPROM.write(ADDR_INVL,m1i>0?1:0);
  EEPROM.write(ADDR_INVR,m2i>0?1:0);
}

void loadInversions() {
  m1i = EEPROM.read(ADDR_INVL)>0?1:-1;
  m2i = EEPROM.read(ADDR_INVR)>0?1:-1;
  adjustInversions(m1i,m2i);
}


void adjustDimensions(float newT,float newB,float newR,float newL) {
  newT = floor(newT*100)/100.0f;
  newB = floor(newB*100)/100.0f;
  newR = floor(newR*100)/100.0f;
  newL = floor(newL*100)/100.0f;

  if( limit_top    != newT ||
      limit_bottom != newB ||
      limit_right  != newR ||
      limit_left   != newL) {
        limit_top=newT;
        limit_bottom=newB;
        limit_right=newR;
        limit_left=newL;
        saveDimensions();
      }
}
char loadVersion() {
  return EEPROM.read(ADDR_VERSION);
}

void loadConfig() {
  int versionNumber = loadVersion();
  
  if( versionNumber != EEPROM_VERSION ) {
    EEPROM.write(ADDR_VERSION,EEPROM_VERSION);
    savePulleyDiameter();
  }

  robot_uid=EEPROM_readLong(ADDR_UUID);
  loadDimensions();
  loadPulleyDiameter();
  loadInversions();
  loadHome();
}

void loadPulleyDiameter() {
  adjustPulleyDiameter((float)EEPROM_readLong(ADDR_PULLEY_DIA1)/10000.0f);
}


#ifdef USE_SD_CARD
void SD_PrintDirectory(File dir, int numTabs) {
   while(true) {

     File entry =  dir.openNextFile();
     if (! entry) {
       // no more files
       Serial.println(F("**nomorefiles**"));
     }
     for (uint8_t i=0; i<numTabs; i++) {
       Serial.print('\t');
     }
     Serial.print(entry.name());
     if (entry.isDirectory()) {
       Serial.println(F("/"));
       SD_PrintDirectory(entry, numTabs+1);
     } else {
       // files have sizes, directories do not
       Serial.print(F("\t\t"));
       Serial.println(entry.size(), DEC);
     }
   }
}
void SD_ListFiles() {
  File f = SD.open("/");
  SD_PrintDirectory(f,0);
}
void SD_ProcessFile(char *filename) {
  File f=SD.open(filename);
  if(!f) {
    Serial.print(F("File "));
    Serial.print(filename);
    Serial.println(F(" not found."));
    return;
  }

  int c;
  while(f.peek() != -1) {
    c=f.read();
    if(sofar<MAX_BUF) serialBuffer[sofar++]=c;
    if(c=='\n') {
      serialBuffer[sofar]=0;
#if VERBOSE > 0
      Serial.println(serialBuffer);
#endif
      processCommand();
      sofar=0;
    }
  }

  f.close();
}
#endif 


void motor_disengage() {
#if VERBOSE > 1
  Serial.println(F("motor_disengage"));
#endif

  motors_engaged = false;

#if MOTHERBOARD == 1
  m1.release();
  m2.release();
#endif
#if MOTHERBOARD == 2
  m1->release();
  m2->release();
#endif
}


void motor_engage() {
#if VERBOSE > 1
  Serial.println(F("motor_engage"));
#endif

  motors_engaged = true;

  M1_ONESTEP(M1_REEL_IN);  M1_ONESTEP(M1_REEL_OUT);
  M2_ONESTEP(M2_REEL_IN);  M2_ONESTEP(M2_REEL_OUT);
}

 **/
float parseNumber(char code,float val) {
  char *ptr=serialBuffer;  
  while((long)ptr > 1 && (*ptr) && (long)ptr < (long)serialBuffer+sofar) { 
    if(*ptr==code) {  
      return atof(ptr+1);  
    }
    ptr=strchr(ptr,' ')+1;  
  }
  return val; 
}




void set_tool_offset(int axis,float x,float y,float z) {
  tool_offset[axis].x=x;
  tool_offset[axis].y=y;
  tool_offset[axis].z=z;
}


Vector3 get_end_plus_offset() {
  return Vector3(tool_offset[current_tool].x + posx,
                 tool_offset[current_tool].y + posy,
                 tool_offset[current_tool].z + posz);
}


void tool_change(int tool_id) {
  if(tool_id < 0) tool_id=0;
  if(tool_id > NUM_TOOLS) tool_id=NUM_TOOLS;
  current_tool=tool_id;
}


void processConfig() {
  limit_top=parseNumber('T',limit_top);
  limit_bottom=parseNumber('B',limit_bottom);
  limit_right=parseNumber('R',limit_right);
  limit_left=parseNumber('L',limit_left);

  char gg=parseNumber('G',m1d);
  char hh=parseNumber('H',m2d);
  char i=parseNumber('I',0);
  char j=parseNumber('J',0);

  adjustInversions(i,j);

  printConfig();

  teleport(0,0);
}


void adjustInversions(int m1,int m2) {
  if(m1>0) {
    M1_REEL_IN  = FORWARD;
    M1_REEL_OUT = BACKWARD;
  } else if(m1<0) {
    M1_REEL_IN  = BACKWARD;
    M1_REEL_OUT = FORWARD;
  }

  if(m2>0) {
    M2_REEL_IN  = FORWARD;
    M2_REEL_OUT = BACKWARD;
  } else if(m2<0) {
    M2_REEL_IN  = BACKWARD;
    M2_REEL_OUT = FORWARD;
  }

  if( m1!=m1i || m2 != m2i) {
    m1i=m1;
    m2i=m2;
    saveInversions();
  }
}

void processCommand() {
  // blank lines
  if(serialBuffer[0]==';') return;
  long cmd=parseNumber('N',-1);
  if(cmd!=-1 && serialBuffer[0] == 'N') {  // line number must appear first on the line
    if( cmd != line_number ) {
      Serial.print(F("BADLINENUM "));
      Serial.println(line_number);
      return;
    }


    
    if(strchr(serialBuffer,'*')!=0) {

      unsigned char checksum=0;
      int c=0;
      while(serialBuffer[c]!='*' && c<MAX_BUF) checksum ^= serialBuffer[c++];
      c++; // skip *
      int against = strtol(serialBuffer+c,NULL,10);
      if( checksum != against ) {
        Serial.print(F("BADCHECKSUM "));
        Serial.println(line_number);
        return;
      }
    } else {
      Serial.print(F("NOCHECKSUM "));
      Serial.println(line_number);
      return;
    }

    line_number++;
  }

  if(!strncmp(serialBuffer,"UID",3)) {
    robot_uid=atoi(strchr(serialBuffer,' ')+1);
    saveUID();
  }

  cmd=parseNumber('M',-1);
  switch(cmd) {
  case 17:  motor_engage();  break;
  case 18:  motor_disengage();  break;
  case 100:  help();  break;
  case 101:  processConfig();  break;
  case 110:  line_number = parseNumber('N',line_number);  break;
  case 114:  where();  break;
  }

  cmd=parseNumber('G',-1);
  switch(cmd) {
  case 0:
  case 1: {  
      Vector3 offset=get_end_plus_offset();
      setFeedRate(parseNumber('F',feed_rate));
      line_safe( parseNumber('X',(absolute_mode?offset.x:0)*10)*0.1 + (absolute_mode?0:offset.x),
                 parseNumber('Y',(absolute_mode?offset.y:0)*10)*0.1 + (absolute_mode?0:offset.y),
                 parseNumber('Z',(absolute_mode?offset.z:0)) + (absolute_mode?0:offset.z) );
    break;
    }
  case 2:
  case 3: {  
      Vector3 offset=get_end_plus_offset();
      setFeedRate(parseNumber('F',feed_rate));
      arc(parseNumber('I',(absolute_mode?offset.x:0)*10)*0.1 + (absolute_mode?0:offset.x),
          parseNumber('J',(absolute_mode?offset.y:0)*10)*0.1 + (absolute_mode?0:offset.y),
          parseNumber('X',(absolute_mode?offset.x:0)*10)*0.1 + (absolute_mode?0:offset.x),
          parseNumber('Y',(absolute_mode?offset.y:0)*10)*0.1 + (absolute_mode?0:offset.y),
          parseNumber('Z',(absolute_mode?offset.z:0)) + (absolute_mode?0:offset.z),
          (cmd==2) ? 1 : 0);
      break;
    }
  case 4:  
    pause(parseNumber('S',0) + parseNumber('P',0)*1000.0f);
    break;
  case 20: 
    mode_scale=2.54f;  
    strcpy(mode_name,"in");
    printFeedRate();
    break;
  case 21:
    mode_scale=0.1;  
    strcpy(mode_name,"mm");
    printFeedRate();
    break;
  case 28:  findHome();  break;
  case 54:
  case 55:
  case 56:
  case 57:
  case 58:
  case 59: {  
    int tool_id=cmd-54;
    set_tool_offset(tool_id,parseNumber('X',tool_offset[tool_id].x),
                            parseNumber('Y',tool_offset[tool_id].y),
                            parseNumber('Z',tool_offset[tool_id].z));
    break;
    }
  case 90:  absolute_mode=1;  break;
  case 91:  absolute_mode=0;  break;  
  case 92: {  
      Vector3 offset = get_end_plus_offset();
      teleport( parseNumber('X',(absolute_mode?offset.x:0)*10)*0.1 + (absolute_mode?0:offset.x),
                parseNumber('Y',(absolute_mode?offset.y:0)*10)*0.1 + (absolute_mode?0:offset.y)//,
  
              );
      break;
    }
  }

  cmd=parseNumber('D',-1);
  switch(cmd) {
  case 0: {
    
      char *ptr=strchr(serialBuffer,' ')+1;
      int amount = atoi(ptr+1);
      int i, dir;
      if(*ptr == m1d) {
        dir = amount < 0 ? M1_REEL_IN : M1_REEL_OUT;
        amount=abs(amount);
#if VERBOSE > 1
        Serial.print(F("M1 "));
        Serial.print(amount);
        Serial.print(' ');
        Serial.println(dir);
#endif
        for(i=0;i<amount;++i) {  M1_ONESTEP(dir);  }
      } else if(*ptr == m2d) {
        dir = amount < 0 ? M2_REEL_IN : M2_REEL_OUT;
        amount = abs(amount);
#if VERBOSE > 1
        Serial.print(F("M2 "));
        Serial.print(amount);
        Serial.print(' ');
        Serial.println(dir);
#endif
        for(i=0;i<amount;++i) {  M2_ONESTEP(dir);  }
      }
    }
    break;
  case 1: {
      // adjust spool diameters
      float amountL=parseNumber('L',pulleyDiameter);
      float amountR=parseNumber('R',pulleyDiameter);

      float tps1=threadPerStep;
      adjustPulleyDiameter(amountL);
      if(threadPerStep != tps1) {
        // Update EEPROM
        savePulleyDiameter();
      }
    }
    break;
  case 2:
    Serial.print('L');  Serial.print(pulleyDiameter);
    Serial.print(F(" R"));   Serial.println(pulleyDiameter);
    break;
#ifdef USE_SD_CARD
  case 3:  SD_ListFiles();  break;   
  case 4:  SD_ProcessFile(strchr(serialBuffer,' ')+1);  break;  
#endif
  case 5:
    sayVersionNumber();
    break;
  case 6: 
    setHome(parseNumber('X',(absolute_mode?homeX:0)*10)*0.1 + (absolute_mode?0:homeX),
            parseNumber('Y',(absolute_mode?homeY:0)*10)*0.1 + (absolute_mode?0:homeY));
    break;
  case 10:  
    Serial.print("D10 V");
    Serial.println(MAKELANGELO_HARDWARE_VERSION);
    break;
  }
}


void setHome(float x,float y) {
  if( (int)(x*100) != (int)(homeX*100)
   || (int)(y*100) != (int)(homeY*100) ) {
    homeX = x;
    homeY = y;
    saveHome();
  }
}


void ready() {
  sofar=0; 
  Serial.println(F("> "));  // signal ready to receive input
  last_ready_time = millis();
}

void tools_setup() {
  for(int i=0;i<NUM_TOOLS;++i) {
    set_tool_offset(i,0,0,0);
  }
}
void setup() {

  Serial.begin(BAUD);
  
  loadConfig();

  Serial.print(F("\n\nHELLO WORLD! I AM DRAWBOT #"));
  Serial.println(robot_uid);

#ifdef USE_SD_CARD
  SD.begin();
  SD_ListFiles();
#endif

#if MOTHERBOARD == 2
  AFMS0.begin();
  m1 = AFMS0.getStepper(STEPPER_STEPS_PER_TURN, M2_PIN);
  m2 = AFMS0.getStepper(STEPPER_STEPS_PER_TURN, M1_PIN);
#endif

  strcpy(mode_name,"mm");
  mode_scale=0.1;

#if MOTHERBOARD == 2
  
  TWBR = ((F_CPU / 400000l) - 16) / 2;
#endif

  setFeedRate(MAX_FEEDRATE); 

  s1.attach(SERVO_PIN);

  digitalWrite(L_PIN,HIGH);
  digitalWrite(R_PIN,HIGH);

  tools_setup();

  teleport(homeX,homeY);
  setPenAngle(PEN_UP_ANGLE);

  sofar=0;
  help();
  ready();

}


void testKinematics() {
  // test IK/FK
  limit_top = 500;
  limit_bottom = -500;
  limit_right = 500;
  limit_left = -500;

  float a,b,e,f;
  long c,d;
  for(int i=0;i<1000;++i) {
    a = random(-500,500);
    b = random(-500,500);
    IK(a,b,c,d);
    FK(c,d,e,f);

    if(abs(a-e)>0.1f || abs(b-f)>0.1f) {
      Serial.print(a);  Serial.print("\t");
      Serial.print(b);  Serial.print("\t");
      Serial.print(c);  Serial.print("\t");
      Serial.print(d);  Serial.print("\t");
      Serial.print(e);  Serial.print("\t");
      Serial.print(f);  Serial.print("\n");
    }
  }
}

void Serial_listen() {
  // listen for serial commands
  while(Serial.available() > 0) {
    char c = Serial.read();
    if(sofar<MAX_BUF) serialBuffer[sofar++]=c;
    if(c=='\n') {
      serialBuffer[sofar]=0;

#if VERBOSE > 0
      // echo confirmation
      Serial.println(serialBuffer);
#endif

      // do something with the command
      processCommand();
      last_cmd_time = millis();
      ready();
      break;
    }
  }
}


void loop() {
  Serial_listen();

  if( (millis() - last_cmd_time) > TIMEOUT_MOTORS && motors_engaged ) {
    motor_disengage();
  }

  
  if( (millis() - last_ready_time) > TIMEOUT_OK ) {
    ready();
  }
}

