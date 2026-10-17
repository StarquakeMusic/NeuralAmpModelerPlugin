#pragma once

// Visual state for the "rig" UI: what kind of gear the loaded capture is, the accent color that follows it, and a
// few live values (output level, animation phase) that the reactive controls draw from.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "IGraphicsStructs.h"
#include "Colors.h"

// Matches the "gear_type" values written by the NAM trainer.
enum class NAMGearType
{
  Unknown,
  Amp,
  Pedal,
  PedalAmp,
  AmpCab,
  AmpPedalCab,
  Preamp,
  Studio
};

// Matches the "tone_type" values written by the NAM trainer.
enum class NAMToneType
{
  Unknown,
  Clean,
  Crunch,
  Overdrive,
  HiGain,
  Fuzz
};

// How the capture bay draws a model.
enum class NAMFaceplate
{
  Empty,
  AmpHead,
  Stompbox,
  Rack
};

// How a capture responds to a sine swept up in level, measured once when it loads.
struct NAMFingerprint
{
  bool valid = false;
  // Input levels (dBFS peak) the sweep stepped through, evenly spaced.
  float inDbLow = -48.f;
  float inDbHigh = 0.f;
  // Output level (dBFS, sine-equivalent peak) at each input step.
  std::vector<float> outDb;
  // One cycle of the output at the hottest step, normalized to +/-1.
  std::vector<float> wave;
  // Input dB per output dB over the top 24 dB of the sweep.
  float compression = 1.f;
  // Input level where the curve first bends below half slope; NaN if it never does.
  float onsetDb = NAN;
};

struct NAMGearInfo
{
  NAMGearType gearType = NAMGearType::Unknown;
  NAMToneType toneType = NAMToneType::Unknown;
  std::string name;
  std::string make;
  std::string model;
  std::string modeledBy;
  std::string fileStem;
  std::string path;
  // Trainer's 0-1 estimate of how much gain the captured gear has; negative if unknown.
  double gain = -1.0;
  NAMFingerprint fingerprint;
};

inline NAMGearType ParseGearType(const std::string& s)
{
  if (s == "amp")
    return NAMGearType::Amp;
  if (s == "pedal")
    return NAMGearType::Pedal;
  if (s == "pedal_amp")
    return NAMGearType::PedalAmp;
  if (s == "amp_cab")
    return NAMGearType::AmpCab;
  if (s == "amp_pedal_cab")
    return NAMGearType::AmpPedalCab;
  if (s == "preamp")
    return NAMGearType::Preamp;
  if (s == "studio")
    return NAMGearType::Studio;
  return NAMGearType::Unknown;
}

inline NAMToneType ParseToneType(const std::string& s)
{
  if (s == "clean")
    return NAMToneType::Clean;
  if (s == "crunch")
    return NAMToneType::Crunch;
  if (s == "overdrive")
    return NAMToneType::Overdrive;
  if (s == "hi_gain")
    return NAMToneType::HiGain;
  if (s == "fuzz")
    return NAMToneType::Fuzz;
  return NAMToneType::Unknown;
}

inline const char* GearTypeLabel(NAMGearType t)
{
  switch (t)
  {
    case NAMGearType::Amp: return "AMP";
    case NAMGearType::Pedal: return "PEDAL";
    case NAMGearType::PedalAmp: return "PEDAL + AMP";
    case NAMGearType::AmpCab: return "AMP + CAB";
    case NAMGearType::AmpPedalCab: return "PEDAL + AMP + CAB";
    case NAMGearType::Preamp: return "PREAMP";
    case NAMGearType::Studio: return "STUDIO";
    default: return "";
  }
}

inline const char* ToneTypeLabel(NAMToneType t)
{
  switch (t)
  {
    case NAMToneType::Clean: return "CLEAN";
    case NAMToneType::Crunch: return "CRUNCH";
    case NAMToneType::Overdrive: return "OVERDRIVE";
    case NAMToneType::HiGain: return "HI-GAIN";
    case NAMToneType::Fuzz: return "FUZZ";
    default: return "";
  }
}

inline bool GearIncludesCab(NAMGearType t)
{
  return t == NAMGearType::AmpCab || t == NAMGearType::AmpPedalCab;
}

inline NAMFaceplate FaceplateFor(NAMGearType t)
{
  switch (t)
  {
    case NAMGearType::Pedal: return NAMFaceplate::Stompbox;
    case NAMGearType::Preamp:
    case NAMGearType::Studio: return NAMFaceplate::Rack;
    default: return NAMFaceplate::AmpHead;
  }
}

inline iplug::igraphics::IColor AccentForTone(NAMToneType t)
{
  using iplug::igraphics::IColor;
  switch (t)
  {
    case NAMToneType::Clean: return IColor(255, 79, 209, 222);
    case NAMToneType::Crunch: return IColor(255, 242, 179, 61);
    case NAMToneType::Overdrive: return IColor(255, 255, 122, 47);
    case NAMToneType::HiGain: return IColor(255, 255, 64, 64);
    case NAMToneType::Fuzz: return IColor(255, 182, 109, 255);
    default: return PluginColors::NAM_THEMECOLOR;
  }
}

inline iplug::igraphics::IColor LerpColor(const iplug::igraphics::IColor& a, const iplug::igraphics::IColor& b,
                                          float t)
{
  auto mix = [t](int x, int y) { return static_cast<int>(std::lround(x + (y - x) * t)); };
  return iplug::igraphics::IColor(mix(a.A, b.A), mix(a.R, b.R), mix(a.G, b.G), mix(a.B, b.B));
}

// Owned by the plug-in instance and only touched on the UI thread.
struct NAMTheme
{
  bool hasModel = false;
  NAMGearInfo gear;

  iplug::igraphics::IColor accent = PluginColors::NAM_THEMECOLOR;
  iplug::igraphics::IColor targetAccent = PluginColors::NAM_THEMECOLOR;

  // Smoothed output level, 0 (silence / -60 dB) to 1 (0 dBFS).
  float level = 0.f;
  // Wraps 0-1; drives the pulses travelling along the patch cables.
  float phase = 0.f;
  // Smoothed level going into the model, in dBFS. Places the "you are here" dot on the fingerprint.
  float inputDb = -120.f;
  // Latest triggered stretch of the output, +/-1, for the scope.
  std::vector<float> scope;

  bool irLoaded = false;
  bool irActive = true;
  // File name of the second ("edge") IR the mic blends toward; empty if none.
  std::string edgeIRName;

  // Font IDs; replaced by system fonts when they're available.
  std::string scriptFont = "Michroma-Regular";
  std::string stompFont = "Michroma-Regular";

  NAMFaceplate Faceplate() const
  {
    return hasModel ? FaceplateFor(gear.gearType) : NAMFaceplate::Empty;
  }

  void SetGear(const NAMGearInfo& info)
  {
    gear = info;
    hasModel = true;
    targetAccent = AccentForTone(info.toneType);
  }

  void ClearGear()
  {
    gear = NAMGearInfo();
    hasModel = false;
    targetAccent = PluginColors::NAM_THEMECOLOR;
  }

  // Moves the accent toward its target. Returns true while it's still changing.
  bool EaseAccent(float amount)
  {
    if (accent == targetAccent)
      return false;
    auto next = LerpColor(accent, targetAccent, amount);
    accent = (next == accent) ? targetAccent : next;
    return true;
  }
};
