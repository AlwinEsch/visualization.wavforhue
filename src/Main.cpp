/*
*      Copyright (C) 2008-2013 Team XBMC
*      http://xbmc.org
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with XBMC; see the file COPYING.  If not, see
*  <http://www.gnu.org/licenses/>.
*
*/

// Waveform.vis
// Was a simple visualisation example by MrC

#include <thread>
#include <stdio.h>
#ifdef HAS_OPENGL
#include "xbmc_vis_dll.h" //so I can build for windows
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
//#include <GL/glew.h>
#endif
#include <unistd.h>
#else
#ifdef _WIN32
#include <d3d11_1.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <windows.h>
#ifndef curl_socket_typedef
/* socket typedef */
#include <winsock2.h>
typedef SOCKET curl_socket_t;
#define CURL_SOCKET_BAD INVALID_SOCKET
#endif
//#include "addons/include/xbmc_vis_dll.h"
#include "xbmc_vis_dll.h"
#endif
#endif

//#include "libXBMC_addon.h"

//th
#include <curl/curl.h>
#include "fft.h"
#include <cstring>
#include <math.h>
#include <sstream>
#include <vector>

//

char g_visName[512];
#ifndef HAS_OPENGL
//LPDIRECT3DDEVICE9 g_device;
ID3D11Device*             g_device = NULL;
ID3D11DeviceContext*      g_context = NULL;
ID3D11VertexShader*       g_vShader = NULL;
ID3D11PixelShader*        g_pShader = NULL;
ID3D11InputLayout*        g_inputLayout = NULL;
ID3D11Buffer*             g_vBuffer = NULL;
ID3D11Buffer*             g_cViewPort = NULL;

using namespace DirectX;
using namespace DirectX::PackedVector;

// Include the precompiled shader code.
namespace
{
  //#include "DefaultPixelShader.inc"
  //#include "DefaultVertexShader.inc"
  #include "DefaultPixelShader.hlsl"
  #include "DefaultVertexShader.hlsl"
}

struct cbViewPort
{
  float g_viewPortWidth;
  float g_viewPortHeigh;
  float align1, align2;
};

#else
void* g_device;
#endif

float g_fWaveform[2][512];

#ifdef HAS_OPENGL
typedef struct {
  int TopLeftX;
  int TopLeftY;
  int Width;
  int Height;
  int MinDepth;
  int MaxDepth;
} D3D11_VIEWPORT;
typedef unsigned long D3DCOLOR;
#endif

D3D11_VIEWPORT g_viewport;

struct Vertex_t
{
  float x, y, z;
#ifdef HAS_OPENGL
  D3DCOLOR  col;
#else
  XMFLOAT4 col;
#endif
};

#ifndef HAS_OPENGL
bool init_renderer_objs()
{
  // Create vertex shader
  if (S_OK != g_device->CreateVertexShader(DefaultVertexShaderCode, sizeof(DefaultVertexShaderCode), nullptr, &g_vShader))
    return false;

  // Create input layout
  D3D11_INPUT_ELEMENT_DESC layout[] =
  {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
  };
  if (S_OK != g_device->CreateInputLayout(layout, ARRAYSIZE(layout), DefaultVertexShaderCode, sizeof(DefaultVertexShaderCode), &g_inputLayout))
    return false;

  // Create pixel shader
  if (S_OK != g_device->CreatePixelShader(DefaultPixelShaderCode, sizeof(DefaultPixelShaderCode), nullptr, &g_pShader))
    return false;

  // create buffers
  CD3D11_BUFFER_DESC desc(sizeof(Vertex_t) * 512, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
  if (S_OK != g_device->CreateBuffer(&desc, NULL, &g_vBuffer))
    return false;

  desc.ByteWidth = sizeof(cbViewPort);
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.CPUAccessFlags = 0;

  cbViewPort viewPort = { (float)g_viewport.Width, (float)g_viewport.Height, 0.0f, 0.0f };
  D3D11_SUBRESOURCE_DATA initData;
  initData.pSysMem = &viewPort;

  if (S_OK != g_device->CreateBuffer(&desc, &initData, &g_cViewPort))
    return false;

  // we are ready
  return true;
}
#endif // !HAS_OPENGL

//th
#define BUFFERSIZE 1024
#define NUM_FREQUENCIES (512)


namespace
{
  // User config settings
  //UserSettings g_Settings;

  FFT g_fftobj;

#ifdef _WIN32
  FLOAT fSecsPerTick;
  LARGE_INTEGER qwTime, qwLastTime, qwLightTime, qwElapsedTime, qwAppTime, qwElapsedAppTime;
#endif
  float fTime, fElapsedTime, fAppTime, fElapsedAppTime, fUpdateTime, fLastTime, fLightTime;
  int iFrames = 0;
  float fFPS = 0;
}

struct SoundData
{
  float   imm[2][3];                // bass, mids, treble, no damping, for each channel (long-term average is 1)
  float   avg[2][3];               // bass, mids, treble, some damping, for each channel (long-term average is 1)
  float   med_avg[2][3];          // bass, mids, treble, more damping, for each channel (long-term average is 1)
  //    float   long_avg[2][3];        // bass, mids, treble, heavy damping, for each channel (long-term average is 1)
  float   fWaveform[2][576];             // Not all 576 are valid! - only NUM_WAVEFORM_SAMPLES samples are valid for each channel (note: NUM_WAVEFORM_SAMPLES is declared in shell_defines.h)
  float   fSpectrum[2][NUM_FREQUENCIES]; // NUM_FREQUENCIES samples for each channel (note: NUM_FREQUENCIES is declared in shell_defines.h)

  float specImm[32];
  float specAvg[32];
  float specMedAvg[32];

  float bigSpecImm[512];
  float leftBigSpecAvg[512];
  float rightBigSpecAvg[512];
};

SoundData g_sound;
float g_bass, g_bassLast;
float g_treble, g_trebleLast;
float g_middle, g_middleLast;
float g_timePass;
bool g_finished;
float g_movingAvgMid[128];
float g_movingAvgMidSum;

//th - settings used throughout
bool useWaveForm = true;
std::string strHueBridgeIPAddress = "192.168.10.6";
std::vector<std::string> activeLightIDs, dimmedLightIDs, afterLightIDs;
int numberOfActiveLights = 3, numberOfDimmedLights = 2, numberOfAfterLights = 1;
int lastHue, initialHue, targetHue, maxBri, targetBri;
int currentBri = 75;
float beatThreshold = 0.25f;
int dimmedBri = 10, dimmedSat = 255, dimmedHue = 65280;
int afterBri = 25, afterSat = 255, afterHue = 65280;
bool lightsOnAfter = false;
bool cuboxHDMIFix = false;
float rgb[3] = { 1.0f, 1.0f, 1.0f };
/* 
This is used if audiodata is not coming from Kodi nicely.
The problem is with Solidrun's Cubox (imx6) set to HDMI audio out. The Waveform visualisation
has the right 1/4 of its waveforms flat because 0's are being reported by the visualisation
API for that architecture.
*/
int iMaxAudioData_i = 256;
float fMaxAudioData = 255.0f;


/*
ADDON::CHelper_libXBMC_addon *XBMC           = NULL;
bool registerHelper(void* hdl)
{
  if (!XBMC)
    XBMC = new ADDON::CHelper_libXBMC_addon;

  if (!XBMC->RegisterMe(hdl))
  {
    delete XBMC, XBMC=NULL;
    return false;
  }

  return true;
}
*/



#ifndef _WIN32
struct timespec systemClock;
#endif


//hsv to rgb conversion
void hsvToRgb(float h, float s, float v, float _rgb[]) {
  float r = 0.0f, g = 0.0f, b = 0.0f;

  int i = int(h * 6);
  float f = h * 6 - i;
  float p = v * (1 - s);
  float q = v * (1 - f * s);
  float t = v * (1 - (1 - f) * s);

  switch (i % 6){
  case 0: r = v, g = t, b = p; break;
  case 1: r = q, g = v, b = p; break;
  case 2: r = p, g = v, b = t; break;
  case 3: r = p, g = q, b = v; break;
  case 4: r = t, g = p, b = v; break;
  case 5: r = v, g = p, b = q; break;
  }

  _rgb[0] = r;
  _rgb[1] = g;
  _rgb[2] = b;
}

size_t noop_cb(void *ptr, size_t size, size_t nmemb, void *data) {
  return size * nmemb;
}

void putWorkerThread(std::string m_strJson, std::string lightID, std::string m_strHueBridgeIPAddress)
{
  std::string strURLLight;
  CURL *curl = curl_easy_init();

  if (curl) 
  {
    strURLLight = "http://" + m_strHueBridgeIPAddress +
      "/api/KodiVisWave/lights/" + lightID + "/state";
    CURLcode res;
    // Now specify we want to PUT data, but not using a file, so it has o be a CUSTOMREQUEST
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, noop_cb);
    //curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, m_strJson.c_str());
    // Set the URL that is about to receive our POST. 
    //printf("Sent %s to %s\n", strJson.c_str(), strURLLight.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, strURLLight.c_str());
    // Perform the request, res will get the return code
    res = curl_easy_perform(curl);
    // always cleanup curl
    curl_easy_cleanup(curl);
  }
}

void putMainThread(int bri, int sat, int hue, int transitionTime, std::vector<std::string> lightIDs, int numberOfLights, bool on, bool off)
{
  std::string strJson;
  
  if (on) //turn on
    strJson = "{\"on\":true}";
  else if (off) //turn light off
    strJson = "{\"on\":false}";
  else if (sat > 0) //change saturation
  {
    std::ostringstream oss;
    oss << "{\"bri\":" << bri << ",\"hue\":" << hue <<
      ",\"sat\":" << sat << ",\"transitiontime\":"
      << transitionTime << "}";
    strJson = oss.str();
  }
  else //change lights
  {
    std::ostringstream oss;
    oss << "{\"bri\":" << bri << ",\"hue\":" << hue <<
      ",\"transitiontime\":" << transitionTime << "}";
    strJson = oss.str();
  }

  for (int i = 0; i < numberOfLights; i++)
  {
    //threading here segfaults upon addon destroy
    //std::thread (putWorkerThread, strJson, lightIDs[i], strHueBridgeIPAddress).detach();  
    putWorkerThread(strJson, lightIDs[i], strHueBridgeIPAddress);
  }
}

void TurnLightsOn(std::vector<std::string> lightIDs, int numberOfLights)
{
  putMainThread(0, 0, 0, 0, lightIDs, numberOfLights, true, false);
}

void TurnLightsOff(std::vector<std::string> lightIDs, int numberOfLights)
{
  putMainThread(0, 0, 0, 0, lightIDs, numberOfLights, false, true);
}

void UpdateLights(int bri, int sat, int hue, int transitionTime, std::vector<std::string> lightIDs, int numberOfLights)
{
  std::thread (putMainThread, bri, sat, hue, transitionTime, lightIDs, numberOfLights, false, false).detach();
}

void AdjustBrightness() //nicely bring the brightness up or down
{
  int briDifference = currentBri - targetBri;
  if (briDifference > 7) currentBri = currentBri - 7;
  else if (briDifference < -7) currentBri = currentBri + 7;
  else currentBri = targetBri;
}

void FastBeatLights()
{
  AdjustBrightness();
  //figure out a good brightness increase
  int beatBri = (int)(currentBri * 1.5f);
  if (beatBri > 255) beatBri = 255;
  //transition the color immediately
  UpdateLights(beatBri, 0, lastHue, 0, activeLightIDs, numberOfActiveLights);
  //fade brightness
  UpdateLights(5, 0, lastHue, 10, activeLightIDs, numberOfActiveLights); //fade
}

void SlowBeatLights()
{
  AdjustBrightness();
  //figure out a good brightness increase
  int beatBri = (int)(currentBri * 1.25f);
  if (beatBri > 255) beatBri = 255;
  //transition the color immediately
  UpdateLights(beatBri, 0, lastHue, 2, activeLightIDs, numberOfActiveLights);
  //fade brightness
  UpdateLights(5, 0, lastHue, 8, activeLightIDs, numberOfActiveLights); //fade
}

void CycleHue(int huePoints)
{
  int hueGap;
  if ((lastHue - targetHue) > 0) hueGap = lastHue - targetHue;
  else hueGap = (lastHue - targetHue) * -1;
  if (hueGap > huePoints)
  {
    if (lastHue > targetHue) lastHue = lastHue - huePoints;
    else lastHue = lastHue + huePoints;
  }
  else
  {
    lastHue = targetHue;
    targetHue = initialHue;
    initialHue = lastHue;
  }
  //for the waveform to match the lights
  hsvToRgb(((float)lastHue / 65535.0f), 1.0f, 1.0f, rgb);
}

void CycleLights()
{
  //this is called once per second if no beats are detected
  CycleHue(3000);
  AdjustBrightness();
  UpdateLights(currentBri, 0, lastHue, 10, activeLightIDs, numberOfActiveLights);
}

//taken from Vortex
float AdjustRateToFPS(float per_frame_decay_rate_at_fps1, float fps1, float actual_fps)
{
  // returns the equivalent per-frame decay rate at actual_fps

  // basically, do all your testing at fps1 and get a good decay rate;
  // then, in the real application, adjust that rate by the actual fps each time you use it.

  float per_second_decay_rate_at_fps1 = powf(per_frame_decay_rate_at_fps1, fps1);
  float per_frame_decay_rate_at_fps2 = powf(per_second_decay_rate_at_fps1, 1.0f / actual_fps);

  return per_frame_decay_rate_at_fps2;
}

//taken from Vortex
void AnalyzeSound()
{
  int m_fps = 60;

  // sum (left channel) spectrum up into 3 bands
  // [note: the new ranges do it so that the 3 bands are equally spaced, pitch-wise]
  float min_freq = 200.0f;
  float max_freq = 11025.0f;
  float net_octaves = (logf(max_freq / min_freq) / logf(2.0f));     // 5.7846348455575205777914165223593
  float octaves_per_band = net_octaves / 3.0f;                    // 1.9282116151858401925971388407864
  float mult = powf(2.0f, octaves_per_band); // each band's highest freq. divided by its lowest freq.; 3.805831305510122517035102576162
  // [to verify: min_freq * mult * mult * mult should equal max_freq.]
  //    for (int ch=0; ch<2; ch++)
  {
    for (int i = 0; i<3; i++)
    {
      // old guesswork code for this:
      //   float exp = 2.1f;
      //   int start = (int)(NUM_FREQUENCIES*0.5f*powf(i/3.0f, exp));
      //   int end   = (int)(NUM_FREQUENCIES*0.5f*powf((i+1)/3.0f, exp));
      // results:
      //          old range:      new range (ideal):
      //   bass:  0-1097          200-761
      //   mids:  1097-4705       761-2897
      //   treb:  4705-11025      2897-11025
      int start = (int)(NUM_FREQUENCIES * min_freq*powf(mult, (float)i) / 11025.0f);
      int end = (int)(NUM_FREQUENCIES * min_freq*powf(mult, (float)i + 1) / 11025.0f);
      if (start < 0) start = 0;
      if (end > NUM_FREQUENCIES) end = NUM_FREQUENCIES;

      g_sound.imm[0][i] = 0;
      for (int j = start; j<end; j++)
      {
        g_sound.imm[0][i] += g_sound.fSpectrum[0][j];
        g_sound.imm[0][i] += g_sound.fSpectrum[1][j];
      }
      g_sound.imm[0][i] /= (float)(end - start) * 2;
    }
  }

  // multiply by long-term, empirically-determined inverse averages:
  // (for a trial of 244 songs, 10 seconds each, somewhere in the 2nd or 3rd minute,
  //  the average levels were: 0.326781557	0.38087377	0.199888934
  for (int ch = 0; ch<2; ch++)
  {
    g_sound.imm[ch][0] /= 0.326781557f;//0.270f;   
    g_sound.imm[ch][1] /= 0.380873770f;//0.343f;   
    g_sound.imm[ch][2] /= 0.199888934f;//0.295f;   
  }

  // do temporal blending to create attenuated and super-attenuated versions
  for (int ch = 0; ch<2; ch++)
  {
    for (int i = 0; i<3; i++)
    {
      // g_sound.avg[i]
      {
        float avg_mix;
        if (g_sound.imm[ch][i] > g_sound.avg[ch][i])
          avg_mix = AdjustRateToFPS(0.2f, 14.0f, (float)m_fps);
        else
          avg_mix = AdjustRateToFPS(0.5f, 14.0f, (float)m_fps);
        //                if (g_sound.imm[ch][i] > g_sound.avg[ch][i])
        //                  avg_mix = 0.5f;
        //                else 
        //                  avg_mix = 0.8f;
        g_sound.avg[ch][i] = g_sound.avg[ch][i] * avg_mix + g_sound.imm[ch][i] * (1 - avg_mix);
      }

      {
        float med_mix = 0.91f;//0.800f + 0.11f*powf(t, 0.4f);    // primarily used for velocity_damping
        float long_mix = 0.96f;//0.800f + 0.16f*powf(t, 0.2f);    // primarily used for smoke plumes
        med_mix = AdjustRateToFPS(med_mix, 14.0f, (float)m_fps);
        long_mix = AdjustRateToFPS(long_mix, 14.0f, (float)m_fps);
        g_sound.med_avg[ch][i] = g_sound.med_avg[ch][i] * (med_mix)+g_sound.imm[ch][i] * (1 - med_mix);
        //                g_sound.long_avg[ch][i] = g_sound.long_avg[ch][i]*(long_mix) + g_sound.imm[ch][i]*(1-long_mix);
      }
    }
  }

  float newBass = ((g_sound.avg[0][0] - g_sound.med_avg[0][0]) / g_sound.med_avg[0][0]) * 2;
  float newMiddle = ((g_sound.avg[0][1] - g_sound.med_avg[0][1]) / g_sound.med_avg[0][1]) * 2;
  float newTreble = ((g_sound.avg[0][2] - g_sound.med_avg[0][2]) / g_sound.med_avg[0][2]) * 2;

#ifdef _WIN32
  newBass = max(min(newBass, 1.0f), -1.0f);
  newMiddle = max(min(newMiddle, 1.0f), -1.0f);
  newTreble = max(min(newTreble, 1.0f), -1.0f);
#else
  newBass = std::max(std::min(newBass, 1.0f), -1.0f);
  newMiddle = std::max(std::min(newMiddle, 1.0f), -1.0f);
  newTreble = std::max(std::min(newTreble, 1.0f), -1.0f);
#endif

  g_bassLast = g_bass;
  g_middleLast = g_middle;

  float avg_mix;
  if (newTreble > g_treble)
    avg_mix = 0.5f;
  else
    avg_mix = 0.5f;

  //dealing with NaN's in linux
  if (g_bass != g_bass) g_bass = 0;
  if (g_middle != g_middle) g_middle = 0;
  if (g_treble != g_treble) g_treble = 0;

  g_bass = g_bass*avg_mix + newBass*(1 - avg_mix);
  g_middle = g_middle*avg_mix + newMiddle*(1 - avg_mix);
  //g_treble = g_treble*avg_mix + newTreble*(1 - avg_mix);

#ifdef _WIN32
  g_bass = max(min(g_bass, 1.0f), -1.0f);
  g_middle = max(min(g_middle, 1.0f), -1.0f);
  //g_treble = max(min(g_treble, 1.0f), -1.0f);
#else
  g_bass = std::max(std::min(g_bass, 1.0f), -1.0f);
  g_middle = std::max(std::min(g_middle, 1.0f), -1.0f);
  //g_treble = std::max(std::min(g_treble, 1.0f), -1.0f);
#endif

  if (g_middle < 0) g_middle = g_middle * -1.0f;
  if (g_bass < 0) g_bass = g_bass * -1.0f;

  if (((g_middle - g_middleLast) > beatThreshold ||
    (g_bass - g_bassLast > beatThreshold))
    && ((fAppTime - fLightTime) > 0.3f))
  {
    //beat
    FastBeatLights();
    CycleHue(1500);
    //changed lights
    fLightTime = fAppTime;
  }
}

void InitTime()
{
#ifdef _WIN32
  // Get the frequency of the timer
  LARGE_INTEGER qwTicksPerSec;
  QueryPerformanceFrequency(&qwTicksPerSec);
  fSecsPerTick = 1.0f / (FLOAT)qwTicksPerSec.QuadPart;

  // Save the start time
  QueryPerformanceCounter(&qwTime);
  qwLastTime.QuadPart = qwTime.QuadPart;
  qwLightTime.QuadPart = qwTime.QuadPart;

  qwAppTime.QuadPart = 0;
  qwElapsedTime.QuadPart = 0;
  qwElapsedAppTime.QuadPart = 0;
  srand(qwTime.QuadPart);
#else
  // Save the start time
  clock_gettime(CLOCK_MONOTONIC, &systemClock);
  fTime = ((float)systemClock.tv_nsec / 1000000000.0) + (float)systemClock.tv_sec;
#endif

  fAppTime = 0;
  fElapsedTime = 0;
  fElapsedAppTime = 0;
  fLastTime = 0;
  fLightTime = 0;
  fUpdateTime = 0;

}

void UpdateTime()
{
#ifdef _WIN32
  QueryPerformanceCounter(&qwTime);
  qwElapsedTime.QuadPart = qwTime.QuadPart - qwLastTime.QuadPart;
  qwLastTime.QuadPart = qwTime.QuadPart;
  qwLightTime.QuadPart = qwTime.QuadPart;
  qwElapsedAppTime.QuadPart = qwElapsedTime.QuadPart;
  qwAppTime.QuadPart += qwElapsedAppTime.QuadPart;

  // Store the current time values as floating point
  fTime = fSecsPerTick * ((FLOAT)(qwTime.QuadPart));
  fElapsedTime = fSecsPerTick * ((FLOAT)(qwElapsedTime.QuadPart));
  fAppTime = fSecsPerTick * ((FLOAT)(qwAppTime.QuadPart));
  fElapsedAppTime = fSecsPerTick * ((FLOAT)(qwElapsedAppTime.QuadPart));
#else
  clock_gettime(CLOCK_MONOTONIC, &systemClock);
  fTime = ((float)systemClock.tv_nsec / 1000000000.0) + (float)systemClock.tv_sec;
  fElapsedTime = fTime - fLastTime;
  fLastTime = fTime;
  fAppTime += fElapsedTime;
#endif

  // Keep track of the frame count
  iFrames++;

  //fBeatTime = 60.0f / (float)(bpm); //skip every other beat

  // If beats aren't doing anything then cycle colors nicely
  if (fAppTime - fLightTime > 1.5f)
  {
    CycleLights();
    fLightTime = fAppTime;
  }

  g_movingAvgMidSum = 0.0f;
  //update the max brightness based on the moving avg of the mid levels
  for (int i = 0; i<128; i++)
  {
    g_movingAvgMidSum += g_movingAvgMid[i];
    if (i != 127)
      g_movingAvgMid[i] = g_movingAvgMid[i + 1];
    else
      g_movingAvgMid[i] = (g_sound.avg[0][1] + g_sound.avg[1][1]) / 2.0f;
  }

  if ((g_movingAvgMidSum*1000.0f / 15.0f) < 0.5f &&
    (g_movingAvgMidSum*1000.0f / 15.0f) > 0.1f)
    targetBri = (int)(maxBri * 2 * g_movingAvgMidSum*1000.0f / 15.0f);
  else if (g_movingAvgMidSum*1000.0f / 15.0f > 0.5f)
    targetBri = maxBri;
  else if (g_movingAvgMidSum*1000.0f / 15.0f < 0.1f)
    targetBri = (int)(maxBri * 0.1f);

  // Update the scene stats once per second
  if (fAppTime - fUpdateTime > 1.0f)
  {
    fFPS = (float)(iFrames / (fAppTime - fLastTime));
    fUpdateTime = fAppTime;
    iFrames = 0;
  }
}

#ifdef _WIN32
void usleep(int waitTime) {
  __int64 time1 = 0, time2 = 0, freq = 0;

  QueryPerformanceCounter((LARGE_INTEGER *)&time1);
  QueryPerformanceFrequency((LARGE_INTEGER *)&freq);

  do {
    QueryPerformanceCounter((LARGE_INTEGER *)&time2);
  } while ((time2 - time1) < waitTime);
}
#endif


//-- Create -------------------------------------------------------------------
// Called on load. Addon should fully initalize or return error status
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  /*
  if (!registerHelper(hdl))
    return ADDON_STATUS_PERMANENT_FAILURE;
  XBMC->Log(ADDON::LOG_ERROR, "WavforHue says hi.");
  */

  if (!props)
    return ADDON_STATUS_UNKNOWN;

  VIS_PROPS* visProps = (VIS_PROPS*)props;

#ifdef HAS_OPENGL
  g_device = visProps->device;
#endif
  g_viewport.TopLeftX = visProps->x;
  g_viewport.TopLeftY = visProps->y;
  g_viewport.Width = visProps->width;
  g_viewport.Height = visProps->height;
  g_viewport.MinDepth = 0;
  g_viewport.MaxDepth = 1;
#ifndef HAS_OPENGL  
  g_context = (ID3D11DeviceContext*)visProps->device;
  g_context->GetDevice(&g_device);
  if (!init_renderer_objs())
    return ADDON_STATUS_PERMANENT_FAILURE;
#endif


  activeLightIDs.push_back("1");
  activeLightIDs.push_back("2");
  activeLightIDs.push_back("3");
  dimmedLightIDs.push_back("4");
  dimmedLightIDs.push_back("5");  
  afterLightIDs.push_back("4");

  return ADDON_STATUS_NEED_SAVEDSETTINGS;
}

//-- Start --------------------------------------------------------------------
// Called when a new soundtrack is played
//-----------------------------------------------------------------------------
extern "C" void Start(int iChannels, int iSamplesPerSec, int iBitsPerSample, const char* szSongName)
{
  std::string strURLRegistration = "http://" + strHueBridgeIPAddress + "/api";

  CURL *curl;
  CURLcode res;

  //set Hue registration command
  const char json[] = "{\"devicetype\":\"Kodi\",\"username\":\"KodiVisWave\"}";

  //struct curl_slist *headers = NULL;
  //XBMC->Log(ADDON::LOG_INFO, "WavforHue: checkpoint 1");
  curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, noop_cb);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
  // Set the URL that is about to receive our POST.
  curl_easy_setopt(curl, CURLOPT_URL, strURLRegistration.c_str());
  // Perform the request, res will get the return code
  res = curl_easy_perform(curl);
  // always cleanup curl
  curl_easy_cleanup(curl);

  //turn the Active lights on
  TurnLightsOn(activeLightIDs, numberOfActiveLights);
  UpdateLights(currentBri, 255, lastHue, 30, activeLightIDs, numberOfActiveLights);
  
  if(numberOfDimmedLights>0)
  {
    //dim the other lights
    TurnLightsOn(dimmedLightIDs, numberOfDimmedLights);
    UpdateLights(dimmedBri, dimmedSat, dimmedHue, 30, dimmedLightIDs, numberOfDimmedLights);
  }

  //initialize the beat detection
  InitTime();
  g_fftobj.Init(576, NUM_FREQUENCIES);

  //initialize the moving average of mids
  for (int i = 0; i<15; i++)
  {
    g_movingAvgMid[i] = 0;
  }
  
  //initialize the workaround for Cubox (imx6) HDMI
  if(cuboxHDMIFix)
  {
	iMaxAudioData_i = 180;
	fMaxAudioData = 179.0f;
  }
}

//-- Audiodata ----------------------------------------------------------------
// Called by XBMC to pass new audio data to the vis
//-----------------------------------------------------------------------------
extern "C" void AudioData(const float* pAudioData, int iAudioDataLength, float *pFreqData, int iFreqDataLength)
{
  int ipos = 0;
  while (ipos < 512)
  {
    for (int i = 0; i < iAudioDataLength; i += 2)
    {
      g_fWaveform[0][ipos] = pAudioData[i  ]; // left channel
      g_fWaveform[1][ipos] = pAudioData[i+1]; // right channel
      ipos++;
      if (ipos >= 512) break;
    }
  }

  //taken from Vortex
  float tempWave[2][576];

  int iPos = 0;
  int iOld = 0;
  //const float SCALE = (1.0f / 32768.0f ) * 255.0f;
  while (iPos < 576)
  {
    for (int i = 0; i < iAudioDataLength; i += 2)
    {
      g_sound.fWaveform[0][iPos] = float((pAudioData[i] / 32768.0f) * 255.0f);
      g_sound.fWaveform[1][iPos] = float((pAudioData[i + 1] / 32768.0f) * 255.0f);

      // damp the input into the FFT a bit, to reduce high-frequency noise:
      tempWave[0][iPos] = 0.5f * (g_sound.fWaveform[0][iPos] + g_sound.fWaveform[0][iOld]);
      tempWave[1][iPos] = 0.5f * (g_sound.fWaveform[1][iPos] + g_sound.fWaveform[1][iOld]);
      iOld = iPos;
      iPos++;
      if (iPos >= 576)
        break;
    }
  }

  g_fftobj.time_to_frequency_domain(tempWave[0], g_sound.fSpectrum[0]);
  g_fftobj.time_to_frequency_domain(tempWave[1], g_sound.fSpectrum[1]);
  AnalyzeSound();
}

//-- Render -------------------------------------------------------------------
// Called once per frame. Do all rendering here.
//-----------------------------------------------------------------------------
extern "C" void Render()
{
  if (useWaveForm) {
    Vertex_t  verts[512];

#ifndef HAS_OPENGL
    unsigned stride = sizeof(Vertex_t), offset = 0;
    g_context->IASetVertexBuffers(0, 1, &g_vBuffer, &stride, &offset);
    g_context->IASetInputLayout(g_inputLayout);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    g_context->VSSetShader(g_vShader, 0, 0);
    g_context->VSSetConstantBuffers(0, 1, &g_cViewPort);
    g_context->PSSetShader(g_pShader, 0, 0);
    float xcolor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
#endif

#ifdef HAS_OPENGL
    GLenum errcode;
    //glColor3f(1.0, 1.0, 1.0);
    glColor3f(rgb[0], rgb[1], rgb[2]);
    glDisable(GL_BLEND);
    glPushMatrix();
    glTranslatef(0, 0, -1.0);
    glBegin(GL_LINE_STRIP);
#endif

    // Left (upper) channel
    for (int i = 0; i < iMaxAudioData_i; i++)
    {
#ifndef HAS_OPENGL
      //verts[i].col = D3DCOLOR_COLORVALUE(rgb[0], rgb[1], rgb[2], 1.0f);
      verts[i].col = XMFLOAT4(rgb[0], rgb[1], rgb[2], 1.0f);
#else
      //need to fix this from white, but how
      verts[i].col = 0xffffffff;
#endif
      verts[i].x = g_viewport.TopLeftX + ((i / fMaxAudioData) * g_viewport.Width);
      verts[i].y = g_viewport.TopLeftY + g_viewport.Height * 0.33f + (g_fWaveform[0][i] * g_viewport.Height * 0.15f);
      verts[i].z = 1.0;
#ifdef HAS_OPENGL
      glVertex2f(verts[i].x, verts[i].y);
#endif
    }
#ifdef HAS_OPENGL
    glEnd();
    if ((errcode = glGetError()) != GL_NO_ERROR) {
      printf("Houston, we have a GL problem: %s\n", gluErrorString(errcode));
    }
#endif


    // Right (lower) channel -problem
#ifdef HAS_OPENGL
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < iMaxAudioData_i; i++)
#else
    for (int i = iMaxAudioData_i; i < iMaxAudioData_i*2; i++) //not sure if this will work..
#endif
    {
#ifndef HAS_OPENGL
      verts[i].col = XMFLOAT4(rgb[0], rgb[1], rgb[2], 1.0f);
      verts[i].x = g_viewport.TopLeftX + ((i / fMaxAudioData) * g_viewport.Width);
#else
      //need to fix this from white? but how
      verts[i].col = 0xffffffff;
      verts[i].x = g_viewport.TopLeftX + ((i / fMaxAudioData) * g_viewport.Width);
#endif
      verts[i].y = g_viewport.TopLeftY + g_viewport.Height * 0.66f + (g_fWaveform[1][i] * g_viewport.Height * 0.15f);
      verts[i].z = 1.0;
#ifdef HAS_OPENGL
      glVertex2f(verts[i].x, verts[i].y);
#endif
    }

#ifdef HAS_OPENGL
    glEnd();
    glEnable(GL_BLEND);
    glPopMatrix();
    if ((errcode = glGetError()) != GL_NO_ERROR) {
      printf("Houston, we have a GL problem: %s\n", gluErrorString(errcode));
    }
#endif


#ifndef HAS_OPENGL
    // a little optimization: generate and send all vertecies for both channels
    D3D11_MAPPED_SUBRESOURCE res;
    if (S_OK == g_context->Map(g_vBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &res))
    {
      memcpy(res.pData, verts, sizeof(Vertex_t) * iMaxAudioData_i*2);
      g_context->Unmap(g_vBuffer, 0);
    }
    // draw left channel
    g_context->Draw(iMaxAudioData_i, 0);
    // draw right channel
    g_context->Draw(iMaxAudioData_i, iMaxAudioData_i);
#endif
  }

  //get some interesting numbers to play with
  UpdateTime();
  g_timePass = fElapsedAppTime;

}

//-- GetInfo ------------------------------------------------------------------
// Tell XBMC our requirements
//-----------------------------------------------------------------------------
extern "C" void GetInfo(VIS_INFO* pInfo)
{
  pInfo->bWantsFreq = false;
  pInfo->iSyncDelay = 0;
}

//-- OnAction -----------------------------------------------------------------
// Handle XBMC actions such as next preset, lock preset, album art changed etc
//-----------------------------------------------------------------------------
extern "C" bool OnAction(long flags, const void *param)
{
  bool ret = false;
  return ret;
}

//-- GetPresets ---------------------------------------------------------------
// Return a list of presets to XBMC for display
//-----------------------------------------------------------------------------
extern "C" unsigned int GetPresets(char ***presets)
{
  return 0;
}

//-- GetPreset ----------------------------------------------------------------
// Return the index of the current playing preset
//-----------------------------------------------------------------------------
extern "C" unsigned GetPreset()
{
  return 0;
}

//-- IsLocked -----------------------------------------------------------------
// Returns true if this add-on uses settings
//-----------------------------------------------------------------------------
extern "C" bool IsLocked()
{
  return true;
}

//-- GetSubModules ------------------------------------------------------------
// Return any sub modules supported by this vis
//-----------------------------------------------------------------------------
extern "C" unsigned int GetSubModules(char ***names)
{
  return 0; // this vis supports 0 sub modules
}

//-- Stop ---------------------------------------------------------------------
// This dll must stop all runtime activities
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" void ADDON_Stop()
{
}

//-- Detroy -------------------------------------------------------------------
// Do everything before unload of this add-on
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" void ADDON_Destroy()
{

#ifndef HAS_OPENGL
  if (g_cViewPort)
    g_cViewPort->Release();
  if (g_vBuffer)
    g_vBuffer->Release();
  if (g_inputLayout)
    g_inputLayout->Release();
  if (g_vShader)
    g_vShader->Release();
  if (g_pShader)
    g_pShader->Release();
  if (g_device)
    g_device->Release();
#endif

  //change the lights to something acceptable
  //wait a second to allow the Hue Bridge to catch up
  usleep(500);
  if(lightsOnAfter)
  {
    TurnLightsOff(activeLightIDs, numberOfActiveLights);
    if(numberOfDimmedLights>0)
    {
      TurnLightsOff(dimmedLightIDs, numberOfDimmedLights);
    }
    usleep(200);	
    TurnLightsOn(afterLightIDs, numberOfAfterLights);
    UpdateLights(afterBri, afterSat, afterHue, 30, afterLightIDs, numberOfAfterLights);
  }
  else
  {
    TurnLightsOff(activeLightIDs, numberOfActiveLights);
    if(numberOfDimmedLights>0)
    {
      TurnLightsOff(dimmedLightIDs, numberOfDimmedLights);
    }
  }

  g_fftobj.CleanUp();


  //XBMC=NULL;
}

//-- HasSettings --------------------------------------------------------------
// Returns true if this add-on use settings
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" bool ADDON_HasSettings()
{
  return true;
}

//-- GetStatus ---------------------------------------------------------------
// Returns the current Status of this visualisation
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" ADDON_STATUS ADDON_GetStatus()
{
  return ADDON_STATUS_OK;
}

//-- GetSettings --------------------------------------------------------------
// Return the settings for XBMC to display
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" unsigned int ADDON_GetSettings(ADDON_StructSetting*** sSet)
{
  return 0;
}

//-- FreeSettings --------------------------------------------------------------
// Free the settings struct passed from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------

extern "C" void ADDON_FreeSettings()
{
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from XBMC)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" ADDON_STATUS ADDON_SetSetting(const char *strSetting, const void* value)
{
  if (!strSetting || !value)
    return ADDON_STATUS_UNKNOWN;

  if (strcmp(strSetting, "UseWaveForm") == 0)
    useWaveForm = *(bool*)value == 1;
  else if (strcmp(strSetting, "HueBridgeIP") == 0)
  {
    char* array;
    array = (char*)value;
    strHueBridgeIPAddress = std::string(array);
  }
//----------------------------------------------------------  
  else if (strcmp(strSetting, "ActiveLights") == 0)
  {
    char* array;
    array = (char*)value;
    std::string activeLightIDsUnsplit = std::string(array);
    activeLightIDs.clear();
    std::string delimiter = ",";
    size_t last = 0;
    size_t next = 0;
    while ((next = activeLightIDsUnsplit.find(delimiter, last)) != std::string::npos)
    {
      activeLightIDs.push_back(activeLightIDsUnsplit.substr(last, next - last));
      last = next + 1;
    }
    //do the last light token
    activeLightIDs.push_back(activeLightIDsUnsplit.substr(last));
    numberOfActiveLights = activeLightIDs.size();
  }
  else if (strcmp(strSetting, "BeatThreshold") == 0)
    beatThreshold = *(float*)value;
  else if (strcmp(strSetting, "MaxBri") == 0)
    maxBri = *(int*)value;
  else if (strcmp(strSetting, "HueRangeUpper") == 0)
  {
    lastHue = *(int*)value;
    initialHue = lastHue;
  }
  else if (strcmp(strSetting, "HueRangeLower") == 0)
    targetHue = *(int*)value;
//----------------------------------------------------------
  else if (strcmp(strSetting, "DimmedLights") == 0)
  {
    char* array;
    array = (char*)value;
    std::string dimmedLightIDsUnsplit = std::string(array);
    dimmedLightIDs.clear();
    std::string delimiter = ",";
    size_t last = 0;
    size_t next = 0;
    while ((next = dimmedLightIDsUnsplit.find(delimiter, last)) != std::string::npos)
    {
      dimmedLightIDs.push_back(dimmedLightIDsUnsplit.substr(last, next - last));
      last = next + 1;
    }
    //do the last light token
    dimmedLightIDs.push_back(dimmedLightIDsUnsplit.substr(last));
    if(dimmedLightIDs[0].size() == 0)
    {
      numberOfDimmedLights = 0;
    }
    else
    {
      numberOfDimmedLights = dimmedLightIDs.size();
    }
  }
  else if (strcmp(strSetting, "DimmedBri") == 0)
    dimmedBri = *(int*)value;
  else if (strcmp(strSetting, "DimmedSat") == 0)
    dimmedSat = *(int*)value;
  else if (strcmp(strSetting, "DimmedHue") == 0)
    dimmedHue = *(int*)value;
//----------------------------------------------------------
  else if (strcmp(strSetting, "LightsOnAfter") == 0)
	lightsOnAfter = *(bool*)value == 1;
  else if (strcmp(strSetting, "AfterLights") == 0)
  {
    char* array;
    array = (char*)value;
    std::string afterLightIDsUnsplit = std::string(array);
    afterLightIDs.clear();
    std::string delimiter = ",";
    size_t last = 0;
    size_t next = 0;
    while ((next = afterLightIDsUnsplit.find(delimiter, last)) != std::string::npos)
    {
      afterLightIDs.push_back(afterLightIDsUnsplit.substr(last, next - last));
      last = next + 1;
    }
    //do the last light token
    afterLightIDs.push_back(afterLightIDsUnsplit.substr(last));
    numberOfAfterLights = afterLightIDs.size();
  }
  else if (strcmp(strSetting, "AfterBri") == 0)
    afterBri = *(int*)value;
  else if (strcmp(strSetting, "AfterSat") == 0)
    afterSat = *(int*)value;
  else if (strcmp(strSetting, "AfterHue") == 0)
    afterHue = *(int*)value;
//----------------------------------------------------------
  else if (strcmp(strSetting, "CuboxHDMIFix") == 0)
	cuboxHDMIFix = *(bool*)value == 1; 
  else
    return ADDON_STATUS_UNKNOWN;

  return ADDON_STATUS_OK;
}

//-- Announce -----------------------------------------------------------------
// Receive announcements from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" void ADDON_Announce(const char *flag, const char *sender, const char *message, const void *data)
{
}
