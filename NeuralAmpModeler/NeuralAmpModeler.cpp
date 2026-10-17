#include <algorithm> // std::clamp, std::min
#include <cmath> // pow
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <utility>

#include "Colors.h"
#include "../NeuralAmpModelerCore/NAM/activations.h"
#include "../NeuralAmpModelerCore/NAM/get_dsp.h"
// clang-format off
// These includes need to happen in this order or else the latter won't know
// a bunch of stuff.
#include "NeuralAmpModeler.h"
#include "IPlug_include_in_plug_src.h"
// clang-format on
#include "architecture.hpp"

#include "NeuralAmpModelerControls.h"
#include "NAMLibraryControl.h"

using namespace iplug;
using namespace igraphics;

const double kDCBlockerFrequency = 5.0;

// Styles
const IVColorSpec colorSpec{
  DEFAULT_BGCOLOR, // Background
  PluginColors::NAM_THEMECOLOR, // Foreground
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.3f), // Pressed
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.4f), // Frame
  PluginColors::MOUSEOVER, // Highlight
  DEFAULT_SHCOLOR, // Shadow
  PluginColors::NAM_THEMECOLOR, // Extra 1
  COLOR_RED, // Extra 2 --> color for clipping in meters
  PluginColors::NAM_THEMECOLOR.WithContrast(0.1f), // Extra 3
};

const IVStyle style =
  IVStyle{true, // Show label
          true, // Show value
          colorSpec,
          {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Middle, PluginColors::NAM_THEMEFONTCOLOR}, // Knob label text5
          {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Bottom, PluginColors::NAM_THEMEFONTCOLOR}, // Knob value text
          DEFAULT_HIDE_CURSOR,
          DEFAULT_DRAW_FRAME,
          false,
          DEFAULT_EMBOSS,
          0.2f,
          2.f,
          DEFAULT_SHADOW_OFFSET,
          DEFAULT_WIDGET_FRAC,
          DEFAULT_WIDGET_ANGLE};
const IVStyle titleStyle =
  DEFAULT_STYLE.WithValueText(IText(30, COLOR_WHITE, "Michroma-Regular")).WithDrawFrame(false).WithShadowOffset(2.f);
const IVStyle radioButtonStyle =
  style
    .WithColor(EVColor::kON, PluginColors::NAM_THEMECOLOR) // Pressed buttons and their labels
    .WithColor(EVColor::kOFF, PluginColors::NAM_THEMECOLOR.WithOpacity(0.1f)) // Unpressed buttons
    .WithColor(EVColor::kX1, PluginColors::NAM_THEMECOLOR.WithOpacity(0.6f)); // Unpressed buttons' labels

EMsgBoxResult _ShowMessageBox(iplug::igraphics::IGraphics* pGraphics, const char* str, const char* caption,
                              EMsgBoxType type)
{
#ifdef OS_MAC
  // macOS is backwards?
  return pGraphics->ShowMessageBox(caption, str, type);
#else
  return pGraphics->ShowMessageBox(str, caption, type);
#endif
}

// Steps a sine up through the capture's input range and records how loud it comes out, plus one cycle of the output
// at the hottest step. Leaves the model mid-signal; whoever uses it next resets it.
NAMFingerprint MeasureFingerprint(nam::DSP& model, const double sampleRate)
{
  NAMFingerprint fp;
  const int blockSize = 256;
  const int nSteps = 25;
  const int stepLength = 2048, measureLength = 1024;
  const double frequency = 220.0;
  const int cycleLength = static_cast<int>(std::round(sampleRate / frequency));
  if (cycleLength < 8 || 2 * cycleLength > stepLength)
    return fp;

  try
  {
    model.ResetAndPrewarm(sampleRate, blockSize);
    std::vector<NAM_SAMPLE> input(blockSize), output(blockSize), tail;
    NAM_SAMPLE* inputPointer = input.data();
    NAM_SAMPLE* outputPointer = output.data();
    double phase = 0.0;
    const double dPhase = 2.0 * PI * frequency / sampleRate;
    const float stepDb = (fp.inDbHigh - fp.inDbLow) / (nSteps - 1);

    fp.outDb.resize(nSteps);
    for (int step = 0; step < nSteps; step++)
    {
      const double amplitude = DBToAmp(fp.inDbLow + stepDb * step);
      const bool last = step == nSteps - 1;
      double sumSquares = 0.0;
      for (int pos = 0; pos < stepLength; pos += blockSize)
      {
        const int n = std::min(blockSize, stepLength - pos);
        for (int i = 0; i < n; i++)
        {
          input[i] = static_cast<NAM_SAMPLE>(amplitude * std::sin(phase));
          phase = std::fmod(phase + dPhase, 2.0 * PI);
        }
        model.process(&inputPointer, &outputPointer, n);
        for (int i = 0; i < n; i++)
        {
          const int t = pos + i;
          if (t >= stepLength - measureLength)
            sumSquares += output[i] * output[i];
          if (last && t >= stepLength - 2 * cycleLength)
            tail.push_back(output[i]);
        }
      }
      // Peak of the sine with the same RMS, so it reads on the same scale as the input
      const double rms = std::sqrt(sumSquares / measureLength);
      fp.outDb[step] = static_cast<float>(20.0 * std::log10(std::max(rms * std::sqrt(2.0), 1e-6)));
      if (!std::isfinite(fp.outDb[step]))
        return NAMFingerprint();
    }

    const int span = static_cast<int>(std::round(24.f / stepDb));
    const float dOut = fp.outDb[nSteps - 1] - fp.outDb[nSteps - 1 - span];
    fp.compression = dOut > 0.f ? std::clamp(24.f / dOut, 0.1f, 99.f) : 99.f;
    for (int i = 0; i + 1 < nSteps; i++)
      if ((fp.outDb[i + 1] - fp.outDb[i]) / stepDb < 0.5f)
      {
        fp.onsetDb = fp.inDbLow + stepDb * i;
        break;
      }

    // One cycle, starting at a rising zero crossing, DC removed
    int start = 0;
    for (int i = 1; i < cycleLength; i++)
      if (tail[i - 1] <= 0.0 && tail[i] > 0.0)
      {
        start = i;
        break;
      }
    double mean = 0.0;
    for (int i = 0; i < cycleLength; i++)
      mean += tail[start + i];
    mean /= cycleLength;
    const int nPoints = 96;
    double peak = 1e-9;
    fp.wave.resize(nPoints);
    for (int k = 0; k < nPoints; k++)
    {
      const double f = static_cast<double>(k) * (cycleLength - 1) / (nPoints - 1);
      const int i0 = static_cast<int>(f);
      const int i1 = std::min(i0 + 1, cycleLength - 1);
      const double v = tail[start + i0] + (tail[start + i1] - tail[start + i0]) * (f - i0) - mean;
      fp.wave[k] = static_cast<float>(v);
      peak = std::max(peak, std::fabs(v));
    }
    for (auto& v : fp.wave)
      v = static_cast<float>(v / peak);
    fp.valid = true;
  }
  catch (std::exception& e)
  {
    std::cerr << "Couldn't measure the model's fingerprint: " << e.what() << std::endl;
    return NAMFingerprint();
  }
  return fp;
}

// Marks the rig's additions to the saved state; see SerializeState().
const char* const kRigStateMarker = "###NAMRig###";

const std::string kCalibrateInputParamName = "CalibrateInput";
const bool kDefaultCalibrateInput = false;
const std::string kInputCalibrationLevelParamName = "InputCalibrationLevel";
const double kDefaultInputCalibrationLevel = 12.0;


NeuralAmpModeler::NeuralAmpModeler(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  _InitToneStack();
  nam::activations::Activation::enable_fast_tanh();
  GetParam(kInputLevel)->InitGain("Input", 0.0, -20.0, 20.0, 0.1);
  GetParam(kToneBass)->InitDouble("Bass", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneMid)->InitDouble("Middle", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneTreble)->InitDouble("Treble", 5.0, 0.0, 10.0, 0.1);
  GetParam(kOutputLevel)->InitGain("Output", 0.0, -40.0, 40.0, 0.1);
  GetParam(kNoiseGateThreshold)->InitGain("Threshold", -80.0, -100.0, 0.0, 0.1);
  GetParam(kNoiseGateActive)->InitBool("NoiseGateActive", true);
  GetParam(kEQActive)->InitBool("ToneStack", true);
  GetParam(kOutputMode)->InitEnum("OutputMode", 1, {"Raw", "Normalized", "Calibrated"}); // TODO DRY w/ control
  GetParam(kIRToggle)->InitBool("IRToggle", true);
  GetParam(kCalibrateInput)->InitBool(kCalibrateInputParamName.c_str(), kDefaultCalibrateInput);
  GetParam(kInputCalibrationLevel)
    ->InitDouble(kInputCalibrationLevelParamName.c_str(), kDefaultInputCalibrationLevel, -60.0, 60.0, 0.1, "dBu");
  GetParam(kSlim)->InitDouble("Slim", 0.0, 0.0, 1.0, 0.01);
  GetParam(kMix)->InitDouble("Mix", 100.0, 0.0, 100.0, 0.1, "%");
  GetParam(kMicPosition)->InitDouble("MicPosition", 0.0, 0.0, 100.0, 0.1, "%");
  GetParam(kMicDistance)->InitDouble("MicDistance", 0.0, 0.0, 100.0, 0.1, "%");

  mNoiseGateTrigger.AddListener(&mNoiseGateGain);

  mMakeGraphicsFunc = [&]() {

#ifdef OS_IOS
    auto scaleFactor = GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT) * 0.85f;
#else
    auto scaleFactor = 1.0f;
#endif

    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, scaleFactor);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachTextEntryControl();
    pGraphics->EnableMouseOver(true);
    pGraphics->EnableTooltips(true);
    pGraphics->EnableMultiTouch(true);

    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->LoadFont("Michroma-Regular", MICHROMA_FN);

    const auto gearSVG = pGraphics->LoadSVG(GEAR_FN);
    const auto fileSVG = pGraphics->LoadSVG(FILE_FN);
    const auto globeSVG = pGraphics->LoadSVG(GLOBE_ICON_FN);
    const auto crossSVG = pGraphics->LoadSVG(CLOSE_BUTTON_FN);
    const auto rightArrowSVG = pGraphics->LoadSVG(RIGHT_ARROW_FN);
    const auto leftArrowSVG = pGraphics->LoadSVG(LEFT_ARROW_FN);
    const auto irIconOnSVG = pGraphics->LoadSVG(IR_ICON_ON_FN);
    const auto irIconOffSVG = pGraphics->LoadSVG(IR_ICON_OFF_FN);
    const auto slimIconSVG = pGraphics->LoadSVG(SLIMMABLE_ICON_FN);

    const auto inputLevelBackgroundBitmap = pGraphics->LoadBitmap(INPUTLEVELBACKGROUND_FN);
    const auto switchHandleBitmap = pGraphics->LoadBitmap(SLIDESWITCHHANDLE_FN);

#ifdef OS_MAC
    // Gear-flavored lettering when the system has it; Michroma otherwise. Read from the file because iPlug2 can't pick
    // a face out of the .ttc collections most system fonts ship in, and these two are single-face.
    auto loadSystemFont = [pGraphics](const char* fontID, const char* fileName) {
      for (const char* dir : {"/System/Library/Fonts/Supplemental/", "/Library/Fonts/"})
      {
        std::ifstream file(std::string(dir) + fileName, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!data.empty() && pGraphics->LoadFont(fontID, data.data(), static_cast<int>(data.size())))
          return true;
      }
      return false;
    };
    if (loadSystemFont("NAM-Script", "Brush Script.ttf"))
      mTheme.scriptFont = "NAM-Script";
    if (loadSystemFont("NAM-Stomp", "Impact.ttf"))
      mTheme.stompFont = "NAM-Stomp";
#endif

    // Signal flows left to right: input meter, capture bay (the model) with its fingerprint, cab (the IR), output
    // meter. The knobs live on a pedalboard underneath, patched in the same order the DSP runs.
    const auto b = pGraphics->GetBounds();
    const auto rigArea = b.GetPadded(-56.f, -72.f, -56.f, -16.f);
    const auto heroArea = rigArea.GetFromTop(180.f);
    const auto fingerprintArea = heroArea.GetFromLeft(172.f);
    const auto cabArea = heroArea.GetFromRight(212.f);
    const IRECT bayArea(fingerprintArea.R + 12.f, heroArea.T, cabArea.L - 12.f, heroArea.B);
    const IRECT scopeArea(b.MW() - 120.f, 16.f, b.MW() + 180.f, 60.f);

    // Areas for model and IR
    const float fileHeight = 32.f;
    const IRECT fileRowArea(rigArea.L, heroArea.B + 8.f, bayArea.R, heroArea.B + 8.f + fileHeight);
    const auto slimIconArea = fileRowArea.GetFromRight(56.f).GetMidVPadded(14.f);
    const auto libraryButtonArea = fileRowArea.GetReducedFromRight(62.f).GetFromRight(76.f).GetMidVPadded(11.f);
    const auto modelArea = fileRowArea.GetReducedFromRight(62.f + 82.f);
    const IRECT irArea(cabArea.L, fileRowArea.T, cabArea.R, fileRowArea.B);
    const auto irSwitchArea = cabArea.GetFromTRHC(46.f, 46.f).GetCentredInside(20.f, 20.f);

    // The pedalboard: input box, gate, (off to the capture and back), EQ, output box
    const IRECT boardArea(rigArea.L, fileRowArea.B + 12.f, rigArea.R, rigArea.B);
    const float pedalT = boardArea.T + 14.f, pedalB = boardArea.B - 14.f;
    const float boxT = pedalT + 22.f, boxB = pedalB - 22.f;
    const IRECT inputBoxArea(boardArea.L + 30.f, boxT, boardArea.L + 120.f, boxB);
    const IRECT gatePedalArea(inputBoxArea.R + 30.f, pedalT, inputBoxArea.R + 166.f, pedalB);
    const IRECT eqPedalArea(gatePedalArea.R + 80.f, pedalT, gatePedalArea.R + 330.f, pedalB);
    const IRECT outputBoxArea(eqPedalArea.R + 30.f, boxT, eqPedalArea.R + 200.f, boxB);

    // Areas for knobs
    const float knobHeight = 100.f;
    auto knobCell = [knobHeight](const IRECT& box, float top, int i, int n) {
      return IRECT(box.L + 5.f, top, box.R - 5.f, top + knobHeight).GetGridCell(0, i, 1, n);
    };
    const auto inputKnobArea = knobCell(inputBoxArea, inputBoxArea.T + 14.f, 0, 1);
    const auto noiseGateArea = knobCell(gatePedalArea, gatePedalArea.T + 28.f, 0, 1);
    const auto bassKnobArea = knobCell(eqPedalArea, eqPedalArea.T + 28.f, 0, 3);
    const auto midKnobArea = knobCell(eqPedalArea, eqPedalArea.T + 28.f, 1, 3);
    const auto trebleKnobArea = knobCell(eqPedalArea, eqPedalArea.T + 28.f, 2, 3);
    const auto outputKnobArea = knobCell(outputBoxArea, outputBoxArea.T + 14.f, 0, 2);
    const auto mixKnobArea = knobCell(outputBoxArea, outputBoxArea.T + 14.f, 1, 2);
    auto footswitchArea = [](const IRECT& pedal) {
      return IRECT(pedal.MW() - 30.f, pedal.B - 62.f, pedal.MW() + 30.f, pedal.B - 4.f);
    };

    // Areas for meters
    const IRECT inputMeterArea(b.L + 22.f, heroArea.T, b.L + 38.f, boardArea.B);
    const IRECT outputMeterArea(b.R - 38.f, heroArea.T, b.R - 22.f, boardArea.B);

    // Misc Areas
    const auto settingsButtonArea = CornerButtonArea(b);

    const IVStyle knobStyle =
      style.WithLabelText(IText(9.f, NAMRig::kCream.WithOpacity(0.85f), "Michroma-Regular", EAlign::Center,
                                EVAlign::Middle))
        .WithValueText(IText(DEFAULT_TEXT_SIZE + 1.f, NAMRig::kCream.WithOpacity(0.7f), nullptr, EAlign::Center,
                             EVAlign::Bottom));

    // Model loader button
    auto loadModelCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        // Sets mNAMPath and mStagedNAM
        const std::string msg = _StageModel(fileName);
        // TODO error messages like the IR loader.
        if (msg.size())
        {
          std::stringstream ss;
          ss << "Failed to load NAM model. Message:\n\n" << msg;
          _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load model!", kMB_OK);
        }
        std::cout << "Loaded: " << fileName.Get() << std::endl;
      }
    };

    // IR loader button
    auto loadIRCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        mIRPath = fileName;
        const dsp::wav::LoadReturnCode retCode = _StageIR(fileName);
        if (retCode != dsp::wav::LoadReturnCode::SUCCESS)
        {
          std::stringstream message;
          message << "Failed to load IR file " << fileName.Get() << ":\n";
          message << dsp::wav::GetMsgForLoadReturnCode(retCode);

          _ShowMessageBox(GetUI(), message.str().c_str(), "Failed to load IR!", kMB_OK);
        }
      }
    };

    // Second IR for the mic to blend toward, picked from the cab
    auto loadEdgeIR = [&](IControl* pCaller) {
      WDL_String fileName, path(mIRPath.Get());
      path.remove_filepart();
      pCaller->GetUI()->PromptForFile(
        fileName, path, EFileAction::Open, "wav", [&](const WDL_String& fileName, const WDL_String& path) {
          if (!fileName.GetLength())
            return;
          const dsp::wav::LoadReturnCode retCode = _StageEdgeIR(fileName);
          if (retCode != dsp::wav::LoadReturnCode::SUCCESS)
          {
            std::stringstream message;
            message << "Failed to load IR file " << fileName.Get() << ":\n";
            message << dsp::wav::GetMsgForLoadReturnCode(retCode);
            _ShowMessageBox(GetUI(), message.str().c_str(), "Failed to load IR!", kMB_OK);
          }
        });
    };
    auto clearEdgeIR = [](IControl* pCaller) { pCaller->GetDelegate()->SendArbitraryMsgFromUI(kMsgTagClearEdgeIR); };

    pGraphics->AttachControl(new NAMRig::PanelControl(b, mTheme, true));
    pGraphics->AttachControl(new NAMRig::ScopeControl(scopeArea, mTheme), kNoTag, NAMRig::kReactiveGroup);
    pGraphics->AttachControl(new NAMRig::FingerprintControl(fingerprintArea, mTheme), kNoTag, NAMRig::kReactiveGroup);
    pGraphics->AttachControl(new NAMRig::CaptureBayControl(bayArea, mTheme), kNoTag, NAMRig::kReactiveGroup);
    pGraphics->AttachControl(
      new NAMRig::CabControl(cabArea, mTheme, kMicPosition, kMicDistance, loadEdgeIR, clearEdgeIR), kNoTag,
      NAMRig::kReactiveGroup);
    pGraphics->AttachControl(
      new NAMRig::BoardControl(boardArea, mTheme, {inputBoxArea, gatePedalArea, eqPedalArea, outputBoxArea}), kNoTag,
      NAMRig::kReactiveGroup);
    pGraphics->AttachControl(new NAMRig::PedalControl(gatePedalArea, mTheme, "NOISE GATE", IColor(255, 40, 58, 82)));
    pGraphics->AttachControl(new NAMRig::PedalControl(eqPedalArea, mTheme, "TONE STACK", IColor(255, 44, 70, 52)));

#ifdef NAM_PICK_DIRECTORY
    const std::string defaultNamFileString = "Select model directory...";
    const std::string defaultIRString = "Select IR directory...";
#else
    const std::string defaultNamFileString = "Select model...";
    const std::string defaultIRString = "Select IR...";
#endif
    // Getting started page listing additional resources
    const char* const getUrl = "https://www.neuralampmodeler.com/users#comp-marb84o5";
    pGraphics->AttachControl(
      new NAMFileBrowserControl(modelArea, kMsgTagClearModel, defaultNamFileString.c_str(), "nam",
                                loadModelCompletionHandler, style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                mTheme, globeSVG, "Get NAM Models", getUrl),
      kCtrlTagModelFileBrowser);
    pGraphics->AttachControl(new NAMRig::PillButtonControl(libraryButtonArea, "LIBRARY", mTheme,
                                                           [&](IControl* pCaller) { _OpenLibrary(pCaller->GetUI()); }));

    auto hideSlimOverlay = [](IControl* pCaller) {
      IGraphics* ui = pCaller->GetUI();
      if (auto* backdrop = ui->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        backdrop->Hide(true);
      if (auto* knob = ui->GetControlWithTag(kCtrlTagSlimKnob))
        knob->Hide(true);
      ui->SetAllControlsDirty();
    };
    auto showSlimOverlay = [](IControl* pCaller) {
      IGraphics* ui = pCaller->GetUI();
      if (auto* backdrop = ui->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        backdrop->Hide(false);
      if (auto* knob = ui->GetControlWithTag(kCtrlTagSlimKnob))
        knob->Hide(false);
      ui->SetAllControlsDirty();
    };

    pGraphics
      ->AttachControl(
        new NAMSquareButtonControl(slimIconArea, DefaultClickActionFunc, slimIconSVG), kCtrlTagSlimmableIcon)
      ->SetAnimationEndActionFunction(showSlimOverlay)
      ->Hide(true);

    pGraphics->AttachControl(new ISVGSwitchControl(irSwitchArea, {irIconOffSVG, irIconOnSVG}, kIRToggle));
    pGraphics->AttachControl(
      new NAMFileBrowserControl(irArea, kMsgTagClearIR, defaultIRString.c_str(), "wav", loadIRCompletionHandler, style,
                                fileSVG, crossSVG, leftArrowSVG, rightArrowSVG, mTheme, globeSVG,
                                "Get IRs", getUrl),
      kCtrlTagIRFileBrowser);
    pGraphics->AttachControl(new NAMRig::FootswitchControl(footswitchArea(gatePedalArea), kNoiseGateActive, mTheme));
    pGraphics->AttachControl(new NAMRig::FootswitchControl(footswitchArea(eqPedalArea), kEQActive, mTheme));

    // The knobs
    pGraphics->AttachControl(new NAMKnobControl(inputKnobArea, kInputLevel, "INPUT", knobStyle, mTheme));
    pGraphics->AttachControl(new NAMKnobControl(noiseGateArea, kNoiseGateThreshold, "THRESHOLD", knobStyle, mTheme));
    pGraphics->AttachControl(
      new NAMKnobControl(bassKnobArea, kToneBass, "BASS", knobStyle, mTheme), -1, "EQ_KNOBS");
    pGraphics->AttachControl(
      new NAMKnobControl(midKnobArea, kToneMid, "MIDDLE", knobStyle, mTheme), -1, "EQ_KNOBS");
    pGraphics->AttachControl(
      new NAMKnobControl(trebleKnobArea, kToneTreble, "TREBLE", knobStyle, mTheme), -1, "EQ_KNOBS");
    pGraphics->AttachControl(new NAMKnobControl(outputKnobArea, kOutputLevel, "OUTPUT", knobStyle, mTheme));
    pGraphics->AttachControl(new NAMKnobControl(mixKnobArea, kMix, "MIX", knobStyle, mTheme));

    // The meters
    pGraphics->AttachControl(new NAMMeterControl(inputMeterArea, style, mTheme), kCtrlTagInputMeter);
    pGraphics->AttachControl(new NAMMeterControl(outputMeterArea, style, mTheme), kCtrlTagOutputMeter);

    // Settings/help/about box
    pGraphics->AttachControl(new NAMCircleButtonControl(
      settingsButtonArea,
      [pGraphics](IControl* pCaller) {
        pGraphics->GetControlWithTag(kCtrlTagSettingsBox)->As<NAMSettingsPageControl>()->HideAnimated(false);
      },
      gearSVG));

    pGraphics
      ->AttachControl(new NAMSettingsPageControl(b, mTheme, inputLevelBackgroundBitmap, switchHandleBitmap,
                                                 crossSVG, style, radioButtonStyle),
                      kCtrlTagSettingsBox)
      ->Hide(true);

    auto pickFromLibrary = [&](const std::string& path) {
      const std::string msg = _StageModel(WDL_String(path.c_str()));
      if (msg.size())
      {
        std::stringstream ss;
        ss << "Failed to load NAM model. Message:\n\n" << msg;
        _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load model!", kMB_OK);
      }
    };
    auto chooseLibraryFolder = [&](IControl* pCaller) {
      WDL_String dir(mLibraryFolder.c_str());
      pCaller->GetUI()->PromptForDirectory(dir, [&](const WDL_String& fileName, const WDL_String& path) {
        if (!path.GetLength())
          return;
        mLibraryFolder = path.Get();
        mLibrary.Scan(mLibraryFolder);
        if (auto* pLibrary = GetUI()->GetControlWithTag(kCtrlTagLibrary))
          pLibrary->As<NAMRig::LibraryControl>()->Show();
      });
    };
    pGraphics
      ->AttachControl(new NAMRig::LibraryControl(b, mTheme, mLibrary, pickFromLibrary, chooseLibraryFolder),
                      kCtrlTagLibrary)
      ->Hide(true);

    const auto slimKnobArea = b.GetCentredInside(100.f, NAM_KNOB_HEIGHT + 24.f);
    pGraphics->AttachControl(new NAMSlimOverlayBackdropControl(b, hideSlimOverlay), kCtrlTagSlimOverlayBackdrop)
      ->Hide(true);
    pGraphics
      ->AttachControl(new NAMKnobControl(slimKnobArea, kSlim, "SLIM", knobStyle, mTheme), kCtrlTagSlimKnob)
      ->Hide(true);

    pGraphics->ForAllControlsFunc([](IControl* pControl) {
      pControl->SetMouseEventsWhenDisabled(true);
      pControl->SetMouseOverWhenDisabled(true);
    });

    // pGraphics->GetControlWithTag(kCtrlTagOutNorm)->SetMouseEventsWhenDisabled(false);
    // pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetMouseEventsWhenDisabled(false);
  };
}

NeuralAmpModeler::~NeuralAmpModeler()
{
  _DeallocateIOPointers();
}

void NeuralAmpModeler::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames)
{
  const size_t numChannelsExternalIn = (size_t)NInChansConnected();
  const size_t numChannelsExternalOut = (size_t)NOutChansConnected();
  const size_t numChannelsInternal = kNumChannelsInternal;
  const size_t numFrames = (size_t)nFrames;
  const double sampleRate = GetSampleRate();

  // Disable floating point denormals
  std::fenv_t fe_state;
  std::feholdexcept(&fe_state);
  disable_denormals();

  _PrepareBuffers(numChannelsInternal, numFrames);
  _CaptureDryInput(inputs, numFrames, numChannelsExternalIn, numChannelsExternalOut);
  // Input is collapsed to mono in preparation for the NAM.
  _ProcessInput(inputs, numFrames, numChannelsExternalIn, numChannelsInternal);
  _ApplyDSPStaging();
  const bool noiseGateActive = GetParam(kNoiseGateActive)->Value();
  const bool toneStackActive = GetParam(kEQActive)->Value();

  // Noise gate trigger
  sample** triggerOutput = mInputPointers;
  if (noiseGateActive)
  {
    const double time = 0.01;
    const double threshold = GetParam(kNoiseGateThreshold)->Value(); // GetParam...
    const double ratio = 0.1; // Quadratic...
    const double openTime = 0.005;
    const double holdTime = 0.01;
    const double closeTime = 0.05;
    const dsp::noise_gate::TriggerParams triggerParams(time, threshold, ratio, openTime, holdTime, closeTime);
    mNoiseGateTrigger.SetParams(triggerParams);
    mNoiseGateTrigger.SetSampleRate(sampleRate);
    triggerOutput = mNoiseGateTrigger.Process(mInputPointers, numChannelsInternal, numFrames);
  }

  if (mModel != nullptr)
  {
    mModel->process(triggerOutput, mOutputPointers, nFrames);
  }
  else
  {
    _FallbackDSP(triggerOutput, mOutputPointers, numChannelsInternal, numFrames);
  }
  // Apply the noise gate after the NAM
  sample** gateGainOutput =
    noiseGateActive ? mNoiseGateGain.Process(mOutputPointers, numChannelsInternal, numFrames) : mOutputPointers;

  sample** toneStackOutPointers = (toneStackActive && mToneStack != nullptr)
                                    ? mToneStack->Process(gateGainOutput, numChannelsInternal, nFrames)
                                    : gateGainOutput;

  sample** irPointers = toneStackOutPointers;
  if (GetParam(kIRToggle)->Value())
    irPointers = _ProcessCab(toneStackOutPointers, numFrames, sampleRate);

  // And the HPF for DC offset (Issue 271)
  const double highPassCutoffFreq = kDCBlockerFrequency;
  // const double lowPassCutoffFreq = 20000.0;
  const recursive_linear_filter::HighPassParams highPassParams(sampleRate, highPassCutoffFreq);
  // const recursive_linear_filter::LowPassParams lowPassParams(sampleRate, lowPassCutoffFreq);
  mHighPass.SetParams(highPassParams);
  // mLowPass.SetParams(lowPassParams);
  sample** hpfPointers = mHighPass.Process(irPointers, numChannelsInternal, numFrames);
  // sample** lpfPointers = mLowPass.Process(hpfPointers, numChannelsInternal, numFrames);

  // restore previous floating point state
  std::feupdateenv(&fe_state);

  // Let's get outta here
  // This is where we exit mono for whatever the output requires.
  _ProcessOutput(hpfPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
  // _ProcessOutput(lpfPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
  // * Output of input leveling (inputs -> mInputPointers),
  // * Output of output leveling (mOutputPointers -> outputs)
  _UpdateMeters(mInputPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
}

void NeuralAmpModeler::OnReset()
{
  const auto sampleRate = GetSampleRate();
  const int maxBlockSize = GetBlockSize();

  mMixSmoother.SetSmoothTime(20.0, sampleRate);
  mMixSmoother.SetValue(GetParam(kMix)->Value() / 100.0);

  // Tail is because the HPF DC blocker has a decay.
  // 10 cycles should be enough to pass the VST3 tests checking tail behavior.
  // I'm ignoring the model & IR, but it's not the end of the world.
  const int tailCycles = 10;
  SetTailSize(tailCycles * (int)(sampleRate / kDCBlockerFrequency));
  mInputSender.Reset(sampleRate);
  mOutputSender.Reset(sampleRate);
  // If there is a model or IR loaded, they need to be checked for resampling.
  _ResetModelAndIR(sampleRate, GetBlockSize());
  mToneStack->Reset(sampleRate, maxBlockSize);
  _UpdateLatency();
}

void NeuralAmpModeler::OnIdle()
{
  mInputSender.TransmitData(*this);
  mOutputSender.TransmitData(*this);
  _UpdateRigVisuals();
  if (auto* pGraphics = GetUI())
    if (auto* pLibrary = pGraphics->GetControlWithTag(kCtrlTagLibrary))
      pLibrary->As<NAMRig::LibraryControl>()->Poll();

  if (mNewModelLoadedInDSP)
  {
    if (auto* pGraphics = GetUI())
    {
      _UpdateControlsFromModel();
      mNewModelLoadedInDSP = false;
    }
  }
  if (mModelCleared)
  {
    if (auto* pGraphics = GetUI())
    {
      // FIXME -- need to disable only the "normalized" model
      // pGraphics->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
      static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->ClearModelInfo();
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimKnob))
        p->Hide(true);
      pGraphics->SetAllControlsDirty();
      mModelCleared = false;
    }
  }
}

bool NeuralAmpModeler::SerializeState(IByteChunk& chunk) const
{
  // If this isn't here when unserializing, then we know we're dealing with something before v0.8.0.
  WDL_String header("###NeuralAmpModeler###"); // Don't change this!
  chunk.PutStr(header.Get());
  // Plugin version, so we can load legacy serialized states in the future!
  WDL_String version(PLUG_VERSION_STR);
  chunk.PutStr(version.Get());
  // Model directory (don't serialize the model itself; we'll just load it again
  // when we unserialize)
  chunk.PutStr(mNAMPath.Get());
  chunk.PutStr(mIRPath.Get());
  if (!SerializeParams(chunk))
    return false;
  // The rig's additions go after the params, behind a marker. Builds that don't know about them stop reading before
  // they get here, and states from those builds simply end without the marker. See _UnserializeRigExtras().
  chunk.PutStr(kRigStateMarker);
  chunk.PutStr(mEdgeIRPath.Get());
  return true;
}

int NeuralAmpModeler::UnserializeState(const IByteChunk& chunk, int startPos)
{
  // Look for the expected header. If it's there, then we'll know what to do.
  WDL_String header;
  int pos = startPos;
  pos = chunk.GetStr(header, pos);

  const char* kExpectedHeader = "###NeuralAmpModeler###";
  if (strcmp(header.Get(), kExpectedHeader) == 0)
  {
    return _UnserializeStateWithKnownVersion(chunk, pos);
  }
  else
  {
    return _UnserializeStateWithUnknownVersion(chunk, startPos);
  }
}

void NeuralAmpModeler::OnUIOpen()
{
  Plugin::OnUIOpen();

  if (mNAMPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
    // If it's not loaded yet, then mark as failed.
    // If it's yet to be loaded, then the completion handler will set us straight once it runs.
    if (mModel == nullptr && mStagedModel == nullptr)
      SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);
  }

  if (mIRPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath.GetLength(), mIRPath.Get());
    if (mIR == nullptr && mStagedIR == nullptr)
      SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadFailed);
  }

  if (mModel != nullptr)
  {
    _UpdateControlsFromModel();
  }
}

void NeuralAmpModeler::OnParamChange(int paramIdx)
{
  switch (paramIdx)
  {
    // Changes to the input gain
    case kCalibrateInput:
    case kInputCalibrationLevel:
    case kInputLevel: _SetInputGain(); break;
    // Changes to the output gain
    case kOutputLevel:
    case kOutputMode: _SetOutputGain(); break;
    // Tone stack:
    case kToneBass: mToneStack->SetParam("bass", GetParam(paramIdx)->Value()); break;
    case kToneMid: mToneStack->SetParam("middle", GetParam(paramIdx)->Value()); break;
    case kToneTreble: mToneStack->SetParam("treble", GetParam(paramIdx)->Value()); break;
    case kSlim: _ApplySlimParamToLoadedNAMs(); break;
    default: break;
  }
}

void NeuralAmpModeler::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (auto pGraphics = GetUI())
  {
    bool active = GetParam(paramIdx)->Bool();

    switch (paramIdx)
    {
      case kNoiseGateActive: pGraphics->GetControlWithParamIdx(kNoiseGateThreshold)->SetDisabled(!active); break;
      case kEQActive:
        pGraphics->ForControlInGroup("EQ_KNOBS", [active](IControl* pControl) { pControl->SetDisabled(!active); });
        break;
      case kIRToggle: pGraphics->GetControlWithTag(kCtrlTagIRFileBrowser)->SetDisabled(!active); break;
      default: break;
    }
  }
}

bool NeuralAmpModeler::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  switch (msgTag)
  {
    case kMsgTagClearModel:
    {
      mShouldRemoveModel = true;
      std::lock_guard<std::mutex> lock(mGearInfoMutex);
      mGearInfoChanged = false;
      mTheme.ClearGear();
      return true;
    }
    case kMsgTagClearIR:
      mShouldRemoveIR = true;
      mIRPresent = false;
      return true;
    case kMsgTagClearEdgeIR:
    {
      mShouldRemoveEdgeIR = true;
      std::lock_guard<std::mutex> lock(mGearInfoMutex);
      mEdgeIRChanged = false;
      mTheme.edgeIRName.clear();
      if (GetUI())
        GetUI()->SetAllControlsDirty();
      return true;
    }
    case kMsgTagHighlightColor:
    {
      mHighLightColor.Set((const char*)pData);

      if (GetUI())
      {
        GetUI()->ForStandardControlsFunc([&](IControl* pControl) {
          if (auto* pVectorBase = pControl->As<IVectorBase>())
          {
            IColor color = IColor::FromColorCodeStr(mHighLightColor.Get());

            pVectorBase->SetColor(kX1, color);
            pVectorBase->SetColor(kPR, color.WithOpacity(0.3f));
            pVectorBase->SetColor(kFR, color.WithOpacity(0.4f));
            pVectorBase->SetColor(kX3, color.WithContrast(0.1f));
          }
          pControl->GetUI()->SetAllControlsDirty();
        });
      }

      return true;
    }
    default: return false;
  }
}

// Private methods ============================================================

void NeuralAmpModeler::_AllocateIOPointers(const size_t nChans)
{
  if (mInputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mInputPointers without freeing");
  mInputPointers = new sample*[nChans];
  if (mInputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mOutputPointers without freeing");
  mOutputPointers = new sample*[nChans];
  if (mOutputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_ApplyDSPStaging()
{
  // Remove marked modules
  if (mShouldRemoveModel)
  {
    mModel = nullptr;
    mNAMPath.Set("");
    mShouldRemoveModel = false;
    mModelCleared = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mShouldRemoveIR)
  {
    mIR = nullptr;
    mIRPath.Set("");
    mShouldRemoveIR = false;
  }
  // Move things from staged to live
  if (mStagedModel != nullptr)
  {
    mModel = std::move(mStagedModel);
    mStagedModel = nullptr;
    mNewModelLoadedInDSP = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mStagedIR != nullptr)
  {
    mIR = std::move(mStagedIR);
    mStagedIR = nullptr;
  }
  if (mShouldRemoveEdgeIR)
  {
    mEdgeIR = nullptr;
    mEdgeIRPath.Set("");
    mShouldRemoveEdgeIR = false;
  }
  if (mStagedEdgeIR != nullptr)
  {
    mEdgeIR = std::move(mStagedEdgeIR);
    mStagedEdgeIR = nullptr;
  }
}

void NeuralAmpModeler::_DeallocateIOPointers()
{
  if (mInputPointers != nullptr)
  {
    delete[] mInputPointers;
    mInputPointers = nullptr;
  }
  if (mInputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
  {
    delete[] mOutputPointers;
    mOutputPointers = nullptr;
  }
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels,
                                    const size_t numFrames)
{
  for (auto c = 0; c < numChannels; c++)
    for (auto s = 0; s < numFrames; s++)
      mOutputArray[c][s] = mInputArray[c][s];
}

void NeuralAmpModeler::_ResetModelAndIR(const double sampleRate, const int maxBlockSize)
{
  // Model
  if (mStagedModel != nullptr)
  {
    mStagedModel->Reset(sampleRate, maxBlockSize);
  }
  else if (mModel != nullptr)
  {
    mModel->Reset(sampleRate, maxBlockSize);
  }

  // IRs
  auto resampleIR = [sampleRate](std::unique_ptr<dsp::ImpulseResponse>& staged,
                                 const std::unique_ptr<dsp::ImpulseResponse>& live) {
    dsp::ImpulseResponse* current = staged != nullptr ? staged.get() : live.get();
    if (current != nullptr && current->GetSampleRate() != sampleRate)
    {
      const auto irData = current->GetData();
      staged = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  };
  resampleIR(mStagedIR, mIR);
  resampleIR(mStagedEdgeIR, mEdgeIR);
}

void NeuralAmpModeler::_SetInputGain()
{
  iplug::sample inputGainDB = GetParam(kInputLevel)->Value();
  // Input calibration
  if ((mModel != nullptr) && (mModel->HasInputLevel()) && GetParam(kCalibrateInput)->Bool())
  {
    inputGainDB += GetParam(kInputCalibrationLevel)->Value() - mModel->GetInputLevel();
  }
  mInputGain = DBToAmp(inputGainDB);
}

void NeuralAmpModeler::_SetOutputGain()
{
  double gainDB = GetParam(kOutputLevel)->Value();
  if (mModel != nullptr)
  {
    const int outputMode = GetParam(kOutputMode)->Int();
    switch (outputMode)
    {
      case 1: // Normalized
        if (mModel->HasLoudness())
        {
          const double loudness = mModel->GetLoudness();
          const double targetLoudness = -18.0;
          gainDB += (targetLoudness - loudness);
        }
        break;
      case 2: // Calibrated
        if (mModel->HasOutputLevel())
        {
          const double inputLevel = GetParam(kInputCalibrationLevel)->Value();
          const double outputLevel = mModel->GetOutputLevel();
          gainDB += (outputLevel - inputLevel);
        }
        break;
      case 0: // Raw
      default: break;
    }
  }
  mOutputGain = DBToAmp(gainDB);
}

void NeuralAmpModeler::_ApplySlimParamToLoadedNAMs()
{
  const double v = GetParam(kSlim)->Value();
  auto apply = [v](ResamplingNAM* p) {
    if (p == nullptr)
      return;
    if (nam::SlimmableModel* s = p->GetSlimmableModel())
      s->SetSlimmableSize(v);
  };
  apply(mModel.get());
  apply(mStagedModel.get());
}

std::string NeuralAmpModeler::_StageModel(const WDL_String& modelPath)
{
  WDL_String previousNAMPath = mNAMPath;
  try
  {
    nam::dspData modelData;
    std::unique_ptr<nam::DSP> model = nam::get_dsp(std::filesystem::u8path(modelPath.Get()), modelData);

    // Check that the model has 1 input and 1 output channel
    if (model->NumInputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 input channel, but has " + std::to_string(model->NumInputChannels()));
    }
    if (model->NumOutputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 output channel, but has "
                               + std::to_string(model->NumOutputChannels()));
    }

    // Measure before wrapping; the wrapper resets the model when it's built.
    const NAMFingerprint fingerprint = MeasureFingerprint(*model, GetNAMSampleRate(model));

    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    if (nam::SlimmableModel* slimmable = temp->GetSlimmableModel())
    {
      slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
    }
    mStagedModel = std::move(temp);
    mNAMPath = modelPath;
    {
      std::lock_guard<std::mutex> lock(mGearInfoMutex);
      mPendingGearInfo = ReadGearInfo(modelData.metadata, modelPath);
      mPendingGearInfo.fingerprint = fingerprint;
      mGearInfoChanged = true;
    }
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);

    if (mStagedModel != nullptr)
    {
      mStagedModel = nullptr;
    }
    mNAMPath = previousNAMPath;
    std::cerr << "Failed to read DSP module" << std::endl;
    std::cerr << e.what() << std::endl;
    return e.what();
  }
  return "";
}

dsp::wav::LoadReturnCode NeuralAmpModeler::_StageIR(const WDL_String& irPath)
{
  // FIXME it'd be better for the path to be "staged" as well. Just in case the
  // path and the model got caught on opposite sides of the fence...
  WDL_String previousIRPath = mIRPath;
  const double sampleRate = GetSampleRate();
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    mStagedIR = std::make_unique<dsp::ImpulseResponse>(irPath.Get(), sampleRate);
    wavState = mStagedIR->GetWavState();
  }
  catch (std::runtime_error& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unhandled exception while attempting to load IR:" << std::endl;
    std::cerr << e.what() << std::endl;
  }

  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    mIRPath = irPath;
    mIRPresent = true;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath.GetLength(), mIRPath.Get());
  }
  else
  {
    if (mStagedIR != nullptr)
    {
      mStagedIR = nullptr;
    }
    mIRPath = previousIRPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadFailed);
  }

  return wavState;
}

dsp::wav::LoadReturnCode NeuralAmpModeler::_StageEdgeIR(const WDL_String& irPath)
{
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    auto staged = std::make_unique<dsp::ImpulseResponse>(irPath.Get(), GetSampleRate());
    wavState = staged->GetWavState();
    if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
      mStagedEdgeIR = std::move(staged);
  }
  catch (std::runtime_error& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unhandled exception while attempting to load IR:" << std::endl;
    std::cerr << e.what() << std::endl;
  }

  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    mEdgeIRPath = irPath;
    WDL_String name(irPath.get_filepart());
    name.remove_fileext();
    std::lock_guard<std::mutex> lock(mGearInfoMutex);
    mPendingEdgeIRName = name.Get();
    mEdgeIRChanged = true;
  }
  return wavState;
}

sample** NeuralAmpModeler::_ProcessCab(sample** inputs, const size_t numFrames, const double sampleRate)
{
  const double position = GetParam(kMicPosition)->Value() / 100.0;
  const double distance = GetParam(kMicDistance)->Value() / 100.0;

  sample** mainPointers = mIR != nullptr ? mIR->Process(inputs, kNumChannelsInternal, numFrames) : nullptr;
  sample** edgePointers = mEdgeIR != nullptr ? mEdgeIR->Process(inputs, kNumChannelsInternal, numFrames) : nullptr;
  const bool blending = mainPointers != nullptr && edgePointers != nullptr;

  sample** outputs = inputs;
  if (blending)
  {
    if (mCabBlendArray.size() < numFrames)
      mCabBlendArray.resize(numFrames);
    // Ramp across the block so dragging the mic doesn't zipper.
    const double startBlend = mEdgeBlend;
    for (size_t s = 0; s < numFrames; s++)
    {
      const double blend = startBlend + (position - startBlend) * (s + 1) / numFrames;
      mCabBlendArray[s] = mainPointers[0][s] + blend * (edgePointers[0][s] - mainPointers[0][s]);
    }
    mCabBlendPointer = mCabBlendArray.data();
    outputs = &mCabBlendPointer;
  }
  else if (mainPointers != nullptr)
    outputs = mainPointers;
  else if (edgePointers != nullptr)
    outputs = edgePointers;
  mEdgeBlend = position;

  // With two IRs, the position is the blend; with one (or none), moving off axis voices it darker instead.
  const double offAxis = blending ? 0.0 : position;
  if (offAxis != mMicVoicedPosition || distance != mMicVoicedDistance || sampleRate != mMicVoicedSampleRate)
  {
    using recursive_linear_filter::BiquadParams;
    mMicBody.SetParams(BiquadParams(sampleRate, 300.0, 0.8, 2.5 * offAxis));
    mMicOffAxis.SetParams(BiquadParams(sampleRate, 2500.0, 0.707, -10.0 * offAxis));
    mMicProximity.SetParams(BiquadParams(sampleRate, 160.0, 0.707, -7.0 * distance));
    mMicAir.SetParams(BiquadParams(sampleRate, 7000.0, 0.707, -5.0 * distance));
    mMicVoicedPosition = offAxis;
    mMicVoicedDistance = distance;
    mMicVoicedSampleRate = sampleRate;
  }
  outputs = mMicBody.Process(outputs, kNumChannelsInternal, numFrames);
  outputs = mMicOffAxis.Process(outputs, kNumChannelsInternal, numFrames);
  outputs = mMicProximity.Process(outputs, kNumChannelsInternal, numFrames);
  outputs = mMicAir.Process(outputs, kNumChannelsInternal, numFrames);
  return outputs;
}

void NeuralAmpModeler::_OpenLibrary(IGraphics* pGraphics)
{
  auto* pLibrary = pGraphics->GetControlWithTag(kCtrlTagLibrary)->As<NAMRig::LibraryControl>();
  std::string folder = mLibraryFolder;
  if (mNAMPath.GetLength())
  {
    WDL_String dir(mNAMPath);
    dir.remove_filepart(true);
    folder = dir.Get();
  }

  if (folder.empty())
  {
    // Nothing to go on yet: ask.
    WDL_String dir;
    pGraphics->PromptForDirectory(dir, [&, pLibrary](const WDL_String& fileName, const WDL_String& path) {
      if (!path.GetLength())
        return;
      mLibraryFolder = path.Get();
      mLibrary.Scan(mLibraryFolder);
      pLibrary->Show();
    });
    return;
  }

  std::vector<NAMGearInfo> unused;
  std::string scanned;
  mLibrary.Snapshot(unused, scanned);
  if (scanned != folder)
    mLibrary.Scan(folder);
  pLibrary->Show();
}

size_t NeuralAmpModeler::_GetBufferNumChannels() const
{
  // Assumes input=output (no mono->stereo effects)
  return mInputArray.size();
}

size_t NeuralAmpModeler::_GetBufferNumFrames() const
{
  if (_GetBufferNumChannels() == 0)
    return 0;
  return mInputArray[0].size();
}

void NeuralAmpModeler::_InitToneStack()
{
  // If you want to customize the tone stack, then put it here!
  mToneStack = std::make_unique<dsp::tone_stack::BasicNamToneStack>();
}
void NeuralAmpModeler::_PrepareBuffers(const size_t numChannels, const size_t numFrames)
{
  const bool updateChannels = numChannels != _GetBufferNumChannels();
  const bool updateFrames = updateChannels || (_GetBufferNumFrames() != numFrames);
  //  if (!updateChannels && !updateFrames)  // Could we do this?
  //    return;

  if (updateChannels)
  {
    _PrepareIOPointers(numChannels);
    mInputArray.resize(numChannels);
    mOutputArray.resize(numChannels);
  }
  if (updateFrames)
  {
    for (auto c = 0; c < mInputArray.size(); c++)
    {
      mInputArray[c].resize(numFrames);
      std::fill(mInputArray[c].begin(), mInputArray[c].end(), 0.0);
    }
    for (auto c = 0; c < mOutputArray.size(); c++)
    {
      mOutputArray[c].resize(numFrames);
      std::fill(mOutputArray[c].begin(), mOutputArray[c].end(), 0.0);
    }
  }
  // Would these ever get changed by something?
  for (auto c = 0; c < mInputArray.size(); c++)
    mInputPointers[c] = mInputArray[c].data();
  for (auto c = 0; c < mOutputArray.size(); c++)
    mOutputPointers[c] = mOutputArray[c].data();
}

void NeuralAmpModeler::_PrepareIOPointers(const size_t numChannels)
{
  _DeallocateIOPointers();
  _AllocateIOPointers(numChannels);
}

void NeuralAmpModeler::_ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn,
                                     const size_t nChansOut)
{
  // We'll assume that the main processing is mono for now. We'll handle dual amps later.
  if (nChansOut != 1)
  {
    std::stringstream ss;
    ss << "Expected mono output, but " << nChansOut << " output channels are requested!";
    throw std::runtime_error(ss.str());
  }

  // On the standalone, we can probably assume that the user has plugged into only one input and they expect it to be
  // carried straight through. Don't apply any division over nChansIn because we're just "catching anything out there."
  // However, in a DAW, it's probably something providing stereo, and we want to take the average in order to avoid
  // doubling the loudness. (This would change w/ double mono processing)
  double gain = mInputGain;
#ifndef APP_API
  gain /= (float)nChansIn;
#endif
  // Assume _PrepareBuffers() was already called
  for (size_t c = 0; c < nChansIn; c++)
    for (size_t s = 0; s < nFrames; s++)
      if (c == 0)
        mInputArray[0][s] = gain * inputs[c][s];
      else
        mInputArray[0][s] += gain * inputs[c][s];
}

void NeuralAmpModeler::_CaptureDryInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn,
                                        const size_t nChansOut)
{
  if (mDryArray.size() != nChansOut)
    mDryArray.resize(nChansOut);

  for (size_t c = 0; c < nChansOut; c++)
  {
    if (mDryArray[c].size() != nFrames)
      mDryArray[c].resize(nFrames);

    // Preserve stereo dry channels. Broadcast mono dry input to multiple outputs.
    const size_t inputChannel = nChansIn > 1 ? std::min(c, nChansIn - 1) : 0;
    for (size_t s = 0; s < nFrames; s++)
      mDryArray[c][s] = nChansIn > 0 ? inputs[inputChannel][s] : 0.0;
  }
}

void NeuralAmpModeler::_ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames,
                                      const size_t nChansIn, const size_t nChansOut)
{
  const double gain = mOutputGain;
  // Assume _PrepareBuffers() was already called
  if (nChansIn != 1)
    throw std::runtime_error("Plugin is supposed to process in mono.");
  const double targetWet = GetParam(kMix)->Value() / 100.0;
  // Broadcast the internal mono wet stream to all output channels. The smoother
  // advances once per frame so every channel receives the same mix coefficient.
  const size_t cin = 0;
  for (auto s = 0; s < nFrames; s++)
  {
    const double wet = mMixSmoother.Process(targetWet);
    const double dry = 1.0 - wet;
    for (auto cout = 0; cout < nChansOut; cout++)
    {
      const double mixed = dry * mDryArray[cout][s] + wet * inputs[cin][s];
#ifdef APP_API // Ensure valid output to interface
      outputs[cout][s] = std::clamp(gain * mixed, -1.0, 1.0);
#else // In a DAW, other things may come next and should be able to handle large
      // values.
      outputs[cout][s] = gain * mixed;
#endif
    }
  }
}

void NeuralAmpModeler::_UpdateControlsFromModel()
{
  if (mModel == nullptr)
  {
    return;
  }
  if (auto* pGraphics = GetUI())
  {
    ModelInfo modelInfo;
    modelInfo.sampleRate.known = true;
    modelInfo.sampleRate.value = mModel->GetEncapsulatedSampleRate();
    modelInfo.inputCalibrationLevel.known = mModel->HasInputLevel();
    modelInfo.inputCalibrationLevel.value = mModel->HasInputLevel() ? mModel->GetInputLevel() : 0.0;
    modelInfo.outputCalibrationLevel.known = mModel->HasOutputLevel();
    modelInfo.outputCalibrationLevel.value = mModel->HasOutputLevel() ? mModel->GetOutputLevel() : 0.0;

    static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->SetModelInfo(modelInfo);

    const bool disableInputCalibrationControls = !mModel->HasInputLevel();
    pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetDisabled(disableInputCalibrationControls);
    pGraphics->GetControlWithTag(kCtrlTagInputCalibrationLevel)->SetDisabled(disableInputCalibrationControls);
    {
      auto* c = static_cast<OutputModeControl*>(pGraphics->GetControlWithTag(kCtrlTagOutputMode));
      c->SetNormalizedDisable(!mModel->HasLoudness());
      c->SetCalibratedDisable(!mModel->HasOutputLevel());
    }

    if (auto* pSlimIcon = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
    {
      const bool show = mModel->GetSlimmableModel() != nullptr;
      pSlimIcon->Hide(!show);
    }
  }
}

void NeuralAmpModeler::_UpdateLatency()
{
  int latency = 0;
  if (mModel)
  {
    latency += mModel->GetLatency();
  }
  // Other things that add latency here...

  // Feels weird to have to do this.
  if (GetLatency() != latency)
  {
    SetLatency(latency);
  }
}

void NeuralAmpModeler::_UpdateMeters(sample** inputPointer, sample** outputPointer, const size_t nFrames,
                                     const size_t nChansIn, const size_t nChansOut)
{
  // Right now, we didn't specify MAXNC when we initialized these, so it's 1.
  const int nChansHack = 1;
  mInputSender.ProcessBlock(inputPointer, (int)nFrames, kCtrlTagInputMeter, nChansHack);
  mOutputSender.ProcessBlock(outputPointer, (int)nFrames, kCtrlTagOutputMeter, nChansHack);

  // Loudest sample since the UI last looked, in and out, for the reactive visuals.
  auto recordPeak = [nFrames](const sample* samples, std::atomic<float>& peak) {
    float blockPeak = 0.f;
    for (size_t s = 0; s < nFrames; s++)
      blockPeak = std::max(blockPeak, static_cast<float>(std::fabs(samples[s])));
    if (blockPeak > peak.load(std::memory_order_relaxed))
      peak.store(blockPeak, std::memory_order_relaxed);
  };
  if (nChansIn > 0)
    recordPeak(inputPointer[0], mModelInputPeak);
  if (nChansOut > 0)
  {
    recordPeak(outputPointer[0], mOutputPeak);

    // And the output itself, for the scope
    const size_t write = mScopeWrite.load(std::memory_order_relaxed);
    for (size_t s = 0; s < nFrames; s++)
      mScopeRing[(write + s) % kScopeRingSize].store(static_cast<float>(outputPointer[0][s]), std::memory_order_relaxed);
    mScopeWrite.store(write + nFrames, std::memory_order_release);
  }
}

void NeuralAmpModeler::_UpdateRigVisuals()
{
  bool themeChanged = false;
  if (mGearInfoChanged)
  {
    std::lock_guard<std::mutex> lock(mGearInfoMutex);
    mTheme.SetGear(mPendingGearInfo);
    mGearInfoChanged = false;
    themeChanged = true;
  }
  if (mEdgeIRChanged)
  {
    std::lock_guard<std::mutex> lock(mGearInfoMutex);
    mTheme.edgeIRName = mPendingEdgeIRName;
    mEdgeIRChanged = false;
    themeChanged = true;
  }
  const bool irLoaded = mIRPresent;
  const bool irActive = GetParam(kIRToggle)->Bool();
  if (irLoaded != mTheme.irLoaded || irActive != mTheme.irActive)
  {
    mTheme.irLoaded = irLoaded;
    mTheme.irActive = irActive;
    themeChanged = true;
  }

  // Fast attack, slow release, over a 60 dB window.
  const float peak = mOutputPeak.exchange(0.f);
  const float target = std::clamp((20.f * std::log10(std::max(peak, 1e-6f)) + 60.f) / 60.f, 0.f, 1.f);
  const float previousLevel = mTheme.level;
  mTheme.level = target > mTheme.level ? target : mTheme.level * 0.9f;
  if (mTheme.level < 0.002f)
    mTheme.level = 0.f;
  if (mTheme.level > 0.f)
    mTheme.phase = std::fmod(mTheme.phase + 0.004f + 0.025f * mTheme.level, 1.f);

  // Input: jump up, fall back at 90 dB/s
  const float inputDb = 20.f * std::log10(std::max(mModelInputPeak.exchange(0.f), 1e-6f));
  mTheme.inputDb = std::max(inputDb, mTheme.inputDb - 1.5f);

  if (mTheme.level > 0.f)
    _UpdateScope();

  if (auto* pGraphics = GetUI())
  {
    if (mTheme.EaseAccent(0.15f) || themeChanged)
      pGraphics->SetAllControlsDirty();
    else if (mTheme.level > 0.f || previousLevel > 0.f)
      pGraphics->ForControlInGroup(NAMRig::kReactiveGroup, [](IControl* pControl) { pControl->SetDirty(false); });
  }
}

void NeuralAmpModeler::_UpdateScope()
{
  const size_t write = mScopeWrite.load(std::memory_order_acquire);
  const size_t window = std::clamp<size_t>(static_cast<size_t>(GetSampleRate() * 0.03), 64, kScopeRingSize / 4);
  // Look back up to another window's worth for a rising zero crossing to start on, so the trace holds still.
  const size_t search = window;
  if (write < window + search)
    return;
  mScopeScratch.resize(window + search);
  const size_t begin = write - window - search;
  for (size_t i = 0; i < mScopeScratch.size(); i++)
    mScopeScratch[i] = mScopeRing[(begin + i) % kScopeRingSize].load(std::memory_order_relaxed);

  size_t trigger = search;
  for (size_t i = search; i > 0; i--)
    if (mScopeScratch[i - 1] < 0.f && mScopeScratch[i] >= 0.f)
    {
      trigger = i;
      break;
    }

  float peak = 0.f;
  for (size_t i = trigger; i < trigger + window; i++)
    peak = std::max(peak, std::fabs(mScopeScratch[i]));
  // Quiet signals stay small rather than blowing noise up to full height.
  const float scale = 0.9f / std::max(peak, 0.03f);

  const size_t nPoints = 300;
  mTheme.scope.resize(nPoints);
  for (size_t k = 0; k < nPoints; k++)
    mTheme.scope[k] = scale * mScopeScratch[trigger + k * window / nPoints];
}

// HACK
#include "Unserialization.cpp"
