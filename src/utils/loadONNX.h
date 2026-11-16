//
// Created by GRZ on 2026/11/3.
//

#ifndef VISIONDESK_LOADONNX_H
#define VISIONDESK_LOADONNX_H

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

// 模型可正常加载但没有类别元数据时，success 为 true，classNames 为空。
struct OnnxModelInfo {
    bool success = false;
    std::string error;
    std::string warning;
    // 下标就是类别 ID；仅接受从 0 开始连续的类别 ID。
    std::vector<std::string> classNames;
};

OnnxModelInfo readOnnxModelInfo(const std::filesystem::path &modelPath);

struct OnnxDetectionOptions {
    float confidenceThreshold = 0.25f;
    float nmsIouThreshold = 0.45f;
    size_t classCount = 2;
};

struct OnnxDetection {
    cv::Rect2f box;
    int classId =0;
    float confidence = 0;
};

struct OnnxOutputSummary {
    std::string name;
    std::vector<int64_t> shape;
    int elementType = 0;
    size_t elementCount = 0;
    bool isTensor= false;
    std::vector<float> firstValues;
};

struct OnnxInferenceResult {
    bool success = false;
    std::string error;
    std::string inputName;
    std::vector<int64_t> modelInputShape;
    std::vector<int64_t> inputShape;
    double inferenceTimeMs = 0;
    std::vector<OnnxOutputSummary> outputs;
    cv::Mat sourceImage;
    std::vector<OnnxDetection> detections;
};

OnnxInferenceResult runOnnxInference(const std::filesystem::path& modelPath,
                                    const std::filesystem::path& imagePath,
                                    int inputWidth,
                                    int inputHeight,
                                    const OnnxDetectionOptions* detectionOptions = nullptr);

int loadONNX(const std::filesystem::path &modelPath,
             const std::filesystem::path &imagePath,
             int inputWidth = 640, int inputHeight = 640);


#endif //VISIONDESK_LOADONNX_H
