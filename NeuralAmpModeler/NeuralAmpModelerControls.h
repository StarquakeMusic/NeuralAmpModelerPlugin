#pragma once

#include <cmath> // std::round
#include <cstdio> // FILE, fclose
#include <sstream> // std::stringstream
#include <unordered_map> // std::unordered_map
#include "IControls.h"
#include "IPlugPaths.h"
#include "NAMRigControls.h"

#ifdef OS_WIN
  #include <Windows.h>
  #include <Shellapi.h>
#endif

#define PLUG() static_cast<PLUG_CLASS_NAME*>(GetDelegate())
#define NAM_KNOB_HEIGHT 120.0f
#define NAM_SWTICH_HEIGHT 50.0f

using namespace iplug;
using namespace igraphics;

enum class NAMBrowserState
{
  Empty, // when no file loaded, show "Get" button
  Loaded // when file loaded, show "Clear" button
};

enum class NAMFileLoadSource
{
  ExistingBrowserSelection,
  FilePickerSelection
};

// Where the corner button on the plugin (settings, close settings) goes
// :param rect: Rect for the whole plugin's UI
IRECT CornerButtonArea(const IRECT& rect)
{
  const auto mainArea = rect.GetPadded(-20);
  return mainArea.GetFromTRHC(50, 50).GetCentredInside(20, 20);
};

class NAMSquareButtonControl : public ISVGButtonControl
{
public:
  NAMSquareButtonControl(const IRECT& bounds, IActionFunction af, const ISVG& svg)
  : ISVGButtonControl(bounds, af, svg, svg)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (mMouseIsOver)
      g.FillRoundRect(PluginColors::MOUSEOVER, mRECT, 2.f);

    ISVGButtonControl::Draw(g);
  }
};

class NAMCircleButtonControl : public ISVGButtonControl
{
public:
  NAMCircleButtonControl(const IRECT& bounds, IActionFunction af, const ISVG& svg)
  : ISVGButtonControl(bounds, af, svg, svg)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (mMouseIsOver)
      g.FillEllipse(PluginColors::MOUSEOVER, mRECT);

    ISVGButtonControl::Draw(g);
  }
};

/// Full-window dim layer; click dismisses (used for Slim overlay).
class NAMSlimOverlayBackdropControl : public IControl
{
public:
  NAMSlimOverlayBackdropControl(const IRECT& bounds, IActionFunction dismiss)
  : IControl(bounds, dismiss)
  , mDismiss(dismiss)
  {
  }

  void Draw(IGraphics& g) override { g.FillRect(COLOR_BLACK.WithOpacity(0.45f), mRECT); }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (mDismiss)
      mDismiss(this);
  }

private:
  IActionFunction mDismiss;
};

class NAMKnobControl : public IVKnobControl
{
public:
  NAMKnobControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, const NAMTheme& theme)
  : IVKnobControl(bounds, paramIdx, label, style, true)
  , mTheme(theme)
  {
  }

  void OnInit() override
  {
    IVKnobControl::OnInit();
    // Bipolar params (gain in dB) draw their arc out from 0.
    if (const IParam* p = GetParam())
      if (p->GetMin() < 0.0 && p->GetMax() > 0.0)
        mAnchorAngle = mAngle1 + static_cast<float>(p->ToNormalized(0.0)) * (mAngle2 - mAngle1);
  }

  void DrawWidget(IGraphics& g) override
  {
    const float cx = mWidgetBounds.MW(), cy = mWidgetBounds.MH();
    const float R = std::min(mWidgetBounds.W(), mWidgetBounds.H()) * 0.5f - 7.f;
    const float angle = mAngle1 + static_cast<float>(GetValue()) * (mAngle2 - mAngle1);

    // Track and value arc
    const float a1 = std::min(angle, mAnchorAngle), a2 = std::max(angle, mAnchorAngle);
    g.DrawArc(COLOR_BLACK.WithOpacity(0.6f), cx, cy, R + 5.f, mAngle1, mAngle2, &mBlend, 3.f);
    g.DrawArc(mTheme.accent.WithOpacity(0.25f), cx, cy, R + 5.f, a1, a2, &mBlend, 7.f);
    g.DrawArc(mTheme.accent, cx, cy, R + 5.f, a1, a2, &mBlend, 2.5f);

    // Body
    g.FillCircle(COLOR_BLACK.WithOpacity(0.55f), cx + 1.f, cy + 3.f, R, &mBlend);
    g.PathClear();
    g.PathCircle(cx, cy, R);
    g.PathFill(IPattern::CreateLinearGradient(
                 cx, cy - R, cx, cy + R, {{IColor(255, 62, 61, 66), 0.f}, {IColor(255, 16, 16, 18), 1.f}}),
               IFillOptions(), &mBlend);

    // Knurled skirt, turning with the knob
    g.PathClear();
    for (int i = 0; i < 36; i++)
    {
      const float a = (angle + i * 10.f) * static_cast<float>(PI) / 180.f;
      const float sx = std::sin(a), sy = -std::cos(a);
      g.PathMoveTo(cx + sx * R * 0.8f, cy + sy * R * 0.8f);
      g.PathLineTo(cx + sx * R * 0.97f, cy + sy * R * 0.97f);
    }
    g.PathStroke(IPattern(COLOR_BLACK.WithOpacity(0.45f)), 1.f, IStrokeOptions(), &mBlend);

    // Cap
    const float rc = R * 0.72f;
    g.PathClear();
    g.PathCircle(cx, cy, rc);
    g.PathFill(IPattern::CreateLinearGradient(
                 cx - rc, cy - rc, cx + rc, cy + rc, {{IColor(255, 86, 86, 92), 0.f}, {IColor(255, 26, 26, 30), 1.f}}),
               IFillOptions(), &mBlend);
    g.DrawCircle(COLOR_BLACK.WithOpacity(0.6f), cx, cy, rc, &mBlend, 1.f);
    g.DrawArc(COLOR_WHITE.WithOpacity(0.12f), cx, cy, rc - 1.f, -60.f, 60.f, &mBlend, 1.f);

    const IColor pointer = mMouseIsOver ? LerpColor(mTheme.accent, COLOR_WHITE, 0.3f) : NAMRig::kCream;
    g.DrawRadialLine(pointer, cx, cy, angle, R * 0.25f, R * 0.95f, &mBlend, 2.5f);
  }

private:
  const NAMTheme& mTheme;
};

class NAMSwitchControl : public IVSlideSwitchControl, public IBitmapBase
{
public:
  NAMSwitchControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, IBitmap bitmap)
  : IVSlideSwitchControl(bounds, paramIdx, label,
                         style.WithRoundness(0.666f)
                           .WithShowValue(false)
                           .WithEmboss(true)
                           .WithShadowOffset(1.5f)
                           .WithDrawShadows(false)
                           .WithColor(kFR, COLOR_BLACK)
                           .WithFrameThickness(0.5f)
                           .WithWidgetFrac(0.5f)
                           .WithLabelOrientation(EOrientation::South))
  , IBitmapBase(bitmap)
  {
  }

  void DrawWidget(IGraphics& g) override
  {
    DrawTrack(g, mWidgetBounds);
    DrawHandle(g, mHandleBounds);
  }

  void DrawTrack(IGraphics& g, const IRECT& bounds) override
  {
    IRECT handleBounds = GetAdjustedHandleBounds(bounds);
    handleBounds = IRECT(handleBounds.L, handleBounds.T, handleBounds.R, handleBounds.T + mBitmap.H());
    IRECT centreBounds = handleBounds.GetPadded(-mStyle.shadowOffset);
    IRECT shadowBounds = handleBounds.GetTranslated(mStyle.shadowOffset, mStyle.shadowOffset);
    //    const float contrast = mDisabled ? -GRAYED_ALPHA : 0.f;
    float cR = 7.f;
    const float tlr = cR;
    const float trr = cR;
    const float blr = cR;
    const float brr = cR;

    // outer shadow
    if (mStyle.drawShadows)
      g.FillRoundRect(GetColor(kSH), shadowBounds, tlr, trr, blr, brr, &mBlend);

    // Embossed style unpressed
    if (mStyle.emboss)
    {
      // Positive light
      g.FillRoundRect(GetColor(kPR), handleBounds, tlr, trr, blr, brr /*, &blend*/);

      // Negative light
      g.FillRoundRect(GetColor(kSH), shadowBounds, tlr, trr, blr, brr /*, &blend*/);

      // Fill in foreground
      g.FillRoundRect(GetValue() > 0.5 ? GetColor(kX1) : COLOR_BLACK, centreBounds, tlr, trr, blr, brr, &mBlend);

      // Shade when hovered
      if (mMouseIsOver)
        g.FillRoundRect(GetColor(kHL), centreBounds, tlr, trr, blr, brr, &mBlend);
    }
    else
    {
      g.FillRoundRect(GetValue() > 0.5 ? GetColor(kX1) : COLOR_BLACK, handleBounds, tlr, trr, blr, brr /*, &blend*/);

      // Shade when hovered
      if (mMouseIsOver)
        g.FillRoundRect(GetColor(kHL), handleBounds, tlr, trr, blr, brr, &mBlend);
    }

    if (mStyle.drawFrame)
      g.DrawRoundRect(GetColor(kFR), handleBounds, tlr, trr, blr, brr, &mBlend, mStyle.frameThickness);
  }

  void DrawHandle(IGraphics& g, const IRECT& filledArea) override
  {
    IRECT r;
    if (GetSelectedIdx() == 0)
    {
      r = filledArea.GetFromLeft(mBitmap.W());
    }
    else
    {
      r = filledArea.GetFromRight(mBitmap.W());
    }

    g.DrawBitmap(mBitmap, r, 0, 0, nullptr);
  }
};

class NAMFileNameControl : public IVButtonControl
{
public:
  NAMFileNameControl(const IRECT& bounds, const char* label, const IVStyle& style)
  : IVButtonControl(bounds, DefaultClickActionFunc, label, style)
  {
  }

  void SetLabelAndTooltip(const char* str)
  {
    SetLabelStr(str);
    SetTooltip(str);
  }

  void SetLabelAndTooltipEllipsizing(const WDL_String& fileName)
  {
    auto EllipsizeFilePath = [](const char* filePath, size_t prefixLength, size_t suffixLength, size_t maxLength) {
      const std::string ellipses = "...";
      assert(maxLength <= (prefixLength + suffixLength + ellipses.size()));
      std::string str{filePath};

      if (str.length() <= maxLength)
      {
        return str;
      }
      else
      {
        return str.substr(0, prefixLength) + ellipses + str.substr(str.length() - suffixLength);
      }
    };

    auto ellipsizedFileName = EllipsizeFilePath(fileName.get_filepart(), 22, 22, 45);
    SetLabelStr(ellipsizedFileName.c_str());
    SetTooltip(fileName.get_filepart());
  }
};

// URL control for the "Get" models/irs links
class NAMGetButtonControl : public NAMSquareButtonControl
{
public:
  NAMGetButtonControl(const IRECT& bounds, const char* label, const char* url, const ISVG& globeSVG)
  : NAMSquareButtonControl(
      bounds,
      [url](IControl* pCaller) {
        WDL_String fullURL(url);
        auto* ui = pCaller->GetUI();
        ui->ReleaseMouseCapture();
        ui->OpenURL(fullURL.Get());
      },
      globeSVG)
  {
    SetTooltip(label);
  }
};

class NAMFileBrowserControl : public IDirBrowseControlBase
{
public:
  NAMFileBrowserControl(const IRECT& bounds, int clearMsgTag, const char* labelStr, const char* fileExtension,
                        IFileDialogCompletionHandlerFunc ch, const IVStyle& style, const ISVG& loadSVG,
                        const ISVG& clearSVG, const ISVG& leftSVG, const ISVG& rightSVG, const NAMTheme& theme,
                        const ISVG& globeSVG, const char* getButtonLabel, const char* getButtonURL)
  : IDirBrowseControlBase(bounds, fileExtension, false, false)
  , mClearMsgTag(clearMsgTag)
  , mDefaultLabelStr(labelStr)
  , mCompletionHandlerFunc(ch)
  , mStyle(style.WithColor(kFG, COLOR_TRANSPARENT).WithDrawFrame(false))
  , mTheme(theme)
  , mLoadSVG(loadSVG)
  , mClearSVG(clearSVG)
  , mLeftSVG(leftSVG)
  , mRightSVG(rightSVG)
  , mGlobeSVG(globeSVG)
  , mGetButtonLabel(getButtonLabel)
  , mGetButtonURL(getButtonURL)
  , mBrowserState(NAMBrowserState::Empty)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.5f), mRECT, 6.f, &mBlend);
    g.DrawRoundRect(mTheme.accent.WithOpacity(0.35f), mRECT, 6.f, &mBlend, 1.f);
    g.DrawLine(COLOR_WHITE.WithOpacity(0.05f), mRECT.L + 6.f, mRECT.B + 0.5f, mRECT.R - 6.f, mRECT.B + 0.5f, &mBlend);
  }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    if (pSelectedMenu)
    {
      IPopupMenu::Item* pItem = pSelectedMenu->GetChosenItem();

      if (pItem)
      {
        mSelectedItemIndex = mItems.Find(pItem);
        LoadFileAtCurrentIndex(NAMFileLoadSource::ExistingBrowserSelection);
      }
    }
  }

  void OnAttached() override
  {
    auto prevFileFunc = [&](IControl* pCaller) {
      const auto nItems = NItems();
      if (nItems == 0)
        return;
      mSelectedItemIndex--;

      if (mSelectedItemIndex < 0)
        mSelectedItemIndex = nItems - 1;

      LoadFileAtCurrentIndex(NAMFileLoadSource::ExistingBrowserSelection);
    };

    auto nextFileFunc = [&](IControl* pCaller) {
      const auto nItems = NItems();
      if (nItems == 0)
        return;
      mSelectedItemIndex++;

      if (mSelectedItemIndex >= nItems)
        mSelectedItemIndex = 0;

      LoadFileAtCurrentIndex(NAMFileLoadSource::ExistingBrowserSelection);
    };

    auto loadFileFunc = [&](IControl* pCaller) {
      WDL_String fileName;
      WDL_String path;
      GetSelectedFileDirectory(path);
#ifdef NAM_PICK_DIRECTORY
      pCaller->GetUI()->PromptForDirectory(path, [&](const WDL_String& fileName, const WDL_String& path) {
        if (path.GetLength())
        {
          ClearPathList();
          AddPath(path.Get(), "");
          SetupMenu();
          SelectFirstFile();
          LoadFileAtCurrentIndex(NAMFileLoadSource::ExistingBrowserSelection);
        }
      });
#else
      pCaller->GetUI()->PromptForFile(
        fileName, path, EFileAction::Open, mExtension.Get(), [&](const WDL_String& fileName, const WDL_String& path) {
          if (fileName.GetLength())
          {
            ClearPathList();
            AddPath(path.Get(), "");
            SetupMenu();
            SetSelectedFile(fileName.Get());
            LoadFileAtCurrentIndex(NAMFileLoadSource::FilePickerSelection, &fileName, &path);
          }
        });
#endif
    };

    auto clearFileFunc = [&](IControl* pCaller) {
      pCaller->GetDelegate()->SendArbitraryMsgFromUI(mClearMsgTag);
      mFileNameControl->SetLabelAndTooltip(mDefaultLabelStr.Get());
      SetBrowserState(NAMBrowserState::Empty);
      // FIXME disabling output mode...
      //      pCaller->GetUI()->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
    };

    auto chooseFileFunc = [&, loadFileFunc](IControl* pCaller) {
      if (std::string_view(pCaller->As<IVButtonControl>()->GetLabelStr()) == mDefaultLabelStr.Get())
      {
        loadFileFunc(pCaller);
      }
      else
      {
        CheckSelectedItem();

        if (!mMainMenu.HasSubMenus())
        {
          mMainMenu.SetChosenItemIdx(mSelectedItemIndex);
        }
        pCaller->GetUI()->CreatePopupMenu(*this, mMainMenu, pCaller->GetRECT());
      }
    };

    IRECT padded = mRECT.GetPadded(-6.f).GetHPadded(-2.f);
    const auto buttonWidth = padded.H();
    const auto loadFileButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto clearAndGetButtonBounds = padded.ReduceFromRight(buttonWidth);
    const auto leftButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto rightButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto fileNameButtonBounds = padded;

    AddChildControl(new NAMSquareButtonControl(loadFileButtonBounds, DefaultClickActionFunc, mLoadSVG))
      ->SetAnimationEndActionFunction(loadFileFunc);
    AddChildControl(new NAMSquareButtonControl(leftButtonBounds, DefaultClickActionFunc, mLeftSVG))
      ->SetAnimationEndActionFunction(prevFileFunc);
    AddChildControl(new NAMSquareButtonControl(rightButtonBounds, DefaultClickActionFunc, mRightSVG))
      ->SetAnimationEndActionFunction(nextFileFunc);
    AddChildControl(mFileNameControl = new NAMFileNameControl(fileNameButtonBounds, mDefaultLabelStr.Get(), mStyle))
      ->SetAnimationEndActionFunction(chooseFileFunc);

    // creates both right-side controls but only show one based on state
    mClearButton = new NAMSquareButtonControl(clearAndGetButtonBounds, DefaultClickActionFunc, mClearSVG);
    mClearButton->SetAnimationEndActionFunction(clearFileFunc);
    AddChildControl(mClearButton);

    mGetButton = new NAMGetButtonControl(clearAndGetButtonBounds, mGetButtonLabel, mGetButtonURL, mGlobeSVG);
    AddChildControl(mGetButton);

    // initialize control visibility
    SetBrowserState(NAMBrowserState::Empty);
  }

  void LoadFileAtCurrentIndex(NAMFileLoadSource source, const WDL_String* filePickerFileName = nullptr,
                              const WDL_String* filePickerPath = nullptr)
  {
    if (source == NAMFileLoadSource::FilePickerSelection && mSelectedItemIndex == -1 && filePickerFileName != nullptr
        && filePickerPath != nullptr)
    {
      ReportDirectoryScanFailure(*filePickerFileName, *filePickerPath);
      return;
    }

    if (mSelectedItemIndex < 0 || mSelectedItemIndex >= NItems())
    {
      ReportUnexpectedLoadFailure(source, filePickerFileName);
      return;
    }

    WDL_String fileName, path;
    GetSelectedFile(fileName);
    mFileNameControl->SetLabelAndTooltipEllipsizing(fileName);
    mCompletionHandlerFunc(fileName, path);
  }

  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    switch (msgTag)
    {
      case kMsgTagLoadFailed:
        // Honestly, not sure why I made a big stink of it before. Why not just say it failed and move on? :)
        {
          std::string label(std::string("(FAILED) ") + std::string(mFileNameControl->GetLabelStr()));
          mFileNameControl->SetLabelAndTooltip(label.c_str());
          SetBrowserState(NAMBrowserState::Empty);
        }
        break;
      case kMsgTagLoadedModel:
      case kMsgTagLoadedIR:
      {
        WDL_String fileName, directory;
        fileName.Set(reinterpret_cast<const char*>(pData));
        directory.Set(reinterpret_cast<const char*>(pData));
        directory.remove_filepart(true);

        ClearPathList();
        AddPath(directory.Get(), "");
        SetupMenu();
        SetSelectedFile(fileName.Get());
        mFileNameControl->SetLabelAndTooltipEllipsizing(fileName);
        SetBrowserState(NAMBrowserState::Loaded);
      }
      break;
      default: break;
    }
  }

private:
  void ReportDirectoryScanFailure(const WDL_String& fileName, const WDL_String& path)
  {
    mFileNameControl->SetLabelAndTooltipEllipsizing(fileName);
    const std::string label = std::string("(FAILED) ") + mFileNameControl->GetLabelStr();

    std::stringstream message;
    message << "The selected file '" << fileName.Get() << "' was not found after scanning directory '" << path.Get()
            << "'. The host may not have granted permission to enumerate that directory.";

    mFileNameControl->SetLabelStr(label.c_str());
    mFileNameControl->SetTooltip(message.str().c_str());
    SetBrowserState(NAMBrowserState::Empty);
    std::fprintf(stderr, "NAM: File picker selection produced index -1. %s\n", message.str().c_str());
  }

  void ReportUnexpectedLoadFailure(NAMFileLoadSource source, const WDL_String* filePickerFileName)
  {
    if (filePickerFileName != nullptr)
      mFileNameControl->SetLabelAndTooltipEllipsizing(*filePickerFileName);

    const std::string label = std::string("(FAILED) ") + mFileNameControl->GetLabelStr();
    const std::string message =
      "The selected file could not be loaded because the file browser encountered an "
      "unexpected selection state. Please select the file again.";

    mFileNameControl->SetLabelStr(label.c_str());
    mFileNameControl->SetTooltip(message.c_str());
    SetBrowserState(NAMBrowserState::Empty);
    std::fprintf(stderr, "NAM: %s Source: %s; selected index: %d; item count: %d.\n", message.c_str(),
                 source == NAMFileLoadSource::FilePickerSelection ? "file picker" : "existing browser selection",
                 mSelectedItemIndex, NItems());
  }

  void SelectFirstFile() { mSelectedItemIndex = mFiles.GetSize() ? 0 : -1; }

  void GetSelectedFileDirectory(WDL_String& path)
  {
    GetSelectedFile(path);
    path.remove_filepart();
    return;
  }

  // set the state of the browser and the visibility of the "Get" vs. "Clear" buttons
  void SetBrowserState(NAMBrowserState newState)
  {
    mBrowserState = newState;

    switch (mBrowserState)
    {
      case NAMBrowserState::Empty:
        mClearButton->Hide(true);
        mGetButton->Hide(false);
        break;
      case NAMBrowserState::Loaded:
        mClearButton->Hide(false);
        mGetButton->Hide(true);
        break;
    }
  }

  WDL_String mDefaultLabelStr;
  IFileDialogCompletionHandlerFunc mCompletionHandlerFunc;
  NAMFileNameControl* mFileNameControl = nullptr;
  IVStyle mStyle;
  const NAMTheme& mTheme;
  ISVG mLoadSVG, mClearSVG, mLeftSVG, mRightSVG, mGlobeSVG;
  int mClearMsgTag;

  // new members for the "Get" button
  const char* mGetButtonLabel;
  const char* mGetButtonURL;
  NAMBrowserState mBrowserState;
  NAMSquareButtonControl* mClearButton = nullptr;
  NAMGetButtonControl* mGetButton = nullptr;
};

class NAMMeterControl : public IVPeakAvgMeterControl<>
{
  static constexpr float KMeterMin = -70.0f;
  static constexpr float KMeterMax = -0.01f;

public:
  NAMMeterControl(const IRECT& bounds, const IVStyle& style, const NAMTheme& theme)
  : IVPeakAvgMeterControl<>(bounds, "", style.WithShowValue(false).WithDrawFrame(false).WithWidgetFrac(0.8),
                            EDirection::Vertical, {}, 0, KMeterMin, KMeterMax, {})
  , mTheme(theme)
  {
    SetPeakSize(1.0f);
    mPeakValues.fill(0.f);
  }

  // Segmented LED ladder: accent up to -12 dB, amber to -3 dB, red above.
  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.55f), mRECT, 4.f, &mBlend);
    g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.06f), mRECT, 4.f, &mBlend, 1.f);

    const IRECT track = mRECT.GetPadded(-3.f);
    const float segH = 4.f, gap = 1.5f;
    const int nSegs = static_cast<int>((track.H() + gap) / (segH + gap));
    const float level = static_cast<float>(GetValue(0));
    const float peak = mPeakValues[0];
    const int peakSeg = peak > 0.0001f ? std::min(nSegs - 1, static_cast<int>(peak * nSegs)) : -1;
    const IColor amber(255, 242, 179, 61), red(255, 255, 64, 64);

    for (int i = 0; i < nSegs; i++)
    {
      const float frac = (i + 0.5f) / nSegs;
      const float bottom = track.B - i * (segH + gap);
      const IRECT seg(track.L, bottom - segH, track.R, bottom);
      const IColor c = frac < 0.83f ? mTheme.accent : (frac < 0.96f ? amber : red);
      const bool lit = frac <= level || i == peakSeg;
      g.FillRoundRect(lit ? c : c.WithOpacity(0.1f), seg, 1.f, &mBlend);
    }
  }

private:
  const NAMTheme& mTheme;
};

// Container where we can refer to children by names instead of indices
class IContainerBaseWithNamedChildren : public IContainerBase
{
public:
  IContainerBaseWithNamedChildren(const IRECT& bounds)
  : IContainerBase(bounds) {};
  ~IContainerBaseWithNamedChildren() = default;

protected:
  IControl* AddNamedChildControl(IControl* control, std::string name, int ctrlTag = kNoTag, const char* group = "")
  {
    // Make sure we haven't already used this name
    assert(mChildNameIndexMap.find(name) == mChildNameIndexMap.end());
    mChildNameIndexMap[name] = NChildren();
    return AddChildControl(control, ctrlTag, group);
  };

  IControl* GetNamedChild(std::string name)
  {
    const int index = mChildNameIndexMap[name];
    return GetChild(index);
  };


private:
  std::unordered_map<std::string, int> mChildNameIndexMap;
}; // class IContainerBaseWithNamedChildren


struct PossiblyKnownParameter
{
  bool known = false;
  double value = 0.0;
};

struct ModelInfo
{
  PossiblyKnownParameter sampleRate;
  PossiblyKnownParameter inputCalibrationLevel;
  PossiblyKnownParameter outputCalibrationLevel;
};

class ModelInfoControl : public IContainerBaseWithNamedChildren
{
public:
  ModelInfoControl(const IRECT& bounds, const IVStyle& style)
  : IContainerBaseWithNamedChildren(bounds)
  , mStyle(style) {};

  void ClearModelInfo()
  {
    static_cast<IVLabelControl*>(GetNamedChild(mControlNames.sampleRate))->SetStr("");
    mHasInfo = false;
  };

  void Hide(bool hide) override
  {
    // Don't show me unless I have info to show!
    IContainerBase::Hide(hide || (!mHasInfo));
  };

  void OnAttached() override
  {
    AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(4, 0), "Model information:", mStyle));
    AddNamedChildControl(new IVLabelControl(GetRECT().SubRectVertical(4, 1), "", mStyle), mControlNames.sampleRate);
    // AddNamedChildControl(
    //   new IVLabelControl(GetRECT().SubRectVertical(4, 2), "", mStyle), mControlNames.inputCalibrationLevel);
    // AddNamedChildControl(
    //   new IVLabelControl(GetRECT().SubRectVertical(4, 3), "", mStyle), mControlNames.outputCalibrationLevel);
  };

  void SetModelInfo(const ModelInfo& modelInfo)
  {
    auto SetControlStr = [&](const std::string& name, const PossiblyKnownParameter& p, const std::string& units,
                             const std::string& childName) {
      std::stringstream ss;
      ss << name << ": ";
      if (p.known)
      {
        ss << p.value << " " << units;
      }
      else
      {
        ss << "(Unknown)";
      }
      static_cast<IVLabelControl*>(GetNamedChild(childName))->SetStr(ss.str().c_str());
    };

    SetControlStr("Sample rate", modelInfo.sampleRate, "Hz", mControlNames.sampleRate);
    // SetControlStr(
    //   "Input calibration level", modelInfo.inputCalibrationLevel, "dBu", mControlNames.inputCalibrationLevel);
    // SetControlStr(
    //   "Output calibration level", modelInfo.outputCalibrationLevel, "dBu", mControlNames.outputCalibrationLevel);

    mHasInfo = true;
  };

private:
  const IVStyle mStyle;
  struct
  {
    const std::string sampleRate = "sampleRate";
    // const std::string inputCalibrationLevel = "inputCalibrationLevel";
    // const std::string outputCalibrationLevel = "outputCalibrationLevel";
  } mControlNames;
  // Do I have info?
  bool mHasInfo = false;
};

class OutputModeControl : public IVRadioButtonControl
{
public:
  OutputModeControl(const IRECT& bounds, int paramIdx, const IVStyle& style, float buttonSize)
  : IVRadioButtonControl(
      bounds, paramIdx, {}, "Output Mode", style, EVShape::Ellipse, EDirection::Vertical, buttonSize) {};

  void SetNormalizedDisable(const bool disable)
  {
    // HACK non-DRY string and hard-coded indices
    std::stringstream ss;
    ss << "Normalized";
    if (disable)
    {
      ss << " [Not supported by model]";
    }
    mTabLabels.Get(1)->Set(ss.str().c_str());
  };
  void SetCalibratedDisable(const bool disable)
  {
    // HACK non-DRY string and hard-coded indices
    std::stringstream ss;
    ss << "Calibrated";
    if (disable)
    {
      ss << " [Not supported by model]";
    }
    mTabLabels.Get(2)->Set(ss.str().c_str());
  };
};

class NAMSettingsPageControl : public IContainerBaseWithNamedChildren
{
public:
  NAMSettingsPageControl(const IRECT& bounds, const NAMTheme& theme, const IBitmap& inputLevelBackgroundBitmap,
                         const IBitmap& switchBitmap, ISVG closeSVG, const IVStyle& style,
                         const IVStyle& radioButtonStyle)
  : IContainerBaseWithNamedChildren(bounds)
  , mAnimationTime(0)
  , mTheme(theme)
  , mInputLevelBackgroundBitmap(inputLevelBackgroundBitmap)
  , mSwitchBitmap(switchBitmap)
  , mStyle(style)
  , mRadioButtonStyle(radioButtonStyle)
  , mCloseSVG(closeSVG)
  {
    mIgnoreMouse = false;
  }

  void ClearModelInfo()
  {
    auto* modelInfoControl = static_cast<ModelInfoControl*>(GetNamedChild(mControlNames.modelInfo));
    assert(modelInfoControl != nullptr);
    modelInfoControl->ClearModelInfo();
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    if (key.VK == kVK_ESCAPE)
    {
      HideAnimated(true);
      return true;
    }

    return false;
  }

  void HideAnimated(bool hide)
  {
    mWillHide = hide;

    if (hide == false)
    {
      mHide = false;
    }
    else // hide subcontrols immediately
    {
      ForAllChildrenFunc([hide](int childIdx, IControl* pChild) { pChild->Hide(hide); });
    }

    SetAnimation(
      [&](IControl* pCaller) {
        auto progress = static_cast<float>(pCaller->GetAnimationProgress());

        if (mWillHide)
          SetBlend(IBlend(EBlend::Default, 1.0f - progress));
        else
          SetBlend(IBlend(EBlend::Default, progress));

        if (progress > 1.0f)
        {
          pCaller->OnEndAnimation();
          IContainerBase::Hide(mWillHide);
          GetUI()->SetAllControlsDirty();
          return;
        }
      },
      mAnimationTime);

    SetDirty(true);
  }

  void OnAttached() override
  {
    const float pad = 20.0f;
    const IVStyle titleStyle = DEFAULT_STYLE.WithValueText(IText(30, COLOR_WHITE, "Michroma-Regular"))
                                 .WithDrawFrame(false)
                                 .WithShadowOffset(2.f);
    const auto text = IText(DEFAULT_TEXT_SIZE, EAlign::Center, PluginColors::HELP_TEXT);
    const auto leftText = text.WithAlign(EAlign::Near);
    const auto style = mStyle.WithDrawFrame(false).WithValueText(text);
    const IVStyle leftStyle = style.WithValueText(leftText);

    AddNamedChildControl(new NAMRig::PanelControl(GetRECT(), mTheme, false), mControlNames.bitmap);
    const auto titleArea = GetRECT().GetPadded(-(pad + 10.0f)).GetFromTop(50.0f);
    AddNamedChildControl(new IVLabelControl(titleArea, "SETTINGS", titleStyle), mControlNames.title);

    // Attach input/output calibration controls
    {
      const float height = NAM_KNOB_HEIGHT + NAM_SWTICH_HEIGHT + 10.0f;
      const float width = titleArea.W();
      const auto inputOutputArea = titleArea.GetFromBottom(height).GetTranslated(0.0f, height);
      const auto inputArea = inputOutputArea.GetFromLeft(0.5f * width);
      const auto outputArea = inputOutputArea.GetFromRight(0.5f * width);

      const float knobWidth = 87.0f; // HACK based on looking at the main page knobs.
      const auto inputLevelArea =
        inputArea.GetFromTop(NAM_KNOB_HEIGHT).GetFromBottom(25.0f).GetMidHPadded(0.5f * knobWidth);
      const auto inputSwitchArea = inputArea.GetFromBottom(NAM_SWTICH_HEIGHT).GetMidHPadded(0.5f * knobWidth);

      auto* inputLevelControl = AddNamedChildControl(
        new InputLevelControl(inputLevelArea, kInputCalibrationLevel, mInputLevelBackgroundBitmap, text),
        mControlNames.inputCalibrationLevel, kCtrlTagInputCalibrationLevel);
      inputLevelControl->SetTooltip(
        "The analog level, in dBu RMS, that corresponds to digital level of 0 dBFS peak in the host as its signal "
        "enters this plugin.");
      AddNamedChildControl(
        new NAMSwitchControl(inputSwitchArea, kCalibrateInput, "Calibrate Input", mStyle, mSwitchBitmap),
        mControlNames.calibrateInput, kCtrlTagCalibrateInput);

      // Same-ish height & width as input controls
      const auto outputRadioArea = outputArea.GetFromBottom(
        1.1f * (inputLevelArea.H() + inputSwitchArea.H())); // .GetMidHPadded(0.55f * knobWidth);
      const float buttonSize = 10.0f;
      auto* outputModeControl =
        AddNamedChildControl(new OutputModeControl(outputRadioArea, kOutputMode, mRadioButtonStyle, buttonSize),
                             mControlNames.outputMode, kCtrlTagOutputMode);
      outputModeControl->SetTooltip(
        "How to adjust the level of the output.\nRaw=No adjustment.\nNormalized=Adjust the level so that all models "
        "are about the same loudness.\nCalibrated=Match the input's digital-analog calibration.");
    }

    const float halfWidth = PLUG_WIDTH / 2.0f - pad;
    const auto bottomArea = GetRECT().GetPadded(-pad).GetFromBottom(93.0f);
    const float lineHeight = 15.0f;
    const auto modelInfoArea = bottomArea.GetFromLeft(halfWidth).GetFromTop(4 * lineHeight);
    const auto aboutArea = bottomArea.GetFromRight(halfWidth).GetFromTop(6 * lineHeight);
    AddNamedChildControl(new ModelInfoControl(modelInfoArea, leftStyle), mControlNames.modelInfo);
    AddNamedChildControl(new AboutControl(aboutArea, leftStyle, leftText), mControlNames.about);

    auto closeAction = [&](IControl* pCaller) {
      static_cast<NAMSettingsPageControl*>(pCaller->GetParent())->HideAnimated(true);
    };
    AddNamedChildControl(
      new NAMSquareButtonControl(CornerButtonArea(GetRECT()), closeAction, mCloseSVG), mControlNames.close);

    OnResize();
  }

  void SetModelInfo(const ModelInfo& modelInfo)
  {
    auto* modelInfoControl = static_cast<ModelInfoControl*>(GetNamedChild(mControlNames.modelInfo));
    assert(modelInfoControl != nullptr);
    modelInfoControl->SetModelInfo(modelInfo);
  };

private:
  const NAMTheme& mTheme;
  IBitmap mInputLevelBackgroundBitmap;
  IBitmap mSwitchBitmap;
  IVStyle mStyle;
  IVStyle mRadioButtonStyle;
  ISVG mCloseSVG;
  int mAnimationTime = 200;
  bool mWillHide = false;

  // Names for controls
  // Make sure that these are all unique and that you use them with AddNamedChildControl
  struct ControlNames
  {
    const std::string about = "About";
    const std::string bitmap = "Bitmap";
    const std::string calibrateInput = "CalibrateInput";
    const std::string close = "Close";
    const std::string inputCalibrationLevel = "InputCalibrationLevel";
    const std::string modelInfo = "ModelInfo";
    const std::string outputMode = "OutputMode";
    const std::string title = "Title";
  } mControlNames;

  class InputLevelControl : public IEditableTextControl
  {
  public:
    InputLevelControl(const IRECT& bounds, int paramIdx, const IBitmap& bitmap, const IText& text = DEFAULT_TEXT,
                      const IColor& BGColor = DEFAULT_BGCOLOR)
    : IEditableTextControl(bounds, "", text, BGColor)
    , mBitmap(bitmap)
    {
      SetParamIdx(paramIdx);
    };

    void Draw(IGraphics& g) override
    {
      g.DrawFittedBitmap(mBitmap, mRECT);
      ITextControl::Draw(g);
    };

    void SetValueFromUserInput(double normalizedValue, int valIdx) override
    {
      IControl::SetValueFromUserInput(normalizedValue, valIdx);
      const std::string s = ConvertToString(normalizedValue);
      OnTextEntryCompletion(s.c_str(), valIdx);
    };

    void SetValueFromDelegate(double normalizedValue, int valIdx) override
    {
      IControl::SetValueFromDelegate(normalizedValue, valIdx);
      const std::string s = ConvertToString(normalizedValue);
      SetStr(s.c_str());
      SetDirty(false);
    };

  private:
    std::string ConvertToString(const double normalizedValue)
    {
      const double naturalValue = GetParam()->FromNormalized(normalizedValue);
      // And make the value to display
      std::stringstream ss;
      ss << naturalValue << " dBu";
      std::string s = ss.str();
      return s;
    };

    IBitmap mBitmap;
  };

  class AboutControl : public IContainerBase
  {
  public:
    AboutControl(const IRECT& bounds, const IVStyle& style, const IText& text)
    : IContainerBase(bounds)
    , mStyle(style)
    , mText(text) {};

    void OnAttached() override
    {
      WDL_String verStr, buildInfoStr;
      PLUG()->GetPluginVersionStr(verStr);

      buildInfoStr.SetFormatted(100, "Version %s %s %s", verStr.Get(), PLUG()->GetArchStr(), PLUG()->GetAPIStr());

      AddChildControl(new IURLControl(GetRECT().SubRectVertical(6, 0), "NEURAL AMP MODELER",
                                      "https://www.neuralampmodeler.com", mText, COLOR_TRANSPARENT,
                                      PluginColors::HELP_TEXT_MO, PluginColors::HELP_TEXT_CLICKED));
      AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(6, 1), "By Steven Atkinson", mStyle));
      AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(6, 2), buildInfoStr.Get(), mStyle));
      AddChildControl(new IURLControl(GetRECT().SubRectVertical(6, 3),
                                      "Plug-in development: Steve Atkinson, Oli Larkin, ... ",
                                      "https://github.com/sdatkinson/NeuralAmpModelerPlugin/graphs/contributors", mText,
                                      COLOR_TRANSPARENT, PluginColors::HELP_TEXT_MO, PluginColors::HELP_TEXT_CLICKED));
      AddChildControl(new IURLControl(GetRECT().SubRectVertical(6, 4), "UI design: Starquake Music",
                                      "https://github.com/StarquakeMusic/NeuralAmpModelerPlugin", mText,
                                      COLOR_TRANSPARENT, PluginColors::HELP_TEXT_MO, PluginColors::HELP_TEXT_CLICKED));
      AddChildControl(new ThirdPartyNoticesControl(GetRECT().SubRectVertical(6, 5), mText));
    };

  private:
    class ThirdPartyNoticesControl : public IURLControl
    {
    public:
      ThirdPartyNoticesControl(const IRECT& bounds, const IText& text)
      : IURLControl(bounds, "Third party notices", "", text, COLOR_TRANSPARENT, PluginColors::HELP_TEXT_MO,
                    PluginColors::HELP_TEXT_CLICKED)
      {
      }

      void OnMouseDown(float x, float y, const IMouseMod& mod) override
      {
        WDL_String path;
        bool opened = false;

        if (ResolveNoticesPath(GetUI(), path))
          opened = OpenNoticesPath(GetUI(), path);

        if (!opened)
          ShowOpenError(GetUI());

        GetUI()->ReleaseMouseCapture();
        mClicked = true;
        SetDirty(false);
      }

    private:
      static bool FileExists(const WDL_String& path)
      {
        if (!CStringHasContents(path.Get()))
          return false;

        FILE* file = WDL_fopenA(path.Get(), "rb");
        if (file == nullptr)
          return false;

        fclose(file);
        return true;
      }

      static bool TryNoticePathInDirectory(WDL_String& result, const WDL_String& directory)
      {
        if (!CStringHasContents(directory.Get()))
          return false;

        WDL_String candidate(directory);
        const char lastChar = candidate.Get()[candidate.GetLength() - 1];

        if (!WDL_IS_DIRCHAR(lastChar))
          candidate.Append(WDL_DIRCHAR_STR);

        candidate.Append(kNoticesFileName);

        if (!FileExists(candidate))
          return false;

        result.Set(candidate.Get());
        return true;
      }

      // AAX (and similar) load the binary from Contents\x64 or Contents\Win32 while notices live in
      // Contents\Resources. Same layout as a VST3 bundle; this path is not covered by BundleResourcePath
      // when the plug-in is built as AAX_API only (no VST3_API).
      static bool TryNoticePathSiblingResources(WDL_String& result, const WDL_String& moduleDirectory)
      {
        if (!CStringHasContents(moduleDirectory.Get()))
          return false;

        WDL_String candidate(moduleDirectory);
        const char lastChar = candidate.Get()[candidate.GetLength() - 1];
        if (!WDL_IS_DIRCHAR(lastChar))
          candidate.Append(WDL_DIRCHAR_STR);

        candidate.Append("..");
        candidate.Append(WDL_DIRCHAR_STR);
        candidate.Append("Resources");
        candidate.Append(WDL_DIRCHAR_STR);
        candidate.Append(kNoticesFileName);

        if (!FileExists(candidate))
          return false;

        result.Set(candidate.Get());
        return true;
      }

      static bool ResolveNoticesPath(IGraphics* pGraphics, WDL_String& path)
      {
        path.Set("");

        if (pGraphics == nullptr)
          return false;

#ifdef OS_WIN
        WDL_String directory;
        const auto moduleHandle = static_cast<PluginIDType>(pGraphics->GetWinModuleHandle());

        BundleResourcePath(directory, moduleHandle);
        if (TryNoticePathInDirectory(path, directory))
          return true;

        directory.Set("");
        PluginPath(directory, moduleHandle);
        if (TryNoticePathInDirectory(path, directory))
          return true;

        if (TryNoticePathSiblingResources(path, directory))
          return true;
#endif

        const auto resourceLocation =
          LocateResource(kNoticesFileName, "txt", path, pGraphics->GetBundleID(), pGraphics->GetWinModuleHandle(),
                         pGraphics->GetSharedResourcesSubPath());

        return resourceLocation == EResourceLocation::kAbsolutePath && FileExists(path);
      }

      static bool OpenNoticesPath(IGraphics* pGraphics, const WDL_String& path)
      {
        if (pGraphics == nullptr || !CStringHasContents(path.Get()))
          return false;

#ifdef OS_WIN
        WCHAR pathWide[IPLUG_WIN_MAX_WIDE_PATH];
        UTF8ToUTF16(pathWide, path.Get(), IPLUG_WIN_MAX_WIDE_PATH);

        if (pathWide[0] == 0)
          return false;

        WCHAR canon[IPLUG_WIN_MAX_WIDE_PATH];
        const DWORD nCanon = GetFullPathNameW(pathWide, IPLUG_WIN_MAX_WIDE_PATH, canon, nullptr);
        const WCHAR* const launchPath = (nCanon > 0 && nCanon < IPLUG_WIN_MAX_WIDE_PATH) ? canon : pathWide;

        return ShellExecuteW(nullptr, L"open", launchPath, nullptr, nullptr, SW_SHOWNORMAL) > HINSTANCE(32);
#else
        return pGraphics->OpenURL(path.Get());
#endif
      }

      static void ShowOpenError(IGraphics* pGraphics)
      {
        if (pGraphics == nullptr)
          return;

        const char* const title = "Third party notices";
        const char* const message = "Could not open ThirdPartyNotices.txt.";

#ifdef OS_MAC
        pGraphics->ShowMessageBox(title, message, kMB_OK);
#else
        pGraphics->ShowMessageBox(message, title, kMB_OK);
#endif
      }

      static constexpr const char* kNoticesFileName = "ThirdPartyNotices.txt";
    };

    IVStyle mStyle;
    IText mText;
  };
};
