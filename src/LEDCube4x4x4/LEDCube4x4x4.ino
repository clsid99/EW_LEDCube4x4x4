#include <FatFs.h>

/*
3  2  1  0
7  6  5  4
11 10 9  8
15 14 13 12
*/

const int latchPin = 14;//8;  // 74HC595のST_CPへ
const int clockPin = 15;//12; // 74HC595のSH_CPへ
const int dataPin = 16;//11;  // 74HC595のDSへ

const int F1Pin = 4;
const int F2Pin = 5;
const int F3Pin = 6;
const int F4Pin = 7;

typedef unsigned short ushort_t;

struct PatData
{
  ushort_t floors[4];
};

struct IndexData
{
  short pattern;
  byte params;
  byte patspeed;
  ushort_t time;
};

#define BP_RAIN     -1   // 雨
#define BP_SHIFT    -2   // 移動
#define BP_RANDOM   -3   // 

#define SHIFT_PARAMS_LOOP 0x80
#define SHIFT_PARAMS_DIR_MASK  0x0F
#define SHIFT_PARAMS_DIR_T2B  1
#define SHIFT_PARAMS_DIR_B2T  2
#define SHIFT_PARAMS_DIR_F2B  3
#define SHIFT_PARAMS_DIR_B2F  4
#define SHIFT_PARAMS_DIR_L2R  5
#define SHIFT_PARAMS_DIR_R2L  6

#define RANDOM_PARAMS_OFF     0
#define RANDOM_PARAMS_ON      1

#define SET(F4, F3, F2, F1) \
  { \
    g_pattern_datas[index].floors[0] = F1; \
    g_pattern_datas[index].floors[1] = F2; \
    g_pattern_datas[index].floors[2] = F3; \
    g_pattern_datas[index].floors[3] = F4; \
    index++; \
  }
  
#define INDEX(PAT, PARAM, SPEED, TIME) \
 { \
   g_index_datas[index].pattern = (PAT); \
   g_index_datas[index].params = (PARAM); \
   g_index_datas[index].patspeed = (SPEED); \
   g_index_datas[index].time = (TIME); \
   index++; \
 }
    
static PatData *g_pattern_datas = 0;
static ushort_t g_pattern_count = 0;
static IndexData *g_index_datas = 0;
static ushort_t g_count = 0;

static ushort_t g_index = 0;
static unsigned long g_prev_time = 0;
static unsigned long g_animation_timer = 0;
static PatData g_data_instance = {0};

void clear_datas ()
{
  if (g_pattern_datas)
    free (g_pattern_datas);
  if (g_index_datas)
    free (g_index_datas);
    
  g_pattern_datas = 0;
  g_index_datas = 0;
  g_pattern_count = 0;
  g_index = 0;
  g_count = 0;
}

void load_data ()
{
  clear_datas ();

  File file;
  if (!file.open ("1.dat"))
    return;
   
  file.read ((uint8_t *)&g_pattern_count, 2);
  
  g_pattern_datas = (PatData *)malloc (sizeof (PatData) * g_pattern_count);
  if (!g_pattern_datas)
    {
      clear_datas ();
      file.close ();
      return;
    }
  file.read ((uint8_t *)g_pattern_datas, g_pattern_count * sizeof (PatData));
  
  file.read ((uint8_t *)&g_count, 2);
  
  g_index_datas = (IndexData *)malloc (sizeof (IndexData) * g_count);
  if (!g_index_datas)
    {
      clear_datas ();
      file.close ();
      return;
    }
  file.read ((uint8_t *)g_index_datas, sizeof (IndexData) * g_count);
   
  file.close ();
  g_animation_timer = 0;
}

void rain (unsigned long time, int params, int patspeed)
{
  if (time - g_animation_timer > patspeed)
    {
      g_data_instance.floors[0] = g_data_instance.floors[1];
      g_data_instance.floors[1] = g_data_instance.floors[2];
      g_data_instance.floors[2] = g_data_instance.floors[3];
      g_data_instance.floors[3] = 0;
      
      if (g_data_instance.floors[3] == 0)
        {
          for (int i = 0; i < max (1, params); i++)
            {
              int b = random(16);
              g_data_instance.floors[3] |= (1 << b);
            }
        }
      g_animation_timer = time;
    }
}

void shift (unsigned long time, int params, int patspeed)
{
  int dir = params & SHIFT_PARAMS_DIR_MASK;
  
  if (time - g_animation_timer > patspeed)
    {
      ushort_t save = 0;
      switch (dir)
        {
        case SHIFT_PARAMS_DIR_T2B:
          save = g_data_instance.floors[0];
          g_data_instance.floors[0] = g_data_instance.floors[1];
          g_data_instance.floors[1] = g_data_instance.floors[2];
          g_data_instance.floors[2] = g_data_instance.floors[3];
          g_data_instance.floors[3] = (params & SHIFT_PARAMS_LOOP) ? save : 0;
          break;
          
        case SHIFT_PARAMS_DIR_B2T:
          save = g_data_instance.floors[3];
          g_data_instance.floors[3] = g_data_instance.floors[2];
          g_data_instance.floors[2] = g_data_instance.floors[1];
          g_data_instance.floors[1] = g_data_instance.floors[0];
          g_data_instance.floors[0] = (params & SHIFT_PARAMS_LOOP) ? save : 0;
          break;
          
        case SHIFT_PARAMS_DIR_F2B:
          for (int i = 0; i < 4; i++)
            {
              save = (g_data_instance.floors[i] & 0x000f) << 12;
              g_data_instance.floors[i] = g_data_instance.floors[i] >> 4;
              g_data_instance.floors[i] |= (params & SHIFT_PARAMS_LOOP) ? save : 0;
            }
          break;
        case SHIFT_PARAMS_DIR_B2F:
          for (int i = 0; i < 4; i++)
            {
              save = (g_data_instance.floors[i] & 0xf000) >> 12;
              g_data_instance.floors[i] = g_data_instance.floors[i] << 4;
              g_data_instance.floors[i] |= (params & SHIFT_PARAMS_LOOP) ? save : 0;
            }
          break;
        case SHIFT_PARAMS_DIR_L2R:
          for (int i = 0; i < 4; i++)
            {
              for (int j = 0; j < 16; j += 4)
                {
                  ushort_t mask = 0xf << j;
                  ushort_t v = g_data_instance.floors[i] & mask;
                  save = v & (0x1 << j);
                  save = save << 3;
                  v = v >> 1;
                  v |= (params & SHIFT_PARAMS_LOOP) ? save : 0;
                  g_data_instance.floors[i] &= ~mask;
                  g_data_instance.floors[i] |= (v & mask);
                }
            }
          break;
        case SHIFT_PARAMS_DIR_R2L:
          for (int i = 0; i < 4; i++)
            {
              for (int j = 0; j < 16; j += 4)
                {
                  ushort_t mask = 0xf << j;
                  ushort_t v = g_data_instance.floors[i] & mask;
                  save = v & (0x8 << j);
                  save = save >> 3;
                  v = v << 1;
                  v |= (params & SHIFT_PARAMS_LOOP) ? save : 0;
                  g_data_instance.floors[i] &= ~mask;
                  g_data_instance.floors[i] |= (v & mask);
                }
            }
          break;
        }
        
      g_animation_timer = time;
    }
}

void random_all (unsigned long time, int params, int patspeed)
{
  if (time - g_animation_timer > patspeed)
    {
      int f = random(4);
      int b = random(16);
      
      if (params & RANDOM_PARAMS_ON)
        g_data_instance.floors[f] |= (1 << b);
      else
        g_data_instance.floors[f] &= ~(1 << b);
    }
}

void setup() {
  delay(500);
  if(FatFs.initialize())
    {
      load_data ();
    }
  
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(dataPin, OUTPUT);

  pinMode(F1Pin, OUTPUT);
  pinMode(F2Pin, OUTPUT);
  pinMode(F3Pin, OUTPUT);
  pinMode(F4Pin, OUTPUT);
}

void loop()
{
  if (!g_index_datas)
    return;
  
  ushort_t continue_time = 0;

  if (g_index_datas[g_index].pattern >= 0)
    {
      if (g_index_datas[g_index].pattern < g_pattern_count)
        {
          g_data_instance = g_pattern_datas[g_index_datas[g_index].pattern];
        }
      continue_time = g_index_datas[g_index].time;
    }
  else
    {
      unsigned long time = millis ();
  
      continue_time = g_index_datas[g_index].time;
      switch (g_index_datas[g_index].pattern)
        {
        case BP_RAIN:           // 雨
          rain (time, g_index_datas[g_index].params, g_index_datas[g_index].patspeed);
          break;
        case BP_SHIFT:
          shift (time, g_index_datas[g_index].params, g_index_datas[g_index].patspeed);
          break;
        case BP_RANDOM:
          random_all (time, g_index_datas[g_index].params, g_index_datas[g_index].patspeed);
          break;
        
        default:
          continue_time = 100;
          break;
        }
    }
    
   PatData *data = &g_data_instance;

  for (int i = 0; i < 4; i++)
    {
      digitalWrite(latchPin, LOW);
      shiftOut(dataPin, clockPin, MSBFIRST, 0);
      shiftOut(dataPin, clockPin, MSBFIRST, 0);
      digitalWrite(latchPin, HIGH);
      
      digitalWrite(F1Pin + i, HIGH);
        digitalWrite(latchPin, LOW);
        shiftOut(dataPin, clockPin, MSBFIRST, 0);
        shiftOut(dataPin, clockPin, MSBFIRST, highByte (data->floors[i]));
        shiftOut(dataPin, clockPin, MSBFIRST, lowByte (data->floors[i]));
        digitalWrite(latchPin, HIGH);
      delay (2);
      digitalWrite(F1Pin + i, LOW);
    }
        
  unsigned long time = millis ();
  if (time - g_prev_time > continue_time)
    {
      g_index = (g_index + 1) % g_count;
      g_prev_time = time;
      g_animation_timer = time;
    }
}
