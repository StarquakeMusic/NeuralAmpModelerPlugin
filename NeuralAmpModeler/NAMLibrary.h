#pragma once

// Reads gear metadata out of .nam files, and scans a folder of them on a background thread for the capture library.

#include <algorithm>
#include <cctype>
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"
#include "wdlstring.h"
#include "dirscan.h"

#include "NAMTheme.h"

// Pulls the gear description the trainer writes into a .nam file's metadata.
inline NAMGearInfo ReadGearInfo(const nlohmann::json& metadata, const WDL_String& path)
{
  NAMGearInfo info;
  info.path = path.Get();
  WDL_String stem(path.get_filepart());
  stem.remove_fileext();
  info.fileStem = stem.Get();
  if (!metadata.is_object())
    return info;

  auto str = [&metadata](const char* key) {
    const auto it = metadata.find(key);
    return (it != metadata.end() && it->is_string()) ? it->get<std::string>() : std::string();
  };
  info.name = str("name");
  info.make = str("gear_make");
  info.model = str("gear_model");
  info.modeledBy = str("modeled_by");
  info.gearType = ParseGearType(str("gear_type"));
  info.toneType = ParseToneType(str("tone_type"));
  const auto gain = metadata.find("gain");
  if (gain != metadata.end() && gain->is_number())
    info.gain = gain->get<double>();
  return info;
}

class NAMLibrary
{
public:
  ~NAMLibrary() { Cancel(); }

  // Starts reading every .nam file in the folder (and two levels of subfolders). Replaces any scan in progress.
  void Scan(const std::string& folder)
  {
    Cancel();
    {
      std::lock_guard<std::mutex> lock(mMutex);
      mFolder = folder;
      mEntries.clear();
    }
    mCancel = false;
    mScanning = true;
    mGeneration++;
    mThread = std::thread([this, folder]() { Run(folder); });
  }

  void Cancel()
  {
    mCancel = true;
    if (mThread.joinable())
      mThread.join();
    mScanning = false;
  }

  // Bumped whenever the entries change, so the UI knows when to take a new snapshot.
  unsigned Generation() const { return mGeneration; }
  bool IsScanning() const { return mScanning; }

  void Snapshot(std::vector<NAMGearInfo>& entries, std::string& folder) const
  {
    std::lock_guard<std::mutex> lock(mMutex);
    entries = mEntries;
    folder = mFolder;
  }

private:
  static constexpr size_t kMaxFiles = 600;

  void Collect(const std::string& dir, int depth, std::vector<std::string>& files)
  {
    WDL_DirScan scan;
    if (scan.First(dir.c_str()) != 0)
      return;
    do
    {
      if (mCancel || files.size() >= kMaxFiles)
        return;
      const char* fn = scan.GetCurrentFN();
      if (!fn || fn[0] == '.')
        continue;
      WDL_String full;
      scan.GetCurrentFullFN(&full);
      if (scan.GetCurrentIsDirectory())
      {
        if (depth > 0)
          Collect(full.Get(), depth - 1, files);
      }
      else
      {
        std::string ext(full.get_fileext());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".nam")
          files.push_back(full.Get());
      }
    } while (scan.Next() == 0);
  }

  void Run(const std::string folder)
  {
    std::vector<std::string> files;
    Collect(folder, 2, files);
    std::sort(files.begin(), files.end());

    // Skip building the weights; only the metadata matters here.
    const nlohmann::json::parser_callback_t skipWeights =
      [](int depth, nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
        return !(event == nlohmann::json::parse_event_t::key && depth == 1 && parsed == "weights");
      };

    for (size_t i = 0; i < files.size() && !mCancel; i++)
    {
      std::ifstream stream(files[i]);
      if (!stream)
        continue;
      const nlohmann::json j = nlohmann::json::parse(stream, skipWeights, false);
      if (j.is_discarded() || !j.is_object())
        continue;
      const auto metadata = j.find("metadata");
      NAMGearInfo gear =
        ReadGearInfo(metadata != j.end() ? *metadata : nlohmann::json(), WDL_String(files[i].c_str()));
      {
        std::lock_guard<std::mutex> lock(mMutex);
        mEntries.push_back(std::move(gear));
      }
      if (i % 8 == 7)
        mGeneration++;
    }
    mScanning = false;
    mGeneration++;
  }

  mutable std::mutex mMutex;
  std::vector<NAMGearInfo> mEntries;
  std::string mFolder;
  std::thread mThread;
  std::atomic<bool> mCancel = false;
  std::atomic<bool> mScanning = false;
  std::atomic<unsigned> mGeneration = 0;
};
