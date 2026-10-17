#pragma once

// Full-window capture library: every .nam in a folder as a card with a little drawing of its gear, filterable by gear
// and tone. Click a card to load it.

#include <functional>
#include <string>
#include <vector>

#include "NAMLibrary.h"
#include "NAMRigControls.h"

namespace NAMRig
{
// A small drawing of a piece of gear, for the library cards.
inline void DrawGearThumb(IGraphics& g, const IRECT& r, const NAMGearInfo& gear, const NAMTheme& theme)
{
  const IColor accent = AccentForTone(gear.toneType);
  switch (FaceplateFor(gear.gearType))
  {
    case NAMFaceplate::Stompbox:
    {
      const IRECT box = r.GetCentredInside(std::min(r.W(), r.H() * 0.72f), r.H());
      g.FillRoundRect(COLOR_BLACK.WithOpacity(0.5f), box.GetTranslated(0.f, 2.f), 6.f);
      g.PathClear();
      g.PathRoundRect(box, 6.f);
      g.PathFill(IPattern::CreateLinearGradient(
        box, EDirection::Vertical,
        {{LerpColor(accent, COLOR_BLACK, 0.35f), 0.f}, {LerpColor(accent, COLOR_BLACK, 0.72f), 1.f}}));
      g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.18f), box.GetPadded(-0.5f), 6.f, nullptr, 1.f);
      for (float f : {0.3f, 0.7f})
      {
        g.FillCircle(IColor(255, 20, 20, 22), box.L + box.W() * f, box.T + box.H() * 0.17f, box.W() * 0.11f);
        g.DrawRadialLine(kCream, box.L + box.W() * f, box.T + box.H() * 0.17f, -40.f, 0.f, box.W() * 0.1f);
      }
      g.FillRoundRect(kCream.WithOpacity(0.9f), box.GetPadded(-box.W() * 0.14f).GetFromTop(box.H() * 0.25f).GetVShifted(box.H() * 0.25f), 2.f);
      g.FillCircle(kSilverLight, box.MW(), box.B - box.H() * 0.2f, box.W() * 0.16f);
      g.FillCircle(kSilverDark, box.MW(), box.B - box.H() * 0.2f, box.W() * 0.09f);
      break;
    }
    case NAMFaceplate::Rack:
    {
      const IRECT unit = r.GetCentredInside(r.W(), r.H() * 0.46f);
      g.FillRect(COLOR_BLACK.WithOpacity(0.5f), unit.GetTranslated(0.f, 2.f));
      g.PathClear();
      g.PathRect(unit);
      g.PathFill(IPattern::CreateLinearGradient(
        unit, EDirection::Vertical, {{IColor(255, 70, 72, 78), 0.f}, {IColor(255, 36, 37, 41), 1.f}}));
      for (const IRECT& ear : {unit.GetFromLeft(7.f), unit.GetFromRight(7.f)})
      {
        g.FillRect(IColor(255, 30, 31, 35), ear);
        g.FillCircle(kSilverLight, ear.MW(), ear.T + 5.f, 1.5f);
        g.FillCircle(kSilverLight, ear.MW(), ear.B - 5.f, 1.5f);
      }
      const IRECT vu = unit.GetPadded(-5.f).GetReducedFromRight(5.f).GetFromRight(unit.H() * 0.95f);
      g.FillRoundRect(IColor(255, 236, 222, 186), vu, 2.f);
      g.DrawRadialLine(kInk, vu.MW(), vu.B - 2.f, -25.f, 0.f, vu.H() * 0.8f);
      g.FillRect(accent.WithOpacity(0.25f), vu);
      g.FillRect(kCream.WithOpacity(0.7f), IRECT(unit.L + 12.f, unit.T + 6.f, vu.L - 8.f, unit.T + 9.f));
      g.FillRect(kCream.WithOpacity(0.35f), IRECT(unit.L + 12.f, unit.T + 13.f, vu.L - 20.f, unit.T + 15.f));
      g.FillCircle(accent, unit.L + 14.f, unit.B - 7.f, 2.f);
      break;
    }
    default:
    {
      // Amp head; with a speaker under it if the cab is part of the capture
      const bool combo = GearIncludesCab(gear.gearType);
      const IRECT box = combo ? r.GetCentredInside(r.W() * 0.86f, r.H()) : r.GetCentredInside(r.W(), r.H() * 0.6f);
      const IRECT head = combo ? box.GetFromTop(box.H() * 0.38f) : box;
      g.FillRoundRect(COLOR_BLACK.WithOpacity(0.5f), box.GetTranslated(0.f, 2.f), 5.f);
      g.FillRoundRect(IColor(255, 24, 23, 26), box, 5.f);
      StrokeHatch(g, box.GetPadded(-2.f), 3.f, COLOR_WHITE.WithOpacity(0.04f), false);
      const IRECT piping = head.GetPadded(-4.f);
      g.DrawRoundRect(kGold.WithOpacity(0.8f), piping, 3.f, nullptr, 1.f);
      const IRECT strip = piping.GetPadded(-3.f).GetFromBottom(piping.H() * 0.32f);
      g.FillRect(IColor(255, 46, 43, 38), strip);
      for (int k = 0; k < 5; k++)
        g.FillCircle(kCream.WithOpacity(0.75f), strip.L + 6.f + k * 7.f, strip.MH(), 1.5f);
      DrawGlow(g, strip.R - 6.f, strip.MH(), 7.f, accent, 0.9f);
      g.FillCircle(accent, strip.R - 6.f, strip.MH(), 2.5f);
      if (combo)
      {
        const IRECT cab = box.GetReducedFromTop(head.H()).GetPadded(-4.f);
        g.FillRoundRect(IColor(255, 34, 31, 29), cab, 3.f);
        StrokeHatch(g, cab, 2.f, kCream.WithOpacity(0.06f), true);
        const float rr = std::min(cab.W(), cab.H()) * 0.38f;
        g.FillCircle(IColor(255, 18, 18, 20), cab.MW(), cab.MH(), rr);
        g.FillCircle(IColor(255, 54, 54, 58), cab.MW(), cab.MH(), rr * 0.3f);
      }
      else
      {
        std::string primary, secondary;
        GetTitleLines(gear, primary, secondary);
        IText brand(14.f, kCream, theme.scriptFont.c_str(), EAlign::Center, EVAlign::Middle);
        const IRECT plate = piping.GetReducedFromBottom(strip.H() + 3.f);
        const std::string fitted = FitText(g, brand, primary, plate.W() - 8.f, 7.f);
        g.DrawText(brand, fitted.c_str(), plate);
      }
      break;
    }
  }
}

class LibraryControl : public IControl
{
public:
  using PickFunc = std::function<void(const std::string& path)>;

  LibraryControl(const IRECT& bounds, const NAMTheme& theme, const NAMLibrary& library, PickFunc onPick,
                 IActionFunction onChooseFolder)
  : IControl(bounds)
  , mTheme(theme)
  , mLibrary(library)
  , mOnPick(onPick)
  , mOnChooseFolder(onChooseFolder)
  {
  }

  void Show()
  {
    mGeneration = ~0u;
    Refresh();
    Hide(false);
    GetUI()->SetAllControlsDirty();
  }

  void Close()
  {
    Hide(true);
    GetUI()->SetAllControlsDirty();
  }

  // From the plug-in's idle loop: picks up results as the scan finds them.
  void Poll()
  {
    if (IsHidden())
      return;
    if (mLibrary.Generation() != mGeneration || mLibrary.IsScanning())
    {
      Refresh();
      SetDirty(false);
    }
  }

  void Draw(IGraphics& g) override
  {
    mHits.clear();
    DrawPanelBackdrop(g, mRECT, mTheme);
    const IRECT content = mRECT.GetPadded(-36.f, -28.f, -36.f, -24.f);

    // Title, folder and buttons
    DrawTextShadowed(g, IText(18.f, kCream, "Michroma-Regular", EAlign::Near, EVAlign::Middle), "CAPTURE LIBRARY",
                     IRECT(content.L, content.T, content.R, content.T + 28.f));
    const IRECT closeRect(content.R - 22.f, content.T + 3.f, content.R, content.T + 25.f);
    AddHit(closeRect, EHit::Close);
    const IColor closeColor = mHover == HitIndex(EHit::Close) ? mTheme.accent : kCream.WithOpacity(0.7f);
    g.DrawLine(closeColor, closeRect.L + 6.f, closeRect.T + 6.f, closeRect.R - 6.f, closeRect.B - 6.f, nullptr, 1.5f);
    g.DrawLine(closeColor, closeRect.L + 6.f, closeRect.B - 6.f, closeRect.R - 6.f, closeRect.T + 6.f, nullptr, 1.5f);

    const IRECT folderButton(closeRect.L - 150.f, content.T + 4.f, closeRect.L - 14.f, content.T + 24.f);
    AddHit(folderButton, EHit::Folder);
    DrawPill(g, folderButton, "CHOOSE FOLDER", mHover == HitIndex(EHit::Folder) ? 1.f : 0.5f);

    char count[64];
    if (mLibrary.IsScanning())
      snprintf(count, sizeof(count), "SCANNING... %d FOUND", static_cast<int>(mEntries.size()));
    else
      snprintf(count, sizeof(count), "%d CAPTURES", static_cast<int>(mEntries.size()));
    const IRECT folderLine(content.L, content.T + 30.f, content.R, content.T + 46.f);
    const IText countText(8.f, mTheme.accent, "Michroma-Regular", EAlign::Far, EVAlign::Middle);
    g.DrawText(countText, count, folderLine);
    IText folderText(11.f, kCream.WithOpacity(0.5f), nullptr, EAlign::Near, EVAlign::Middle);
    const std::string folder = FitText(g, folderText, mFolder, folderLine.W() - 180.f, 9.f);
    g.DrawText(folderText, folder.c_str(), folderLine);

    // Filters
    const float gearY = content.T + 66.f, toneY = content.T + 90.f;
    const IText rowLabel(8.f, kGold, "Michroma-Regular", EAlign::Near, EVAlign::Middle);
    g.DrawText(rowLabel, "GEAR", IRECT(content.L, gearY - 8.f, content.L + 50.f, gearY + 8.f));
    g.DrawText(rowLabel, "TONE", IRECT(content.L, toneY - 8.f, content.L + 50.f, toneY + 8.f));
    float x = content.L + 52.f;
    for (int i = 0; i < kNumGearFilters; i++)
      x = DrawChip(g, x, gearY, kGearFilterLabels[i], CountMatching(i, mToneFilter), i == mGearFilter, kCream,
                   EHit::GearChip, i);
    x = content.L + 52.f;
    for (int i = 0; i < kNumToneFilters; i++)
    {
      const IColor c = i == 0 ? kCream : AccentForTone(static_cast<NAMToneType>(i));
      x = DrawChip(g, x, toneY, kToneFilterLabels[i], CountMatching(mGearFilter, i), i == mToneFilter, c,
                   EHit::ToneChip, i);
    }

    // Cards
    const IRECT grid(content.L, content.T + 112.f, content.R, content.B - 30.f);
    const int perPage = kCols * kRows;
    const int nPages = std::max(1, (static_cast<int>(mVisible.size()) + perPage - 1) / perPage);
    mPage = std::clamp(mPage, 0, nPages - 1);
    if (mVisible.empty())
    {
      const char* msg = mLibrary.IsScanning()       ? "Reading captures..."
                        : mEntries.empty()          ? "No .nam files in this folder. Choose another one."
                                                    : "Nothing matches these filters.";
      g.DrawText(IText(13.f, kCream.WithOpacity(0.5f), nullptr, EAlign::Center, EVAlign::Middle), msg, grid);
    }
    for (int k = 0; k < perPage; k++)
    {
      const size_t v = static_cast<size_t>(mPage * perPage + k);
      if (v >= mVisible.size())
        break;
      const IRECT card = grid.GetGridCell(k / kCols, k % kCols, kRows, kCols).GetPadded(-6.f);
      AddHit(card, EHit::Card, mVisible[v]);
      DrawCard(g, card, mEntries[mVisible[v]], mHover == static_cast<int>(mHits.size()) - 1);
    }

    // Paging
    const IRECT footer(content.L, content.B - 22.f, content.R, content.B);
    g.DrawText(IText(10.f, kCream.WithOpacity(0.4f), nullptr, EAlign::Near, EVAlign::Middle),
               "Click a capture to load it. Esc closes.", footer);
    if (nPages > 1)
    {
      char page[32];
      snprintf(page, sizeof(page), "%d / %d", mPage + 1, nPages);
      const IRECT pageRect = footer.GetFromRight(140.f);
      g.DrawText(IText(9.f, kCream.WithOpacity(0.7f), "Michroma-Regular", EAlign::Center, EVAlign::Middle), page,
                 pageRect);
      const IRECT prev = pageRect.GetFromLeft(30.f), next = pageRect.GetFromRight(30.f);
      AddHit(prev, EHit::Prev);
      AddHit(next, EHit::Next);
      DrawPill(g, prev, "<", mHover == HitIndex(EHit::Prev) ? 1.f : 0.5f);
      DrawPill(g, next, ">", mHover == HitIndex(EHit::Next) ? 1.f : 0.5f);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override { mPressed = HitAt(x, y); }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    const int idx = HitAt(x, y);
    if (idx < 0 || idx != mPressed)
      return;
    const Hit hit = mHits[idx];
    switch (hit.kind)
    {
      case EHit::Close: Close(); return;
      case EHit::Folder:
        if (mOnChooseFolder)
          mOnChooseFolder(this);
        break;
      case EHit::GearChip:
        mGearFilter = hit.value;
        mPage = 0;
        Filter();
        break;
      case EHit::ToneChip:
        mToneFilter = hit.value;
        mPage = 0;
        Filter();
        break;
      case EHit::Prev: mPage--; break;
      case EHit::Next: mPage++; break;
      case EHit::Card:
      {
        const std::string path = mEntries[hit.value].path;
        Close();
        if (mOnPick)
          mOnPick(path);
        return;
      }
    }
    SetDirty(false);
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    const int idx = HitAt(x, y);
    if (idx != mHover)
    {
      mHover = idx;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    mHover = -1;
    SetDirty(false);
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    mPage += d < 0.f ? 1 : -1;
    SetDirty(false);
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    if (key.VK == kVK_ESCAPE)
    {
      Close();
      return true;
    }
    return false;
  }

private:
  static constexpr int kCols = 3;
  static constexpr int kRows = 3;
  static constexpr int kNumGearFilters = 6;
  static constexpr int kNumToneFilters = 6;
  static constexpr const char* kGearFilterLabels[kNumGearFilters] = {"ALL", "AMP", "AMP + CAB", "PEDAL", "PREAMP",
                                                                      "STUDIO"};
  // Indexed like NAMToneType, with "ALL" in place of Unknown.
  static constexpr const char* kToneFilterLabels[kNumToneFilters] = {"ALL", "CLEAN", "CRUNCH", "OVERDRIVE", "HI-GAIN",
                                                                      "FUZZ"};

  enum class EHit
  {
    Close,
    Folder,
    GearChip,
    ToneChip,
    Card,
    Prev,
    Next
  };

  struct Hit
  {
    IRECT rect;
    EHit kind;
    int value;
  };

  static bool GearMatches(int filter, NAMGearType t)
  {
    switch (filter)
    {
      case 1: return t == NAMGearType::Amp || t == NAMGearType::PedalAmp;
      case 2: return GearIncludesCab(t);
      case 3: return t == NAMGearType::Pedal;
      case 4: return t == NAMGearType::Preamp;
      case 5: return t == NAMGearType::Studio;
      default: return true;
    }
  }

  static bool ToneMatches(int filter, NAMToneType t) { return filter == 0 || static_cast<int>(t) == filter; }

  int CountMatching(int gearFilter, int toneFilter) const
  {
    int n = 0;
    for (const auto& e : mEntries)
      n += GearMatches(gearFilter, e.gearType) && ToneMatches(toneFilter, e.toneType);
    return n;
  }

  void Refresh()
  {
    mGeneration = mLibrary.Generation();
    mLibrary.Snapshot(mEntries, mFolder);
    Filter();
  }

  void Filter()
  {
    mVisible.clear();
    for (size_t i = 0; i < mEntries.size(); i++)
      if (GearMatches(mGearFilter, mEntries[i].gearType) && ToneMatches(mToneFilter, mEntries[i].toneType))
        mVisible.push_back(i);
  }

  void AddHit(const IRECT& r, EHit kind, size_t value = 0) { mHits.push_back({r, kind, static_cast<int>(value)}); }

  int HitAt(float x, float y) const
  {
    for (size_t i = 0; i < mHits.size(); i++)
      if (mHits[i].rect.Contains(x, y))
        return static_cast<int>(i);
    return -1;
  }

  // Index of the first hit area of a kind; only meaningful for the one-off buttons.
  int HitIndex(EHit kind) const
  {
    for (size_t i = 0; i < mHits.size(); i++)
      if (mHits[i].kind == kind)
        return static_cast<int>(i);
    return -2;
  }

  void DrawPill(IGraphics& g, const IRECT& r, const char* label, float emphasis)
  {
    const float rad = r.H() * 0.5f;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.5f), r, rad);
    g.DrawRoundRect(mTheme.accent.WithOpacity(0.3f + 0.6f * emphasis), r, rad, nullptr, 1.f);
    g.DrawText(IText(8.f, kCream.WithOpacity(0.6f + 0.3f * emphasis), "Michroma-Regular", EAlign::Center,
                     EVAlign::Middle),
               label, r);
  }

  float DrawChip(IGraphics& g, float x, float cy, const char* label, int count, bool active, const IColor& color,
                 EHit kind, int value)
  {
    char text[48];
    snprintf(text, sizeof(text), "%s  %d", label, count);
    const IText t(8.f, active ? kInk : color.WithOpacity(count ? 0.85f : 0.3f), "Michroma-Regular", EAlign::Center,
                  EVAlign::Middle);
    IRECT bounds;
    const float w = g.MeasureText(t, text, bounds) + 18.f;
    const IRECT r(x, cy - 9.f, x + w, cy + 9.f);
    AddHit(r, kind, value);
    const bool hover = mHover == static_cast<int>(mHits.size()) - 1;
    if (active)
      g.FillRoundRect(color, r, 9.f);
    else
      g.FillRoundRect(color.WithOpacity(hover ? 0.2f : 0.08f), r, 9.f);
    g.DrawRoundRect(color.WithOpacity(active ? 1.f : 0.4f), r, 9.f, nullptr, 1.f);
    g.DrawText(t, text, r);
    return r.R + 6.f;
  }

  void DrawCard(IGraphics& g, const IRECT& card, const NAMGearInfo& gear, bool hover)
  {
    const IColor accent = AccentForTone(gear.toneType);
    const bool loaded = mTheme.hasModel && mTheme.gear.path == gear.path;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(hover ? 0.55f : 0.4f), card, 10.f);
    if (hover || loaded)
      DrawGlow(g, card.L + card.H() * 0.5f, card.MH(), card.H() * 0.9f, accent, loaded ? 0.35f : 0.2f);
    g.DrawRoundRect(accent.WithOpacity(loaded ? 1.f : (hover ? 0.8f : 0.3f)), card, 10.f, nullptr, loaded ? 1.5f : 1.f);

    const IRECT thumb = card.GetFromLeft(card.H() * 1.05f).GetPadded(-12.f);
    DrawGearThumb(g, thumb, gear, mTheme);

    const IRECT text = card.GetReducedFromLeft(card.H() * 1.05f).GetPadded(0.f, -10.f, -12.f, -10.f);
    std::string primary, secondary;
    GetTitleLines(gear, primary, secondary);
    IText title(11.f, kCream, "Michroma-Regular", EAlign::Near, EVAlign::Middle);
    const std::string fittedTitle = FitText(g, title, ToUpper(primary), text.W(), 8.f);
    g.DrawText(title, fittedTitle.c_str(), IRECT(text.L, text.T, text.R, text.T + 20.f));
    if (!secondary.empty())
    {
      IText sub(11.f, kCream.WithOpacity(0.6f), nullptr, EAlign::Near, EVAlign::Middle);
      const std::string fitted = FitText(g, sub, secondary, text.W(), 9.f);
      g.DrawText(sub, fitted.c_str(), IRECT(text.L, text.T + 20.f, text.R, text.T + 36.f));
    }

    const float tagY = text.T + 50.f;
    float x = DrawTag(g, text.L, tagY, ToneTypeLabel(gear.toneType), accent);
    DrawTag(g, x, tagY, GearTypeLabel(gear.gearType), kGold);
    if (loaded)
      g.DrawText(IText(7.f, accent, "Michroma-Regular", EAlign::Far, EVAlign::Middle), "LOADED",
                 IRECT(text.L, text.T, text.R, text.T + 10.f).GetVShifted(-6.f));

    DrawCredit(g, gear, IRECT(text.L, text.B - 14.f, text.R, text.B), kCream.WithOpacity(0.45f));
    if (gear.modeledBy.empty())
    {
      IText file(9.f, kCream.WithOpacity(0.35f), nullptr, EAlign::Far, EVAlign::Middle);
      const std::string fitted = FitText(g, file, gear.fileStem + ".nam", text.W(), 8.f);
      g.DrawText(file, fitted.c_str(), IRECT(text.L, text.B - 14.f, text.R, text.B));
    }
  }

  const NAMTheme& mTheme;
  const NAMLibrary& mLibrary;
  PickFunc mOnPick;
  IActionFunction mOnChooseFolder;

  unsigned mGeneration = ~0u;
  std::vector<NAMGearInfo> mEntries;
  std::string mFolder;
  std::vector<size_t> mVisible;
  int mGearFilter = 0;
  int mToneFilter = 0;
  int mPage = 0;

  // Rebuilt every draw, so hit testing always matches what's on screen.
  std::vector<Hit> mHits;
  int mHover = -1;
  int mPressed = -1;
};
} // namespace NAMRig
