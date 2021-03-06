/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/lite/tools/evaluation/utils.h"

#if !defined(_WIN32)
#include <dirent.h>
#endif
#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>

namespace tflite {
namespace evaluation {

namespace {

TfLiteDelegatePtr CreateNullDelegate() {
  return TfLiteDelegatePtr(nullptr, [](TfLiteDelegate*) {});
}

}  // namespace

std::string StripTrailingSlashes(const std::string& path) {
  int end = path.size();
  while (end > 0 && path[end - 1] == '/') {
    end--;
  }
  return path.substr(0, end);
}

bool ReadFileLines(const std::string& file_path,
                   std::vector<std::string>* lines_output) {
  if (!lines_output) {
    return false;
  }
  std::ifstream stream(file_path.c_str());
  if (!stream) {
    return false;
  }
  std::string line;
  while (std::getline(stream, line)) {
    lines_output->push_back(line);
  }
  std::sort(lines_output->begin(), lines_output->end());
  return true;
}

#if !defined(_WIN32)
TfLiteStatus GetSortedFileNames(
    const std::string& directory, std::vector<std::string>* result,
    const std::unordered_set<std::string>& extensions) {
  DIR* dir;
  struct dirent* ent;
  if (result == nullptr) {
    return kTfLiteError;
  }
  result->clear();
  std::string dir_path = StripTrailingSlashes(directory);
  if ((dir = opendir(dir_path.c_str())) != nullptr) {
    while ((ent = readdir(dir)) != nullptr) {
      if (ent->d_type == DT_DIR) continue;
      std::string filename(std::string(ent->d_name));
      size_t lastdot = filename.find_last_of('.');
      std::string ext = lastdot != std::string::npos ? filename.substr(lastdot)
                                                     : std::string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (!extensions.empty() && extensions.find(ext) == extensions.end()) {
        continue;
      }
      result->emplace_back(dir_path + "/" + filename);
    }
    closedir(dir);
  } else {
    return kTfLiteError;
  }
  std::sort(result->begin(), result->end());
  return kTfLiteOk;
}
#endif

std::string GetPathFromPath(const std::string& str) {
  int pos = str.find_last_of("/\\");
  if (pos == std::string::npos) return "";
  return str.substr(0, pos + 1);
}

// Get md5 of ground truth images
std::string GetMD5(const std::string& dir) {
  std::string cmd = "md5sum " + dir;
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    TFLITE_LOG(ERROR) << "Could not get md5 of ground truth images.";
  }
  char md5[33];
  fgets(md5, sizeof(md5), pipe);
  pclose(pipe);
  return std::string(md5);
}

std::string GetGroundTruthImagePath(const std::string& dir) {
  std::string path = GetPathFromPath(dir);
  std::string cmd = "tar -xf " + dir + " -C " + path + " --no-same-owner";
  // We don't use popen because `tar -xf` may last several seconds. `fgets` will return soon
  // and `pclose` will be executed before all files have been extracted from .tar file.
  // But `system` will normally sync until the shell command finish.`
  system(cmd.c_str());

  cmd = "tar -tf " + dir + " | head -1";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    TFLITE_LOG(ERROR) << "Could not uncompress ground truth images.";
  }
  char ins_path[1024];
  fgets(ins_path, sizeof(ins_path), pipe);
  pclose(pipe);

  auto ret = path + std::string(ins_path);
  ret = ret.substr(0, ret.find_last_of("/"));
  return ret;
}

// same as numpy.percentile()
float GetPercentile(std::vector<float>& data, const int q) {
  std::sort(data.begin(), data.end());
  float pos = (data.size() - 1) * q / 100.f;
  float pos_floor = floor(pos);
  float pos_ceil = ceil(pos);
  float target = data.at(pos_floor) + (data.at(pos_ceil) - data.at(pos_floor)) * (pos - pos_floor);
  return target;
}

bool DeleteDir(const std::string& dir) {
  std::string cmd = "rm -rf " + dir;
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    TFLITE_LOG(ERROR) << "Could not delete dir: " << dir;
    return false;
  }
  pclose(pipe);
  return true;
}

// TODO(b/138448769): Migrate delegate helper APIs to lite/testing.
TfLiteDelegatePtr CreateNNAPIDelegate() {
#if defined(__ANDROID__)
  return TfLiteDelegatePtr(
      NnApiDelegate(),
      // NnApiDelegate() returns a singleton, so provide a no-op deleter.
      [](TfLiteDelegate*) {});
#else
  return CreateNullDelegate();
#endif  // defined(__ANDROID__)
}

TfLiteDelegatePtr CreateNNAPIDelegate(StatefulNnApiDelegate::Options options) {
#if defined(__ANDROID__)
  return TfLiteDelegatePtr(
      new StatefulNnApiDelegate(options), [](TfLiteDelegate* delegate) {
        delete reinterpret_cast<StatefulNnApiDelegate*>(delegate);
      });
#else
  return CreateNullDelegate();
#endif  // defined(__ANDROID__)
}

#if TFLITE_SUPPORTS_GPU_DELEGATE
TfLiteDelegatePtr CreateGPUDelegate(TfLiteGpuDelegateOptionsV2* options) {
  return TfLiteDelegatePtr(TfLiteGpuDelegateV2Create(options),
                           &TfLiteGpuDelegateV2Delete);
}
#endif  // TFLITE_SUPPORTS_GPU_DELEGATE

TfLiteDelegatePtr CreateGPUDelegate() {
#if TFLITE_SUPPORTS_GPU_DELEGATE
  TfLiteGpuDelegateOptionsV2 options = TfLiteGpuDelegateOptionsV2Default();
  options.inference_priority1 = TFLITE_GPU_INFERENCE_PRIORITY_MIN_LATENCY;
  options.inference_preference =
      TFLITE_GPU_INFERENCE_PREFERENCE_SUSTAINED_SPEED;

  return CreateGPUDelegate(&options);
#else
  return CreateNullDelegate();
#endif  // TFLITE_SUPPORTS_GPU_DELEGATE
}

TfLiteDelegatePtr CreateHexagonDelegate(
    const std::string& library_directory_path, bool profiling) {
#if defined(__ANDROID__) && (defined(__arm__) || defined(__aarch64__))
  TfLiteHexagonDelegateOptions options = {0};
  options.print_graph_profile = profiling;
  return CreateHexagonDelegate(&options, library_directory_path);
#else
  return CreateNullDelegate();
#endif  // defined(__ANDROID__)
}

#if defined(__ANDROID__) && (defined(__arm__) || defined(__aarch64__))
TfLiteDelegatePtr CreateHexagonDelegate(
    const TfLiteHexagonDelegateOptions* options,
    const std::string& library_directory_path) {
  if (library_directory_path.empty()) {
    TfLiteHexagonInit();
  } else {
    TfLiteHexagonInitWithPath(library_directory_path.c_str());
  }

  TfLiteDelegate* delegate = TfLiteHexagonDelegateCreate(options);
  if (!delegate) {
    TfLiteHexagonTearDown();
    return CreateNullDelegate();
  }
  return TfLiteDelegatePtr(delegate, [](TfLiteDelegate* delegate) {
    TfLiteHexagonDelegateDelete(delegate);
    TfLiteHexagonTearDown();
  });
}
#endif

// TODO(b/149248802): include XNNPACK delegate when the issue is resolved.
#if defined(__Fuchsia__) || defined(TFLITE_WITHOUT_XNNPACK)
TfLiteDelegatePtr CreateXNNPACKDelegate(int num_threads) {
  return CreateNullDelegate();
}
#else
TfLiteDelegatePtr CreateXNNPACKDelegate() {
  TfLiteXNNPackDelegateOptions xnnpack_options =
      TfLiteXNNPackDelegateOptionsDefault();
  return CreateXNNPACKDelegate(&xnnpack_options);
}

TfLiteDelegatePtr CreateXNNPACKDelegate(
    const TfLiteXNNPackDelegateOptions* xnnpack_options) {
  auto xnnpack_delegate = TfLiteXNNPackDelegateCreate(xnnpack_options);
  return TfLiteDelegatePtr(xnnpack_delegate, [](TfLiteDelegate* delegate) {
    TfLiteXNNPackDelegateDelete(delegate);
  });
}

TfLiteDelegatePtr CreateXNNPACKDelegate(int num_threads) {
  auto opts = TfLiteXNNPackDelegateOptionsDefault();
  // Note that we don't want to use the thread pool for num_threads == 1.
  opts.num_threads = num_threads > 1 ? num_threads : 0;
  return CreateXNNPACKDelegate(&opts);
}
#endif
}  // namespace evaluation
}  // namespace tflite
