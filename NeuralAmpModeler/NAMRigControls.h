#pragma once

// Procedurally drawn controls for the "rig" UI: the panel, the capture bay whose faceplate follows the loaded model's
// metadata, the fingerprint and scope displays, the speaker cab with its movable mic, and the pedalboard. All of them
// read from a NAMTheme owned by the plug-in.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "IControl.h"
#include "NAMTheme.h"

using namespace iplug;
using namespace igraphics;

namespace NAMRig
{
const IColor kCream(255, 234, 223, 196);
const IColor kGold(255, 201, 178, 124);
const IColor kInk(255, 20, 19, 22);
const IColor kSilverLight(255, 178, 178, 184);
const IColor kSilverDark(255, 58, 58, 63);

// Controls in this group are redrawn from OnIdle while there's signal.
const char* const kReactiveGroup = "NAM_REACTIVE";

inline std::string ToUpper(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

// Shrinks the text down to minSize to fit maxWidth, then trims the string if it still doesn't fit.
inline std::string FitText(IGraphics& g, IText& text, const std::string& str, float maxWidth, float minSize)
{
  IRECT bounds;
  while (text.mSize > minSize && g.MeasureText(text, str.c_str(), bounds) > maxWidth)
    text.mSize -= 1.f;

  std::string fitted = str;
  while (fitted.size() > 1 && g.MeasureText(text, fitted.c_str(), bounds) > maxWidth)
  {
    fitted.pop_back();
    while (!fitted.empty() && fitted.back() == ' ')
      fitted.pop_back();
    fitted += "...";
    if (g.MeasureText(text, fitted.c_str(), bounds) <= maxWidth)
      break;
    fitted.resize(fitted.size() - 3);
  }
  return fitted;
}

inline void DrawTextShadowed(IGraphics& g, const IText& text, const char* str, const IRECT& r,
                             const IBlend* pBlend = nullptr)
{
  IText shadow = text;
  shadow.mFGColor = COLOR_BLACK.WithOpacity(0.6f);
  g.DrawText(shadow, str, r.GetTranslated(1.f, 2.f), pBlend);
  g.DrawText(text, str, r, pBlend);
}

// Soft light around a point. intensity is 0-1.
inline void DrawGlow(IGraphics& g, float cx, float cy, float r, const IColor& color, float intensity,
                     const IBlend* pBlend = nullptr)
{
  if (intensity <= 0.f || r <= 0.f)
    return;
  g.PathClear();
  g.PathCircle(cx, cy, r);
  g.PathFill(IPattern::CreateRadialGradient(
               cx, cy, r, {{color.WithOpacity(0.6f * std::min(intensity, 1.f)), 0.f}, {color.WithOpacity(0.f), 1.f}}),
             IFillOptions(), pBlend);
}

// Diagonal lines across a rect, batched into one stroke. Used for tolex and grille cloth.
inline void StrokeHatch(IGraphics& g, const IRECT& r, float spacing, const IColor& color, bool mirrored,
                        float thickness = 1.f, const IBlend* pBlend = nullptr)
{
  const float w = r.W(), h = r.H();
  g.PathClear();
  for (float d = -h; d < w; d += spacing)
  {
    const float t0 = std::max(0.f, -d);
    const float t1 = std::min(w - d, h);
    if (t1 <= t0)
      continue;
    float x0 = d + t0, x1 = d + t1;
    if (mirrored)
    {
      x0 = w - x0;
      x1 = w - x1;
    }
    g.PathMoveTo(r.L + x0, r.T + t0);
    g.PathLineTo(r.L + x1, r.T + t1);
  }
  g.PathStroke(IPattern(color), thickness, IStrokeOptions(), pBlend);
}

inline void DrawScrew(IGraphics& g, float cx, float cy, float r, float slotAngleDeg = 35.f,
                      const IBlend* pBlend = nullptr)
{
  g.PathClear();
  g.PathCircle(cx, cy, r);
  g.PathFill(IPattern::CreateLinearGradient(cx - r, cy - r, cx + r, cy + r, {{kSilverLight, 0.f}, {kSilverDark, 1.f}}),
             IFillOptions(), pBlend);
  g.DrawCircle(COLOR_BLACK.WithOpacity(0.6f), cx, cy, r, pBlend, 0.8f);
  const float a = slotAngleDeg * static_cast<float>(PI) / 180.f;
  const float dx = std::cos(a) * r * 0.7f, dy = std::sin(a) * r * 0.7f;
  g.DrawLine(IColor(255, 28, 28, 30), cx - dx, cy - dy, cx + dx, cy + dy, pBlend, 1.2f);
}

// A small outlined pill with a label. Returns the x position just right of it.
inline float DrawTag(IGraphics& g, float x, float cy, const char* label, const IColor& color,
                     const IBlend* pBlend = nullptr)
{
  if (!label || !label[0])
    return x;
  const IText text(8.f, color, "Michroma-Regular", EAlign::Center, EVAlign::Middle);
  IRECT bounds;
  const float w = g.MeasureText(text, label, bounds) + 14.f;
  const IRECT r(x, cy - 8.f, x + w, cy + 8.f);
  g.FillRoundRect(color.WithOpacity(0.14f), r, 8.f, pBlend);
  g.DrawRoundRect(color.WithOpacity(0.6f), r, 8.f, pBlend, 1.f);
  g.DrawText(text, label, r, pBlend);
  return r.R + 6.f;
}

inline std::string CapturedByLine(const NAMGearInfo& gear)
{
  return gear.modeledBy.empty() ? std::string() : "captured by " + gear.modeledBy;
}

// Ten-segment bar showing the trainer's gain estimate. Returns the x position just right of it.
inline float DrawGainBar(IGraphics& g, float x, float cy, double gain, const IColor& onColor, const IColor& labelColor,
                         const IBlend* pBlend = nullptr)
{
  if (gain < 0.0)
    return x;
  const IText text(8.f, labelColor, "Michroma-Regular", EAlign::Near, EVAlign::Middle);
  g.DrawText(text, "GAIN", IRECT(x, cy - 8.f, x + 40.f, cy + 8.f), pBlend);
  const int lit = static_cast<int>(std::round(std::clamp(gain, 0.0, 1.0) * 10.0));
  for (int i = 0; i < 10; i++)
  {
    const IRECT seg(x + 36.f + i * 6.f, cy - 5.f, x + 36.f + i * 6.f + 4.f, cy + 5.f);
    g.FillRoundRect(i < lit ? onColor : labelColor.WithOpacity(0.15f), seg, 1.f, pBlend);
  }
  return x + 96.f + 6.f;
}

// Right-aligned "captured by" credit, skipped if there's no room for it.
inline void DrawCredit(IGraphics& g, const NAMGearInfo& gear, const IRECT& area, const IColor& color)
{
  const std::string by = CapturedByLine(gear);
  if (by.empty() || area.W() < 40.f)
    return;
  IText text(10.f, color, nullptr, EAlign::Far, EVAlign::Middle);
  const std::string fitted = FitText(g, text, by, area.W(), 8.f);
  g.DrawText(text, fitted.c_str(), area);
}

// Brand on the big line; model (or name) underneath.
inline void GetTitleLines(const NAMGearInfo& gear, std::string& primary, std::string& secondary)
{
  const std::string* candidates[] = {&gear.make, &gear.model, &gear.name, &gear.fileStem};
  primary.clear();
  for (auto* c : candidates)
    if (!c->empty())
    {
      primary = *c;
      break;
    }

  secondary.clear();
  if (primary == gear.make)
    secondary = !gear.model.empty() ? gear.model : gear.name;
  else if (primary == gear.model && gear.name != gear.model)
    secondary = gear.name;
}

// Window background and frame, shared by the main page and the overlays.
inline void DrawPanelBackdrop(IGraphics& g, const IRECT& r, const NAMTheme& theme, const IBlend* pBlend = nullptr)
{
  g.PathClear();
  g.PathRect(r);
  g.PathFill(IPattern::CreateLinearGradient(
               r, EDirection::Vertical, {{IColor(255, 30, 29, 33), 0.f}, {IColor(255, 11, 11, 13), 1.f}}),
             IFillOptions(), pBlend);

  // The accent washes in behind the gear so each capture tints the room a little.
  DrawGlow(g, r.L + r.W() * 0.4f, r.T + r.H() * 0.32f, r.W() * 0.55f, theme.accent, 0.16f, pBlend);

  const IRECT frame = r.GetPadded(-8.f);
  g.DrawRoundRect(COLOR_BLACK.WithOpacity(0.7f), frame.GetPadded(1.f), 15.f, pBlend, 1.f);
  g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.07f), frame, 14.f, pBlend, 1.f);
  const float s = 14.f;
  DrawScrew(g, frame.L + s, frame.T + s, 4.f, 20.f, pBlend);
  DrawScrew(g, frame.R - s, frame.T + s, 4.f, 70.f, pBlend);
  DrawScrew(g, frame.L + s, frame.B - s, 4.f, 110.f, pBlend);
  DrawScrew(g, frame.R - s, frame.B - s, 4.f, 160.f, pBlend);
}

// Window background with the header.
class PanelControl : public IControl
{
public:
  PanelControl(const IRECT& bounds, const NAMTheme& theme, bool showHeader)
  : IControl(bounds)
  , mTheme(theme)
  , mShowHeader(showHeader)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    DrawPanelBackdrop(g, mRECT, mTheme);
    if (!mShowHeader)
      return;

    const IRECT header(mRECT.L + 32.f, mRECT.T + 22.f, mRECT.R - 80.f, mRECT.T + 58.f);
    const IText logo(15.f, kCream, "Michroma-Regular", EAlign::Near, EVAlign::Middle);
    DrawTextShadowed(g, logo, "NEURAL AMP MODELER", header);
    IRECT bounds;
    const float w = g.MeasureText(logo, "NEURAL AMP MODELER", bounds);
    const float dotX = header.L + w + 14.f;
    DrawGlow(g, dotX, header.MH(), 9.f, mTheme.accent, 0.8f);
    g.FillCircle(mTheme.accent, dotX, header.MH(), 2.5f);
    const IText sub(9.f, kCream.WithOpacity(0.45f), "Michroma-Regular", EAlign::Near, EVAlign::Middle);
    g.DrawText(sub, "THE RIG", IRECT(dotX + 10.f, header.T, header.R, header.B));
  }

private:
  const NAMTheme& mTheme;
  bool mShowHeader;
};

// The hero: draws whatever the loaded capture is.
class CaptureBayControl : public IControl
{
public:
  CaptureBayControl(const IRECT& bounds, const NAMTheme& theme)
  : IControl(bounds)
  , mTheme(theme)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    switch (mTheme.Faceplate())
    {
      case NAMFaceplate::Empty: DrawEmpty(g); break;
      case NAMFaceplate::AmpHead: DrawAmpHead(g); break;
      case NAMFaceplate::Stompbox: DrawStompbox(g); break;
      case NAMFaceplate::Rack: DrawRack(g); break;
    }
  }

private:
  void DrawEmpty(IGraphics& g)
  {
    const IRECT f = mRECT;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.35f), f, 12.f);
    g.DrawRoundRect(mTheme.accent.WithOpacity(0.3f), f.GetPadded(-1.f), 12.f, nullptr, 1.f);
    g.DrawDottedRect(kCream.WithOpacity(0.15f), f.GetPadded(-12.f), nullptr, 1.f, 4.f);

    const IText title(16.f, kCream.WithOpacity(0.75f), "Michroma-Regular", EAlign::Center, EVAlign::Middle);
    const IText sub(12.f, kCream.WithOpacity(0.45f), nullptr, EAlign::Center, EVAlign::Middle);
    g.DrawText(title, "NO CAPTURE LOADED", f.GetVShifted(-12.f));
    g.DrawText(sub, "Load a .nam file below: amps, pedals, preamps, studio gear", f.GetVShifted(16.f));
  }

  void DrawAmpHead(IGraphics& g)
  {
    const NAMGearInfo& gear = mTheme.gear;
    const IRECT f = mRECT;
    const float rad = 12.f;

    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.5f), f.GetTranslated(0.f, 3.f), rad);
    g.FillRoundRect(IColor(255, 24, 23, 26), f, rad);
    // Tolex
    StrokeHatch(g, f.GetPadded(-5.f), 3.f, COLOR_WHITE.WithOpacity(0.035f), false);
    StrokeHatch(g, f.GetPadded(-5.f), 3.f, COLOR_BLACK.WithOpacity(0.25f), true);
    g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.08f), f.GetPadded(-0.5f), rad, nullptr, 1.f);

    const IRECT piping = f.GetPadded(-12.f);
    g.DrawRoundRect(COLOR_BLACK.WithOpacity(0.5f), piping.GetTranslated(0.f, 1.f), 8.f, nullptr, 1.5f);
    g.DrawRoundRect(kGold.WithOpacity(0.8f), piping, 8.f, nullptr, 1.5f);

    // Corner protectors
    const float cp = 22.f;
    const IRECT corners[] = {IRECT(f.L, f.T, f.L + cp, f.T + cp), IRECT(f.R - cp, f.T, f.R, f.T + cp),
                             IRECT(f.L, f.B - cp, f.L + cp, f.B), IRECT(f.R - cp, f.B - cp, f.R, f.B)};
    for (const auto& c : corners)
    {
      g.FillRoundRect(IColor(255, 10, 10, 11), c, 5.f);
      g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.1f), c.GetPadded(-0.5f), 5.f, nullptr, 1.f);
      g.FillCircle(kSilverDark, c.MW(), c.MH(), 2.f);
    }

    // Control strip along the bottom, like the panel on an amp head
    const IRECT strip = piping.GetPadded(-8.f).GetFromBottom(40.f);
    g.PathClear();
    g.PathRoundRect(strip, 5.f);
    g.PathFill(IPattern::CreateLinearGradient(
      strip, EDirection::Vertical, {{IColor(255, 46, 43, 38), 0.f}, {IColor(255, 24, 23, 21), 1.f}}));
    g.DrawLine(kGold.WithOpacity(0.5f), strip.L + 6.f, strip.T, strip.R - 6.f, strip.T);

    // Jewel pilot lamp, brighter with signal
    const float jx = strip.R - 22.f, jy = strip.MH();
    DrawGlow(g, jx, jy, 10.f + 26.f * mTheme.level, mTheme.accent, 0.5f + 0.5f * mTheme.level);
    g.FillCircle(LerpColor(mTheme.accent, COLOR_BLACK, 0.35f - 0.3f * mTheme.level), jx, jy, 7.f);
    g.FillCircle(COLOR_WHITE.WithOpacity(0.55f), jx - 2.5f, jy - 2.5f, 2.f);
    g.DrawCircle(kSilverLight, jx, jy, 9.f, nullptr, 2.f);

    float x = DrawTag(g, strip.L + 12.f, strip.MH(), GearTypeLabel(gear.gearType), kGold);
    x = DrawTag(g, x, strip.MH(), ToneTypeLabel(gear.toneType), mTheme.accent);
    if (gear.gain >= 0.0)
      x = DrawGainBar(g, std::max(x + 6.f, strip.MW() - 48.f), strip.MH(), gear.gain, mTheme.accent, kCream);
    DrawCredit(g, gear, IRECT(x + 4.f, strip.T, jx - 18.f, strip.B), kCream.WithOpacity(0.6f));

    // Nameplate
    std::string primary, secondary;
    GetTitleLines(gear, primary, secondary);
    const IRECT plate(piping.L + 24.f, piping.T + 4.f, piping.R - 24.f, strip.T - 4.f);
    const bool hasSecondary = !secondary.empty();
    const IRECT primaryArea = hasSecondary ? plate.GetFromTop(plate.H() * 0.68f) : plate;
    IText brand(42.f, kCream, mTheme.scriptFont.c_str(), EAlign::Center, EVAlign::Middle);
    const std::string fittedPrimary = FitText(g, brand, primary, primaryArea.W(), 14.f);
    DrawTextShadowed(g, brand, fittedPrimary.c_str(), primaryArea);

    if (hasSecondary)
    {
      IText model(11.f, kGold, "Michroma-Regular", EAlign::Center, EVAlign::Middle);
      const std::string fitted = FitText(g, model, ToUpper(secondary), plate.W(), 8.f);
      g.DrawText(model, fitted.c_str(), plate.GetFromBottom(plate.H() * 0.32f));
    }
  }

  void DrawStompbox(IGraphics& g)
  {
    const NAMGearInfo& gear = mTheme.gear;
    const IRECT box = mRECT.GetPadded(-2.f);
    const float rad = 18.f;

    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.5f), box.GetTranslated(0.f, 4.f), rad);
    g.PathClear();
    g.PathRoundRect(box, rad);
    g.PathFill(IPattern::CreateLinearGradient(box, EDirection::Vertical,
                                              {{LerpColor(mTheme.accent, COLOR_BLACK, 0.45f), 0.f},
                                               {LerpColor(mTheme.accent, COLOR_BLACK, 0.78f), 1.f}}));
    // Enamel sheen and edge
    g.FillRoundRect(COLOR_WHITE.WithOpacity(0.06f), box.GetFromTop(box.H() * 0.45f), rad, rad, 0.f, 0.f);
    g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.18f), box.GetPadded(-1.f), rad - 1.f, nullptr, 1.f);

    const float s = 14.f;
    DrawScrew(g, box.L + s, box.T + s, 5.f, 30.f);
    DrawScrew(g, box.R - s, box.T + s, 5.f, 120.f);
    DrawScrew(g, box.L + s, box.B - s, 5.f, 75.f);
    DrawScrew(g, box.R - s, box.B - s, 5.f, 160.f);

    // LED and footswitch
    const IRECT rightCol = box.GetFromRight(130.f);
    const float lx = rightCol.MW(), ly = box.T + 30.f;
    g.FillCircle(kSilverDark, lx, ly, 7.5f);
    DrawGlow(g, lx, ly, 8.f + 24.f * mTheme.level, mTheme.accent, 0.6f + 0.4f * mTheme.level);
    g.FillCircle(LerpColor(mTheme.accent, COLOR_WHITE, 0.15f + 0.35f * mTheme.level), lx, ly, 5.f);
    g.FillCircle(COLOR_WHITE.WithOpacity(0.6f), lx - 1.5f, ly - 1.5f, 1.5f);

    const float fx = rightCol.MW(), fy = box.MH() + 22.f;
    g.FillCircle(COLOR_BLACK.WithOpacity(0.45f), fx + 1.f, fy + 3.f, 27.f);
    g.PathClear();
    g.PathCircle(fx, fy, 26.f);
    g.PathFill(IPattern::CreateLinearGradient(fx, fy - 26.f, fx, fy + 26.f,
                                              {{IColor(255, 120, 120, 126), 0.f}, {IColor(255, 40, 40, 44), 1.f}}));
    g.PathClear();
    g.PathCircle(fx, fy, 18.f);
    g.PathFill(IPattern::CreateLinearGradient(fx, fy - 18.f, fx, fy + 18.f, {{kSilverLight, 0.f}, {kSilverDark, 1.f}}));
    g.PathClear();
    g.PathCircle(fx, fy, 12.f);
    g.PathFill(IPattern::CreateLinearGradient(
      fx, fy - 12.f, fx, fy + 12.f, {{IColor(255, 90, 90, 96), 0.f}, {IColor(255, 210, 210, 216), 1.f}}));
    g.DrawCircle(COLOR_BLACK.WithOpacity(0.5f), fx, fy, 18.f, nullptr, 1.f);

    // Silkscreened label
    const IRECT label = box.GetReducedFromRight(130.f).GetPadded(-24.f, -22.f, 0.f, -22.f);
    g.FillRoundRect(kCream.WithOpacity(0.94f), label, 8.f);
    g.DrawRoundRect(LerpColor(mTheme.accent, COLOR_BLACK, 0.6f), label.GetPadded(-4.f), 5.f, nullptr, 1.f);

    std::string primary, secondary;
    GetTitleLines(gear, primary, secondary);
    const IRECT inner = label.GetPadded(-14.f, -8.f, -14.f, -8.f);
    IText big(30.f, kInk, mTheme.stompFont.c_str(), EAlign::Center, EVAlign::Middle);
    const std::string fittedPrimary = FitText(g, big, ToUpper(primary), inner.W(), 12.f);
    g.DrawText(big, fittedPrimary.c_str(), inner.GetFromTop(inner.H() * 0.5f));
    if (!secondary.empty())
    {
      IText small(10.f, kInk.WithOpacity(0.75f), "Michroma-Regular", EAlign::Center, EVAlign::Middle);
      const std::string fitted = FitText(g, small, ToUpper(secondary), inner.W(), 8.f);
      g.DrawText(small, fitted.c_str(), inner.GetFromTop(inner.H() * 0.5f).GetVShifted(inner.H() * 0.3f));
    }

    const float tagY = inner.B - 10.f;
    float x = DrawTag(g, inner.L, tagY, ToneTypeLabel(gear.toneType), kInk);
    x = DrawGainBar(g, x + 4.f, tagY, gear.gain, LerpColor(mTheme.accent, COLOR_BLACK, 0.2f), kInk);
    DrawCredit(g, gear, IRECT(x + 4.f, tagY - 8.f, inner.R, tagY + 8.f), kInk.WithOpacity(0.6f));
  }

  void DrawRack(IGraphics& g)
  {
    const NAMGearInfo& gear = mTheme.gear;
    const IRECT f = mRECT.GetPadded(0.f, -6.f, 0.f, -6.f);
    const float earW = 26.f;

    g.FillRect(COLOR_BLACK.WithOpacity(0.5f), f.GetTranslated(0.f, 3.f));
    // Rack ears with screws sitting in their slots
    for (const IRECT& ear : {f.GetFromLeft(earW), f.GetFromRight(earW)})
    {
      g.PathClear();
      g.PathRect(ear);
      g.PathFill(IPattern::CreateLinearGradient(
        ear, EDirection::Vertical, {{IColor(255, 64, 65, 70), 0.f}, {IColor(255, 36, 37, 41), 1.f}}));
      for (float cy : {ear.T + 26.f, ear.B - 26.f})
      {
        g.FillRoundRect(COLOR_BLACK, IRECT(ear.MW() - 7.f, cy - 5.f, ear.MW() + 7.f, cy + 5.f), 5.f);
        DrawScrew(g, ear.MW(), cy, 4.f, 90.f);
      }
    }

    // Brushed, anodized faceplate
    const IRECT face = f.GetReducedFromLeft(earW).GetReducedFromRight(earW);
    g.PathClear();
    g.PathRect(face);
    g.PathFill(IPattern::CreateLinearGradient(
      face, EDirection::Vertical, {{IColor(255, 60, 62, 68), 0.f}, {IColor(255, 34, 35, 39), 1.f}}));
    const float alphas[] = {0.035f, 0.06f, 0.02f};
    for (int pass = 0; pass < 3; pass++)
    {
      g.PathClear();
      for (float y = face.T + 1.f + pass; y < face.B; y += 3.f)
      {
        g.PathMoveTo(face.L, y);
        g.PathLineTo(face.R, y);
      }
      g.PathStroke(IPattern(COLOR_WHITE.WithOpacity(alphas[pass])), 1.f);
    }
    g.DrawRect(COLOR_BLACK.WithOpacity(0.6f), face);
    g.DrawLine(COLOR_WHITE.WithOpacity(0.15f), face.L, face.T + 0.5f, face.R, face.T + 0.5f);

    // VU meter
    const IRECT vu = face.GetPadded(-14.f).GetFromRight(150.f);
    g.FillRoundRect(IColor(255, 14, 14, 16), vu, 6.f);
    const IRECT dial = vu.GetPadded(-6.f);
    g.PathClear();
    g.PathRoundRect(dial, 4.f);
    g.PathFill(IPattern::CreateLinearGradient(
      dial, EDirection::Vertical, {{IColor(255, 243, 233, 205), 0.f}, {IColor(255, 214, 196, 152), 1.f}}));
    g.FillRoundRect(mTheme.accent.WithOpacity(0.12f + 0.12f * mTheme.level), dial, 4.f);

    const float px = dial.MW(), py = dial.B - 12.f;
    const float radius = dial.H() * 0.68f;
    g.DrawArc(kInk, px, py, radius, -50.f, 50.f, nullptr, 1.2f);
    g.DrawArc(IColor(255, 200, 40, 40), px, py, radius + 2.f, 25.f, 50.f, nullptr, 4.f);
    for (int k = 0; k <= 10; k++)
    {
      const float a = -50.f + 10.f * k;
      g.DrawRadialLine(kInk, px, py, a, radius, radius + (k % 5 == 0 ? 8.f : 4.f), nullptr, 1.f);
    }
    g.DrawText(IText(11.f, kInk, "Michroma-Regular", EAlign::Center, EVAlign::Middle), "VU",
               IRECT(px - 20.f, py - radius * 0.5f - 8.f, px + 20.f, py - radius * 0.5f + 8.f));
    const float dB = mTheme.level * 60.f - 60.f;
    const float needle = std::clamp((dB + 36.f) / 36.f, 0.f, 1.f);
    g.DrawRadialLine(IColor(255, 25, 25, 25), px, py, -50.f + 100.f * needle, 0.f, radius + 6.f, nullptr, 1.5f);
    g.FillCircle(kInk, px, py, 5.f);
    g.FillRoundRect(COLOR_WHITE.WithOpacity(0.1f), dial.GetFromTop(dial.H() * 0.45f), 4.f, 4.f, 0.f, 0.f);

    // Silkscreen
    const IRECT text = face.GetPadded(-18.f).GetReducedFromRight(165.f);
    std::string primary, secondary;
    GetTitleLines(gear, primary, secondary);
    IText big(20.f, IColor(255, 236, 236, 240), "Michroma-Regular", EAlign::Near, EVAlign::Middle);
    const std::string fittedPrimary = FitText(g, big, ToUpper(primary), text.W(), 10.f);
    g.DrawText(big, fittedPrimary.c_str(), IRECT(text.L, text.T, text.R, text.T + 34.f));
    if (!secondary.empty())
    {
      IText small(10.f, IColor(255, 200, 200, 206), "Michroma-Regular", EAlign::Near, EVAlign::Middle);
      const std::string fitted = FitText(g, small, ToUpper(secondary), text.W(), 8.f);
      g.DrawText(small, fitted.c_str(), IRECT(text.L, text.T + 34.f, text.R, text.T + 52.f));
    }

    const float tagY = text.T + 72.f;
    float x = DrawTag(g, text.L, tagY, GearTypeLabel(gear.gearType), kCream);
    x = DrawTag(g, x, tagY, ToneTypeLabel(gear.toneType), mTheme.accent);
    DrawGainBar(g, text.L, tagY + 24.f, gear.gain, mTheme.accent, kCream);

    // Signal LED and credit along the bottom
    const float ly = text.B - 6.f;
    DrawGlow(g, text.L + 5.f, ly, 6.f + 14.f * mTheme.level, mTheme.accent, mTheme.level);
    g.FillCircle(LerpColor(IColor(255, 40, 40, 44), mTheme.accent, 0.3f + 0.7f * mTheme.level), text.L + 5.f, ly, 3.5f);
    g.DrawText(IText(8.f, kCream.WithOpacity(0.6f), "Michroma-Regular", EAlign::Near, EVAlign::Middle), "SIGNAL",
               IRECT(text.L + 14.f, ly - 8.f, text.L + 80.f, ly + 8.f));
    DrawCredit(g, gear, IRECT(text.L + 80.f, ly - 8.f, text.R, ly + 8.f), kCream.WithOpacity(0.6f));
  }

  const NAMTheme& mTheme;
};

// A cubic Bezier, for patch cables and mic leads.
struct Cable
{
  float x0, y0, x1, y1, x2, y2, x3, y3;

  void Point(float t, float& x, float& y) const
  {
    const float u = 1.f - t;
    const float a = u * u * u, b = 3.f * u * u * t, c = 3.f * u * t * t, d = t * t * t;
    x = a * x0 + b * x1 + c * x2 + d * x3;
    y = a * y0 + b * y1 + c * y2 + d * y3;
  }

  float Length() const
  {
    float len = 0.f, px = x0, py = y0;
    for (int i = 1; i <= 16; i++)
    {
      float x, y;
      Point(i / 16.f, x, y);
      len += std::hypot(x - px, y - py);
      px = x;
      py = y;
    }
    return len;
  }

  // A cable hanging between two side-facing jacks.
  static Cable Sag(float xa, float ya, float xb, float yb, float sag)
  {
    const float dx = (xb - xa) * 0.35f;
    return {xa, ya, xa + dx, ya + sag, xb - dx, yb + sag, xb, yb};
  }
};

inline void DrawCable(IGraphics& g, const Cable& c, float thickness = 5.f)
{
  auto path = [&](float dy) {
    g.PathClear();
    g.PathMoveTo(c.x0, c.y0 + dy);
    g.PathCubicBezierTo(c.x1, c.y1 + dy, c.x2, c.y2 + dy, c.x3, c.y3 + dy);
  };
  path(3.f);
  g.PathStroke(IPattern(COLOR_BLACK.WithOpacity(0.45f)), thickness + 1.f);
  path(0.f);
  g.PathStroke(IPattern(IColor(255, 16, 16, 18)), thickness);
  path(-thickness * 0.2f);
  g.PathStroke(IPattern(COLOR_WHITE.WithOpacity(0.09f)), thickness * 0.3f);
}

// Signal pulses running along a cable. They wrap seamlessly as the theme's phase wraps.
inline void DrawCablePulses(IGraphics& g, const Cable& c, const NAMTheme& theme, float spacing = 46.f)
{
  const float lvl = theme.level;
  const int n = std::max(1, static_cast<int>(c.Length() / spacing));
  const float laps = 4.f; // Pulses advance this many spacings per phase cycle, whatever the cable's length.
  for (int i = 0; i < n; i++)
  {
    const float t = std::fmod((static_cast<float>(i) + laps * theme.phase) / n, 1.f);
    float x, y;
    c.Point(t, x, y);
    const float edge = std::min(1.f, std::min(t, 1.f - t) * 8.f);
    const float a = (0.15f + 0.85f * lvl) * edge;
    DrawGlow(g, x, y, 5.f + 6.f * lvl, theme.accent, a);
    g.FillCircle(LerpColor(theme.accent, COLOR_WHITE, 0.4f).WithOpacity(a), x, y, 1.3f + lvl);
  }
}

// The sleeve and boot of a 1/4" plug sitting in a jack at (x, y). dir is +1 when the cable leaves to the right.
// Returns where the cable starts.
inline float DrawPlug(IGraphics& g, float x, float y, float dir)
{
  const IRECT sleeve = dir > 0 ? IRECT(x, y - 4.f, x + 11.f, y + 4.f) : IRECT(x - 11.f, y - 4.f, x, y + 4.f);
  g.PathClear();
  g.PathRoundRect(sleeve, 2.f);
  g.PathFill(IPattern::CreateLinearGradient(sleeve, EDirection::Vertical, {{kSilverLight, 0.f}, {kSilverDark, 1.f}}));
  const IRECT boot =
    dir > 0 ? IRECT(sleeve.R, y - 3.5f, sleeve.R + 9.f, y + 3.5f) : IRECT(sleeve.L - 9.f, y - 3.5f, sleeve.L, y + 3.5f);
  g.FillRoundRect(IColor(255, 10, 10, 11), boot, 2.f);
  return x + dir * 20.f;
}

// Small text-only button.
class PillButtonControl : public IControl
{
public:
  PillButtonControl(const IRECT& bounds, const char* label, const NAMTheme& theme, IActionFunction action)
  : IControl(bounds, action)
  , mLabel(label)
  , mTheme(theme)
  {
  }

  void Draw(IGraphics& g) override
  {
    const float rad = mRECT.H() * 0.5f;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.5f), mRECT, rad, &mBlend);
    g.DrawRoundRect(mTheme.accent.WithOpacity(mMouseIsOver ? 0.9f : 0.45f), mRECT, rad, &mBlend, 1.f);
    if (mMouseIsOver)
      g.FillRoundRect(PluginColors::MOUSEOVER, mRECT, rad, &mBlend);
    g.DrawText(IText(8.f, kCream.WithOpacity(0.9f), "Michroma-Regular", EAlign::Center, EVAlign::Middle),
               mLabel.c_str(), mRECT, &mBlend);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override {}

  // Act on release so that any dialog the action opens doesn't swallow the mouse-up.
  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    if (mRECT.Contains(x, y) && GetActionFunction())
      GetActionFunction()(this);
  }

private:
  std::string mLabel;
  const NAMTheme& mTheme;
};

// Phosphor-style scope of the plug-in's output, in the header.
class ScopeControl : public IControl
{
public:
  ScopeControl(const IRECT& bounds, const NAMTheme& theme)
  : IControl(bounds)
  , mTheme(theme)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    const IRECT bezel = mRECT;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.65f), bezel, 7.f);
    g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.07f), bezel.GetPadded(-0.5f), 7.f, nullptr, 1.f);
    const IRECT screen = bezel.GetPadded(-4.f);
    g.FillRoundRect(IColor(255, 7, 9, 9), screen, 4.f);
    g.FillRoundRect(mTheme.accent.WithOpacity(0.05f), screen, 4.f);

    g.PathClear();
    for (int i = 1; i < 10; i++)
    {
      const float x = screen.L + screen.W() * i / 10.f;
      g.PathMoveTo(x, screen.T + 2.f);
      g.PathLineTo(x, screen.B - 2.f);
    }
    for (float f : {0.25f, 0.75f})
    {
      g.PathMoveTo(screen.L + 2.f, screen.T + screen.H() * f);
      g.PathLineTo(screen.R - 2.f, screen.T + screen.H() * f);
    }
    g.PathStroke(IPattern(mTheme.accent.WithOpacity(0.07f)), 1.f);
    g.DrawLine(mTheme.accent.WithOpacity(0.16f), screen.L + 2.f, screen.MH(), screen.R - 2.f, screen.MH());

    const auto& pts = mTheme.scope;
    const float amp = screen.H() * 0.42f;
    auto trace = [&]() {
      g.PathClear();
      if (pts.size() < 2 || mTheme.level <= 0.f)
      {
        g.PathMoveTo(screen.L + 3.f, screen.MH());
        g.PathLineTo(screen.R - 3.f, screen.MH());
        return;
      }
      const float w = screen.W() - 6.f;
      for (size_t i = 0; i < pts.size(); i++)
      {
        const float x = screen.L + 3.f + w * i / (pts.size() - 1);
        const float y = screen.MH() - amp * std::clamp(pts[i], -1.1f, 1.1f);
        if (i == 0)
          g.PathMoveTo(x, y);
        else
          g.PathLineTo(x, y);
      }
    };
    trace();
    g.PathStroke(IPattern(mTheme.accent.WithOpacity(0.2f)), 4.f);
    trace();
    g.PathStroke(IPattern(LerpColor(mTheme.accent, COLOR_WHITE, 0.25f).WithOpacity(0.5f + 0.5f * mTheme.level)), 1.3f);

    g.DrawText(IText(7.f, mTheme.accent.WithOpacity(0.6f), "Michroma-Regular", EAlign::Near, EVAlign::Top), "OUT",
               screen.GetPadded(-5.f, -3.f, 0.f, 0.f));
    g.FillRoundRect(COLOR_WHITE.WithOpacity(0.03f), screen.GetFromTop(screen.H() * 0.45f), 4.f, 4.f, 0.f, 0.f);
  }

private:
  const NAMTheme& mTheme;
};

// How the loaded capture compresses and clips, measured when it loaded, with a dot riding the curve at the
// current input level.
class FingerprintControl : public IControl
{
public:
  FingerprintControl(const IRECT& bounds, const NAMTheme& theme)
  : IControl(bounds)
  , mTheme(theme)
  {
    mIgnoreMouse = true;
    SetTooltip("Input level vs. output level for the loaded capture, plus one cycle of its output when driven hard. "
               "The dot shows where you're playing.");
  }

  void Draw(IGraphics& g) override
  {
    const IRECT r = mRECT;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.5f), r.GetTranslated(0.f, 3.f), 12.f);
    g.FillRoundRect(IColor(255, 22, 21, 24), r, 12.f);
    g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.08f), r.GetPadded(-0.5f), 12.f, nullptr, 1.f);
    g.DrawText(IText(8.f, kGold, "Michroma-Regular", EAlign::Near, EVAlign::Middle), "FINGERPRINT",
               IRECT(r.L + 14.f, r.T + 8.f, r.R, r.T + 22.f));

    const IRECT screen = r.GetPadded(-10.f, -26.f, -10.f, -10.f);
    g.FillRoundRect(IColor(255, 8, 9, 10), screen, 6.f);
    g.FillRoundRect(mTheme.accent.WithOpacity(0.04f), screen, 6.f);

    const IRECT plot = screen.GetPadded(-8.f, -8.f, -8.f, -60.f);
    const IRECT wave = IRECT(plot.L, plot.B + 10.f, plot.R, plot.B + 34.f);
    const IRECT stats = IRECT(plot.L, wave.B + 4.f, plot.R, screen.B - 4.f);

    g.PathClear();
    for (int i = 0; i <= 4; i++)
    {
      const float x = plot.L + plot.W() * i / 4.f;
      g.PathMoveTo(x, plot.T);
      g.PathLineTo(x, plot.B);
    }
    for (int i = 0; i <= 3; i++)
    {
      const float y = plot.T + plot.H() * i / 3.f;
      g.PathMoveTo(plot.L, y);
      g.PathLineTo(plot.R, y);
    }
    g.PathStroke(IPattern(mTheme.accent.WithOpacity(0.08f)), 1.f);
    g.DrawLine(mTheme.accent.WithOpacity(0.15f), wave.L, wave.MH(), wave.R, wave.MH());

    const NAMFingerprint& fp = mTheme.gear.fingerprint;
    const IText small(9.f, kCream.WithOpacity(0.45f), nullptr, EAlign::Center, EVAlign::Middle);
    if (!mTheme.hasModel || !fp.valid || fp.outDb.size() < 2)
    {
      g.DrawText(small, mTheme.hasModel ? "NOT MEASURED" : "NO CAPTURE", plot);
      return;
    }

    const size_t n = fp.outDb.size();
    float lo = *std::min_element(fp.outDb.begin(), fp.outDb.end());
    float hi = *std::max_element(fp.outDb.begin(), fp.outDb.end());
    if (hi - lo < 12.f)
      lo = hi - 12.f;
    const float span = hi - lo;
    auto px = [&](float inDb) { return plot.L + plot.W() * (inDb - fp.inDbLow) / (fp.inDbHigh - fp.inDbLow); };
    auto py = [&](float outDb) { return plot.B - plot.H() * (outDb - lo) / span; };
    auto inDbAt = [&](size_t i) { return fp.inDbLow + (fp.inDbHigh - fp.inDbLow) * i / (n - 1); };

    // A straight wire would follow this line; the further the curve sags under it, the more it compresses.
    g.PathClipRegion(plot);
    g.DrawDottedLine(kCream.WithOpacity(0.25f), px(fp.inDbLow), py(fp.outDb[0]), px(fp.inDbHigh),
                     py(fp.outDb[0] + (fp.inDbHigh - fp.inDbLow)), nullptr, 1.f, 3.f);

    g.PathClear();
    g.PathMoveTo(px(fp.inDbLow), plot.B);
    for (size_t i = 0; i < n; i++)
      g.PathLineTo(px(inDbAt(i)), py(fp.outDb[i]));
    g.PathLineTo(px(fp.inDbHigh), plot.B);
    g.PathClose();
    g.PathFill(IPattern::CreateLinearGradient(plot, EDirection::Vertical,
                                              {{mTheme.accent.WithOpacity(0.28f), 0.f}, {mTheme.accent.WithOpacity(0.f), 1.f}}));
    g.PathClear();
    for (size_t i = 0; i < n; i++)
    {
      if (i == 0)
        g.PathMoveTo(px(inDbAt(i)), py(fp.outDb[i]));
      else
        g.PathLineTo(px(inDbAt(i)), py(fp.outDb[i]));
    }
    g.PathStroke(IPattern(mTheme.accent), 2.f);

    // You are here
    if (mTheme.level > 0.f && mTheme.inputDb > fp.inDbLow - 6.f)
    {
      const float inDb = std::clamp(mTheme.inputDb, fp.inDbLow, fp.inDbHigh);
      const float f = (inDb - fp.inDbLow) / (fp.inDbHigh - fp.inDbLow) * (n - 1);
      const size_t i0 = std::min(static_cast<size_t>(f), n - 2);
      const float outDb = fp.outDb[i0] + (fp.outDb[i0 + 1] - fp.outDb[i0]) * (f - i0);
      const float x = px(inDb), y = py(outDb);
      DrawGlow(g, x, y, 12.f, mTheme.accent, 0.9f);
      g.FillCircle(LerpColor(mTheme.accent, COLOR_WHITE, 0.5f), x, y, 3.f);
    }
    g.PathClipRegion();

    // Axis names in the corners the curve stays out of
    g.DrawText(IText(8.f, kCream.WithOpacity(0.35f), nullptr, EAlign::Far, EVAlign::Bottom), "IN",
               plot.GetPadded(-3.f));
    g.DrawText(IText(8.f, kCream.WithOpacity(0.35f), nullptr, EAlign::Near, EVAlign::Top), "OUT",
               plot.GetPadded(-3.f));

    // One cycle of the output, flat-topped when it clips
    if (fp.wave.size() > 1)
    {
      g.PathClear();
      for (size_t i = 0; i < fp.wave.size(); i++)
      {
        const float x = wave.L + wave.W() * i / (fp.wave.size() - 1);
        const float y = wave.MH() - wave.H() * 0.45f * fp.wave[i];
        if (i == 0)
          g.PathMoveTo(x, y);
        else
          g.PathLineTo(x, y);
      }
      g.PathStroke(IPattern(kCream.WithOpacity(0.75f)), 1.3f);
    }

    char comp[32], onset[32];
    snprintf(comp, sizeof(comp), fp.compression >= 20.f ? "COMP 20+:1" : "COMP %.1f:1", fp.compression);
    if (std::isnan(fp.onsetDb))
      snprintf(onset, sizeof(onset), "STAYS CLEAN");
    else
      snprintf(onset, sizeof(onset), "BREAKS %.0f dB", fp.onsetDb);
    const IText statText(9.f, kCream.WithOpacity(0.7f), nullptr, EAlign::Near, EVAlign::Middle);
    g.DrawText(statText, comp, stats);
    g.DrawText(statText.WithAlign(EAlign::Far), onset, stats);
  }

private:
  const NAMTheme& mTheme;
};

// The pedalboard the knobs live on: rails, input/output boxes, and the patch cables between everything.
class BoardControl : public IControl
{
public:
  struct Layout
  {
    IRECT inputBox, gate, eq, outputBox;
  };

  // Jacks sit this far below the top of each pedal.
  static constexpr float kJackDrop = 44.f;

  BoardControl(const IRECT& bounds, const NAMTheme& theme, const Layout& layout)
  : IControl(bounds)
  , mTheme(theme)
  , mLayout(layout)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    const IRECT r = mRECT;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.5f), r.GetTranslated(0.f, 3.f), 10.f);
    g.FillRoundRect(IColor(255, 12, 12, 14), r, 10.f);
    const IRECT inner = r.GetPadded(-6.f);
    const int nRails = 5;
    const float gap = 5.f;
    const float railH = (inner.H() - gap * (nRails - 1)) / nRails;
    for (int i = 0; i < nRails; i++)
    {
      const float top = inner.T + i * (railH + gap);
      const IRECT rail(inner.L, top, inner.R, top + railH);
      g.PathClear();
      g.PathRoundRect(rail, 3.f);
      g.PathFill(IPattern::CreateLinearGradient(
        rail, EDirection::Vertical, {{IColor(255, 48, 49, 54), 0.f}, {IColor(255, 29, 30, 34), 1.f}}));
      g.DrawLine(COLOR_WHITE.WithOpacity(0.07f), rail.L + 3.f, rail.T + 0.5f, rail.R - 3.f, rail.T + 0.5f);
    }
    g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.06f), r.GetPadded(-0.5f), 10.f, nullptr, 1.f);

    DrawBox(g, mLayout.inputBox, "IN");
    DrawBox(g, mLayout.outputBox, "OUT");

    const float jy = mLayout.gate.T + kJackDrop;
    const IRECT& in = mLayout.inputBox;
    const IRECT& gate = mLayout.gate;
    const IRECT& eq = mLayout.eq;
    const IRECT& out = mLayout.outputBox;

    // Plugs first; the cables start at their boots.
    const float guitarEnd = DrawPlug(g, in.L, jy, -1.f);
    const float inOut = DrawPlug(g, in.R, jy, 1.f);
    const float gateIn = DrawPlug(g, gate.L, jy, -1.f);
    const float gateOut = DrawPlug(g, gate.R, jy, 1.f);
    const float eqIn = DrawPlug(g, eq.L, jy, -1.f);
    const float eqOut = DrawPlug(g, eq.R, jy, 1.f);
    const float outIn = DrawPlug(g, out.L, jy, -1.f);
    const float deskEnd = DrawPlug(g, out.R, jy, 1.f);

    // Off to the capture and back: up over the top edge of the board.
    const float sendX = gateOut + 6.f, returnX = eqIn - 6.f;
    const Cable cables[] = {
      {r.L - 8.f, jy + 70.f, r.L + 10.f, jy + 40.f, guitarEnd - 20.f, jy, guitarEnd, jy},
      Cable::Sag(inOut, jy, gateIn, jy, 22.f),
      {gateOut, jy, sendX + 4.f, jy, sendX, jy - 20.f, sendX, r.T - 8.f},
      {returnX, r.T - 8.f, returnX, jy - 20.f, returnX - 4.f, jy, eqIn, jy},
      Cable::Sag(eqOut, jy, outIn, jy, 22.f),
      {deskEnd, jy, deskEnd + 20.f, jy, r.R - 10.f, jy + 40.f, r.R + 8.f, jy + 70.f},
    };
    for (const auto& c : cables)
      DrawCable(g, c);
    for (const auto& c : cables)
      DrawCablePulses(g, c, mTheme);

    const float mx = (sendX + returnX) * 0.5f;
    DrawArrow(g, sendX - 9.f, r.T + 16.f, true);
    DrawArrow(g, returnX + 9.f, r.T + 16.f, false);
    g.DrawText(IText(7.f, kCream.WithOpacity(0.5f), "Michroma-Regular", EAlign::Center, EVAlign::Middle), "CAPTURE",
               IRECT(mx - 40.f, jy + 26.f, mx + 40.f, jy + 40.f));
  }

private:
  void DrawArrow(IGraphics& g, float x, float y, bool up)
  {
    const float d = up ? -1.f : 1.f;
    g.FillTriangle(mTheme.accent.WithOpacity(0.7f), x - 4.f, y - 3.f * d, x + 4.f, y - 3.f * d, x, y + 4.f * d);
  }

  // Little brushed-metal junction box.
  void DrawBox(IGraphics& g, const IRECT& box, const char* label)
  {
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.55f), box.GetTranslated(0.f, 3.f), 7.f);
    g.PathClear();
    g.PathRoundRect(box, 7.f);
    g.PathFill(IPattern::CreateLinearGradient(
      box, EDirection::Vertical, {{IColor(255, 74, 75, 80), 0.f}, {IColor(255, 36, 37, 41), 1.f}}));
    g.PathClear();
    for (float y = box.T + 2.f; y < box.B - 2.f; y += 2.f)
    {
      g.PathMoveTo(box.L + 3.f, y);
      g.PathLineTo(box.R - 3.f, y);
    }
    g.PathStroke(IPattern(COLOR_WHITE.WithOpacity(0.025f)), 1.f);
    g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.14f), box.GetPadded(-1.f), 6.f, nullptr, 1.f);
    DrawScrew(g, box.L + 9.f, box.B - 9.f, 3.f, 40.f);
    DrawScrew(g, box.R - 9.f, box.B - 9.f, 3.f, 130.f);
    g.DrawText(IText(8.f, kCream.WithOpacity(0.55f), "Michroma-Regular", EAlign::Center, EVAlign::Middle), label,
               IRECT(box.L, box.B - 18.f, box.R, box.B - 2.f));
  }

  const NAMTheme& mTheme;
  Layout mLayout;
};

// An enamelled stompbox enclosure. Its knobs and footswitch are separate controls placed on top.
class PedalControl : public IControl
{
public:
  PedalControl(const IRECT& bounds, const NAMTheme& theme, const char* name, const IColor& enamel)
  : IControl(bounds)
  , mTheme(theme)
  , mName(name)
  , mEnamel(enamel)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    const IRECT box = mRECT.GetPadded(-1.f, 0.f, -1.f, -4.f);
    const float rad = 11.f;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.55f), box.GetTranslated(0.f, 4.f), rad);
    g.PathClear();
    g.PathRoundRect(box, rad);
    g.PathFill(IPattern::CreateLinearGradient(
      box, EDirection::Vertical,
      {{LerpColor(mEnamel, COLOR_WHITE, 0.08f), 0.f}, {LerpColor(mEnamel, COLOR_BLACK, 0.5f), 1.f}}));
    g.FillRoundRect(COLOR_WHITE.WithOpacity(0.05f), box.GetFromTop(box.H() * 0.4f), rad, rad, 0.f, 0.f);
    g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.16f), box.GetPadded(-1.f), rad - 1.f, nullptr, 1.f);

    const float s = 9.f;
    DrawScrew(g, box.L + s, box.T + s, 3.f, 30.f);
    DrawScrew(g, box.R - s, box.T + s, 3.f, 120.f);
    DrawScrew(g, box.L + s, box.B - s, 3.f, 75.f);
    DrawScrew(g, box.R - s, box.B - s, 3.f, 160.f);

    IText name(15.f, kCream, mTheme.stompFont.c_str(), EAlign::Center, EVAlign::Middle);
    const IRECT nameArea(box.L + 18.f, box.T + 5.f, box.R - 18.f, box.T + 25.f);
    const std::string fitted = FitText(g, name, mName, nameArea.W(), 8.f);
    DrawTextShadowed(g, name, fitted.c_str(), nameArea);
  }

private:
  const NAMTheme& mTheme;
  std::string mName;
  IColor mEnamel;
};

// Status LED over a chrome stomp switch.
class FootswitchControl : public ISwitchControlBase
{
public:
  FootswitchControl(const IRECT& bounds, int paramIdx, const NAMTheme& theme)
  : ISwitchControlBase(bounds, paramIdx)
  , mTheme(theme)
  {
    mDblAsSingleClick = true;
  }

  void Draw(IGraphics& g) override
  {
    const bool on = GetValue() > 0.5;
    const float cx = mRECT.MW();

    const float ly = mRECT.T + 7.f;
    g.FillCircle(kSilverDark, cx, ly, 5.5f);
    if (on)
      DrawGlow(g, cx, ly, 14.f, mTheme.accent, 0.9f);
    g.FillCircle(on ? LerpColor(mTheme.accent, COLOR_WHITE, 0.25f) : IColor(255, 48, 22, 22), cx, ly, 3.5f);
    g.FillCircle(COLOR_WHITE.WithOpacity(on ? 0.6f : 0.15f), cx - 1.f, ly - 1.f, 1.f);

    const float fy = mRECT.B - 23.f;
    g.FillCircle(COLOR_BLACK.WithOpacity(0.45f), cx + 1.f, fy + 3.f, 20.f);
    g.PathClear();
    g.PathCircle(cx, fy, 19.f);
    g.PathFill(IPattern::CreateLinearGradient(cx, fy - 19.f, cx, fy + 19.f,
                                              {{IColor(255, 120, 120, 126), 0.f}, {IColor(255, 40, 40, 44), 1.f}}));
    g.PathClear();
    g.PathCircle(cx, fy, 13.f);
    g.PathFill(IPattern::CreateLinearGradient(cx, fy - 13.f, cx, fy + 13.f, {{kSilverLight, 0.f}, {kSilverDark, 1.f}}));
    g.PathClear();
    g.PathCircle(cx, fy, 8.5f);
    g.PathFill(IPattern::CreateLinearGradient(
      cx, fy - 8.5f, cx, fy + 8.5f, {{IColor(255, 90, 90, 96), 0.f}, {IColor(255, 210, 210, 216), 1.f}}));
    g.DrawCircle(COLOR_BLACK.WithOpacity(0.5f), cx, fy, 13.f, nullptr, 1.f);
    if (mMouseIsOver)
      g.DrawCircle(mTheme.accent.WithOpacity(0.6f), cx, fy, 21.f, nullptr, 1.5f);
  }

private:
  const NAMTheme& mTheme;
};

// Speaker cab with a mic you can move. The IR is what this stage represents: dragging the mic across the cone
// either blends toward a second "edge" IR or, without one, darkens the tone the way moving off the dust cap does.
// The slider sets the mic's distance from the grille.
class CabControl : public IControl
{
public:
  CabControl(const IRECT& bounds, const NAMTheme& theme, int positionParam, int distanceParam,
             IActionFunction loadEdgeIR, IActionFunction clearEdgeIR)
  : IControl(bounds, {positionParam, distanceParam})
  , mTheme(theme)
  , mLoadEdgeIR(loadEdgeIR)
  , mClearEdgeIR(clearEdgeIR)
  {
    SetTooltip("Drag the mic across the speaker. The slider sets how far back it sits. Double-click to reset.");
    Layout();
  }

  void OnResize() override { Layout(); }

  void Draw(IGraphics& g) override
  {
    const IRECT c = mRECT;
    const bool live = mTheme.irActive && (mTheme.irLoaded || !mTheme.edgeIRName.empty());
    const bool hasEdge = !mTheme.edgeIRName.empty();

    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.5f), c.GetTranslated(0.f, 3.f), 12.f);
    g.FillRoundRect(IColor(255, 22, 21, 24), c, 12.f);
    g.FillRoundRect(IColor(255, 31, 28, 27), mGrille, 6.f);
    StrokeHatch(g, mGrille.GetPadded(-2.f), 3.f, kCream.WithOpacity(0.05f), false);
    StrokeHatch(g, mGrille.GetPadded(-2.f), 3.f, kCream.WithOpacity(0.05f), true);
    g.DrawRoundRect(kGold.WithOpacity(0.7f), mGrille, 6.f, nullptr, 1.5f);

    DrawSpeaker(g, live ? mTheme.level : 0.f, live);
    if (hasEdge)
    {
      // The two IRs: the dust cap in the middle, the edge of the cone at the dashed ring
      g.DrawText(IText(7.f, kGold.WithOpacity(0.8f), "Michroma-Regular", EAlign::Center, EVAlign::Middle), "CAP",
                 IRECT(mCx - 20.f, mCy + mR * 0.3f + 2.f, mCx + 20.f, mCy + mR * 0.3f + 12.f));
      g.PathClear();
      const int nDashes = 28;
      const float rr = mR * 0.9f;
      for (int i = 0; i < nDashes; i++)
      {
        // Move first, or each arc gets joined to the last one
        const float a0 = 360.f * i / nDashes;
        const float rad = a0 * static_cast<float>(PI) / 180.f;
        g.PathMoveTo(mCx + rr * std::sin(rad), mCy - rr * std::cos(rad));
        g.PathArc(mCx, mCy, rr, a0, a0 + 180.f / nDashes);
      }
      g.PathStroke(IPattern(mTheme.accent.WithOpacity(0.6f)), 1.f);
    }
    // The lead runs behind the slider and the bottom row
    DrawMicLead(g);
    DrawDistanceSlider(g);
    DrawMic(g);

    // Header line: what the mic is doing
    const IText label(8.f, kGold, "Michroma-Regular", EAlign::Near, EVAlign::Middle);
    const IRECT header(mGrille.L + 8.f, mGrille.T + 4.f, mGrille.R - 26.f, mGrille.T + 20.f);
    g.DrawText(label, "CAB / MIC", header);
    IRECT bounds;
    const float labelW = g.MeasureText(label, "CAB / MIC", bounds);
    const int pos = static_cast<int>(std::round(GetValue(0) * 100.0));
    char readout[32];
    if (hasEdge)
      snprintf(readout, sizeof(readout), "%d / %d", 100 - pos, pos);
    else if (pos < 3)
      snprintf(readout, sizeof(readout), "ON AXIS");
    else
      snprintf(readout, sizeof(readout), "OFF-AXIS %d%%", pos);
    g.DrawText(IText(7.f, kCream.WithOpacity(0.6f), "Michroma-Regular", EAlign::Near, EVAlign::Middle), readout,
               header.GetReducedFromLeft(labelW + 8.f));

    DrawEdgeRow(g);

    if (!mTheme.irActive && (mTheme.irLoaded || hasEdge))
    {
      g.FillRoundRect(COLOR_BLACK.WithOpacity(0.55f), c, 12.f);
      g.DrawText(IText(11.f, kCream.WithOpacity(0.7f), "Michroma-Regular", EAlign::Center, EVAlign::Middle),
                 "BYPASSED", c);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    mDrag = EDrag::None;
    mPress = EPress::None;
    if (mEdgeClearRect.Contains(x, y))
      mPress = EPress::Clear;
    else if (mEdgeTagRect.Contains(x, y))
      mPress = EPress::Load;
    else if (mSlider.GetPadded(10.f, 4.f, 10.f, 4.f).Contains(x, y))
    {
      mDrag = EDrag::Distance;
      SetDistanceFrom(y);
    }
    else if (std::hypot(x - mCx, y - mCy) <= mR * 1.15f)
    {
      mDrag = EDrag::Mic;
      SetMicFrom(x, y);
    }
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (mDrag == EDrag::Mic)
      SetMicFrom(x, y);
    else if (mDrag == EDrag::Distance)
      SetDistanceFrom(y);
  }

  // Act on release so the file dialog doesn't swallow the mouse-up.
  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    if (mPress == EPress::Load && mEdgeTagRect.Contains(x, y) && mLoadEdgeIR)
      mLoadEdgeIR(this);
    else if (mPress == EPress::Clear && mEdgeClearRect.Contains(x, y) && mClearEdgeIR)
      mClearEdgeIR(this);
    mDrag = EDrag::None;
    mPress = EPress::None;
    SetDirty(false);
  }

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    if (mSlider.GetPadded(10.f, 4.f, 10.f, 4.f).Contains(x, y))
    {
      SetValue(0.0, 1);
      SetDirty(true, 1);
    }
    else if (std::hypot(x - mCx, y - mCy) <= mR * 1.15f)
    {
      SetValue(0.0, 0);
      SetDirty(true, 0);
    }
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    SetValue(std::clamp(GetValue(1) + d * 0.04, 0.0, 1.0), 1);
    SetDirty(true, 1);
  }

private:
  enum class EDrag
  {
    None,
    Mic,
    Distance
  };
  enum class EPress
  {
    None,
    Load,
    Clear
  };

  void Layout()
  {
    mGrille = mRECT.GetPadded(-12.f);
    mR = std::min(mGrille.H() * 0.5f - 26.f, mGrille.W() * 0.3f);
    mCx = mGrille.L + mGrille.W() * 0.4f;
    mCy = mGrille.MH() + 2.f;
    mSlider = IRECT(mGrille.R - 24.f, mGrille.T + 40.f, mGrille.R - 16.f, mGrille.B - 34.f);
  }

  void SetMicFrom(float x, float y)
  {
    const float dx = x - mCx, dy = y - mCy;
    const float d = std::hypot(dx, dy);
    if (d > 2.f)
      mAngle = std::atan2(dy, dx);
    SetValue(std::clamp(d / (mR * 0.9f), 0.f, 1.f), 0);
    SetDirty(true, 0);
  }

  void SetDistanceFrom(float y)
  {
    SetValue(std::clamp((y - mSlider.T) / mSlider.H(), 0.f, 1.f), 1);
    SetDirty(true, 1);
  }

  void DrawSpeaker(IGraphics& g, float drive, bool live)
  {
    const float cx = mCx, cy = mCy, R = mR;
    g.FillCircle(COLOR_BLACK.WithOpacity(0.5f), cx + 1.f, cy + 3.f, R);
    g.FillCircle(IColor(255, 38, 38, 42), cx, cy, R);
    g.DrawCircle(COLOR_BLACK.WithOpacity(0.7f), cx, cy, R, nullptr, 2.f);
    for (int k = 0; k < 4; k++)
    {
      const float a = (45.f + 90.f * k) * static_cast<float>(PI) / 180.f;
      g.FillCircle(kSilverDark, cx + std::cos(a) * (R - 5.f), cy + std::sin(a) * (R - 5.f), 2.2f);
    }
    g.FillCircle(IColor(255, 25, 25, 27), cx, cy, R * 0.86f);
    g.DrawCircle(COLOR_WHITE.WithOpacity(0.07f), cx, cy, R * 0.8f, nullptr, 1.f);

    const float rc = R * 0.78f * (1.f + 0.035f * drive);
    g.PathClear();
    g.PathCircle(cx, cy, rc);
    g.PathFill(IPattern::CreateRadialGradient(cx - rc * 0.25f, cy - rc * 0.3f, rc * 1.3f,
                                              {{IColor(255, 64, 63, 68), 0.f}, {IColor(255, 13, 13, 15), 1.f}}));
    g.DrawCircle(COLOR_WHITE.WithOpacity(0.04f), cx, cy, rc * 0.62f, nullptr, 1.f);
    g.DrawCircle(COLOR_WHITE.WithOpacity(0.04f), cx, cy, rc * 0.45f, nullptr, 1.f);

    const float rd = R * 0.3f * (1.f + 0.07f * drive);
    if (live)
      DrawGlow(g, cx, cy, rd * 2.4f, mTheme.accent, 0.25f + 0.75f * drive);
    g.PathClear();
    g.PathCircle(cx, cy, rd);
    g.PathFill(IPattern::CreateRadialGradient(cx - rd * 0.35f, cy - rd * 0.35f, rd * 1.4f,
                                              {{IColor(255, 92, 92, 98), 0.f}, {IColor(255, 22, 22, 25), 1.f}}));
    g.DrawCircle(COLOR_BLACK.WithOpacity(0.5f), cx, cy, rd, nullptr, 1.f);
  }

  void MicCenter(float& x, float& y) const
  {
    const float pos = static_cast<float>(GetValue(0));
    x = mCx + std::cos(mAngle) * pos * mR * 0.9f;
    y = mCy + std::sin(mAngle) * pos * mR * 0.9f;
  }

  // From the end of the mic's shaft off the bottom of the cab
  void DrawMicLead(IGraphics& g)
  {
    float mx, my;
    MicCenter(mx, my);
    const float sx = mx + 22.f, sy = my + 18.f;
    DrawCable(g, {sx, sy, sx + 16.f, sy + 14.f, mSlider.L - 6.f, mGrille.B - 30.f, mSlider.L - 2.f, mRECT.B + 6.f},
              2.5f);
  }

  // Seen from the front: the capsule over the cone, its shaft running off to the side. The further back it sits, the
  // bigger it looks and the further its shadow falls.
  void DrawMic(IGraphics& g)
  {
    const float dist = static_cast<float>(GetValue(1));
    float mx, my;
    MicCenter(mx, my);
    const float rc = 8.f + 3.f * dist;
    const float sx = mx + 22.f, sy = my + 18.f;

    g.FillCircle(COLOR_BLACK.WithOpacity(0.45f - 0.2f * dist), mx + 2.f + 6.f * dist, my + 3.f + 8.f * dist, rc);
    g.PathClear();
    g.PathMoveTo(mx + 5.f, my - 5.f);
    g.PathLineTo(sx + 4.f, sy - 4.f);
    g.PathLineTo(sx - 4.f, sy + 4.f);
    g.PathLineTo(mx - 5.f, my + 5.f);
    g.PathClose();
    g.PathFill(IPattern::CreateLinearGradient(mx, my - 6.f, mx, my + 6.f,
                                              {{IColor(255, 84, 85, 90), 0.f}, {IColor(255, 24, 25, 28), 1.f}}));

    g.PathClear();
    g.PathCircle(mx, my, rc);
    g.PathFill(IPattern::CreateLinearGradient(mx - rc, my - rc, mx + rc, my + rc, {{kSilverLight, 0.f}, {kSilverDark, 1.f}}));
    g.PathClear();
    g.PathMoveTo(mx - rc * 0.7f, my);
    g.PathLineTo(mx + rc * 0.7f, my);
    g.PathMoveTo(mx, my - rc * 0.7f);
    g.PathLineTo(mx, my + rc * 0.7f);
    g.PathStroke(IPattern(COLOR_BLACK.WithOpacity(0.35f)), 1.f);
    g.DrawCircle(COLOR_BLACK.WithOpacity(0.35f), mx, my, rc * 0.55f, nullptr, 1.f);
    g.DrawCircle(COLOR_BLACK.WithOpacity(0.6f), mx, my, rc, nullptr, 1.f);
    if (mMouseIsOver || mDrag == EDrag::Mic)
      g.DrawCircle(mTheme.accent.WithOpacity(mDrag == EDrag::Mic ? 0.9f : 0.5f), mx, my, rc + 3.f, nullptr, 1.5f);
  }

  void DrawDistanceSlider(IGraphics& g)
  {
    const float dist = static_cast<float>(GetValue(1));
    const float x = mSlider.MW();
    const float hy = mSlider.T + dist * mSlider.H();
    const IText t(6.f, kCream.WithOpacity(0.5f), "Michroma-Regular", EAlign::Center, EVAlign::Middle);
    g.DrawText(t, "NEAR", IRECT(x - 16.f, mSlider.T - 14.f, x + 16.f, mSlider.T - 4.f));
    g.DrawText(t, "FAR", IRECT(x - 16.f, mSlider.B + 4.f, x + 16.f, mSlider.B + 14.f));
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.6f), IRECT(x - 2.f, mSlider.T, x + 2.f, mSlider.B), 2.f);
    g.FillRoundRect(mTheme.accent.WithOpacity(0.7f), IRECT(x - 1.5f, mSlider.T, x + 1.5f, hy), 1.5f);
    const IRECT handle(x - 7.f, hy - 4.f, x + 7.f, hy + 4.f);
    g.PathClear();
    g.PathRoundRect(handle, 2.f);
    g.PathFill(IPattern::CreateLinearGradient(handle, EDirection::Vertical, {{kSilverLight, 0.f}, {kSilverDark, 1.f}}));
    if (mDrag == EDrag::Distance)
      g.DrawRoundRect(mTheme.accent, handle.GetPadded(1.f), 3.f, nullptr, 1.f);
  }

  // Bottom line: status, or the slot for the second IR.
  void DrawEdgeRow(IGraphics& g)
  {
    const IRECT row(mGrille.L + 8.f, mGrille.B - 19.f, mGrille.R - 8.f, mGrille.B - 3.f);
    mEdgeTagRect = IRECT();
    mEdgeClearRect = IRECT();
    const IText status(7.f, kCream.WithOpacity(0.55f), "Michroma-Regular", EAlign::Near, EVAlign::Middle);

    if (!mTheme.edgeIRName.empty())
    {
      const IRECT pill = row;
      g.FillRoundRect(COLOR_BLACK.WithOpacity(0.55f), pill, 8.f);
      g.DrawRoundRect(mTheme.accent.WithOpacity(0.6f), pill, 8.f, nullptr, 1.f);
      mEdgeClearRect = pill.GetFromRight(18.f);
      const float cx = mEdgeClearRect.MW(), cy = mEdgeClearRect.MH();
      g.DrawLine(kCream.WithOpacity(0.8f), cx - 3.f, cy - 3.f, cx + 3.f, cy + 3.f, nullptr, 1.2f);
      g.DrawLine(kCream.WithOpacity(0.8f), cx - 3.f, cy + 3.f, cx + 3.f, cy - 3.f, nullptr, 1.2f);
      IText name(7.f, kCream.WithOpacity(0.85f), "Michroma-Regular", EAlign::Near, EVAlign::Middle);
      const IRECT nameArea = pill.GetReducedFromLeft(8.f).GetReducedFromRight(20.f);
      const std::string fitted = FitText(g, name, "EDGE: " + mTheme.edgeIRName, nameArea.W(), 6.f);
      g.DrawText(name, fitted.c_str(), nameArea);
    }
    else if (mTheme.irLoaded)
    {
      const float end = DrawTag(g, row.L, row.MH(), "+ EDGE IR", mTheme.accent);
      mEdgeTagRect = IRECT(row.L, row.T, end, row.B);
    }
    else
    {
      const bool cabInCapture = mTheme.hasModel && GearIncludesCab(mTheme.gear.gearType);
      g.DrawText(status, cabInCapture ? "CAB IS IN THE CAPTURE" : "NO IR LOADED", row);
    }
  }

  const NAMTheme& mTheme;
  IActionFunction mLoadEdgeIR, mClearEdgeIR;
  IRECT mGrille, mSlider, mEdgeTagRect, mEdgeClearRect;
  float mCx = 0.f, mCy = 0.f, mR = 0.f;
  // Which way round the cone the mic sits; only the distance from the center is a parameter.
  float mAngle = -0.6f;
  EDrag mDrag = EDrag::None;
  EPress mPress = EPress::None;
};
} // namespace NAMRig
