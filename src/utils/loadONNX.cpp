//
// Created by GRZ on 2026/11/3.
//

#include "loadONNX.h"
#include <onnxruntime_cxx_api.h>
#include <opencv2/dnn.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
    // 只解析字符串列表/类别字典，不执行 names 中的任何表达式。
    // 支持 JSON 和 Ultralytics 常用的 Python 字典字符串。
    class ClassNamesParser {
    public:
        explicit ClassNamesParser(const std::string &text) : text(text) {}

        std::vector<std::string> parse() {
            std::map<size_t, std::string> names;
            const bool dictionary = consume('{');
            if (!dictionary && !consume('['))
                fail();
            const char closing = dictionary ? '}' : ']';
            if (!consume(closing)) {
                do {
                    size_t id = names.size();
                    if (dictionary) {
                        skipSpace();
                        std::string key;
                        if (peek() == '\'' || peek() == '"') {
                            key = readString();
                        } else {
                            while (std::isdigit(static_cast<unsigned char>(peek())))
                                key += text[position++];
                        }
                        if (key.empty() || key.find_first_not_of("0123456789") != std::string::npos)
                            fail();
                        const auto number = std::stoull(key);
                        if (number >= 100000)
                            fail();
                        id = static_cast<size_t>(number);
                        if (!consume(':'))
                            fail();
                    }
                    const auto name = readString();
                    if (name.empty() || names.size() >= 100000 || !names.emplace(id, name).second)
                        fail();
                    if (!consume(',')) {
                        if (!consume(closing))
                            fail();
                        break;
                    }
                    // Python 容器允许结尾逗号。
                    if (consume(closing))
                        break;
                } while (true);
            }
            skipSpace();
            if (position != text.size())
                fail();
            std::vector<std::string> result;
            for (const auto &entry : names) {
                if (entry.first != result.size())
                    fail();
                result.push_back(entry.second);
            }
            return result;
        }

    private:
        [[noreturn]] static void fail() {
            throw std::runtime_error("Unsupported names metadata: expected a string list or a dictionary with consecutive class IDs starting at 0.");
        }
        char peek() const { return position < text.size() ? text[position] : '\0'; }
        void skipSpace() {
            while (position < text.size() && std::isspace(static_cast<unsigned char>(peek())))
                ++position;
        }
        bool consume(char value) {
            skipSpace();
            if (peek() != value)
                return false;
            ++position;
            return true;
        }
        uint32_t readHex(int digits) {
            uint32_t value = 0;
            for (int i = 0; i < digits; ++i) {
                const char c = peek();
                const int digit = c >= '0' && c <= '9' ? c - '0' :
                    c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (digit < 0)
                    fail();
                ++position;
                value = value * 16 + static_cast<uint32_t>(digit);
            }
            return value;
        }
        static void appendUtf8(std::string &result, uint32_t value) {
            if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
                fail();
            if (value <= 0x7f) {
                result += static_cast<char>(value);
            } else if (value <= 0x7ff) {
                result += static_cast<char>(0xc0 | (value >> 6));
                result += static_cast<char>(0x80 | (value & 0x3f));
            } else if (value <= 0xffff) {
                result += static_cast<char>(0xe0 | (value >> 12));
                result += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
                result += static_cast<char>(0x80 | (value & 0x3f));
            } else {
                result += static_cast<char>(0xf0 | (value >> 18));
                result += static_cast<char>(0x80 | ((value >> 12) & 0x3f));
                result += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
                result += static_cast<char>(0x80 | (value & 0x3f));
            }
        }
        std::string readString() {
            skipSpace();
            const char quote = peek();
            if (quote != '\'' && quote != '"')
                fail();
            ++position;
            std::string result;
            while (position < text.size()) {
                char c = text[position++];
                if (c == quote)
                    return result;
                if (c != '\\') {
                    result += c;
                    continue;
                }
                if (position == text.size())
                    fail();
                c = text[position++];
                switch (c) {
                case '\\': case '\'': case '"': case '/': result += c; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'x': appendUtf8(result, readHex(2)); break;
                case 'U': appendUtf8(result, readHex(8)); break;
                case 'u': {
                    uint32_t value = readHex(4);
                    if (value >= 0xd800 && value <= 0xdbff) {
                        if (position + 2 > text.size() || text[position] != '\\' || text[position + 1] != 'u')
                            fail();
                        position += 2;
                        const uint32_t low = readHex(4);
                        if (low < 0xdc00 || low > 0xdfff)
                            fail();
                        value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
                    }
                    appendUtf8(result, value);
                    break;
                }
                default: fail();
                }
            }
            fail();
        }
        const std::string &text;
        size_t position = 0;
    };

    void printShape(const std::vector<int64_t>& shape) {
        std::cout<<"[";
        for (int i = 0;i<shape.size();i++) {
            if (i!=0)
                std::cout<<",";
            std::cout<<shape[i];
        }
        std::cout<<"]";
    }

    void decodeDetections(const Ort::Value& output,const OnnxDetectionOptions& options,
                        float scale,int padLeft,int padTop,OnnxInferenceResult& result) {
        if (!output.IsTensor())
            throw std::runtime_error("Output is not a tensor");
        const auto info = output.GetTensorTypeAndShapeInfo();
        const auto shape = info.GetShape();
        const auto features = static_cast<int64_t>(4+options.classCount);
        if (info.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||shape.size()!=3||shape[0]!=1||
            (shape[1]!=features&&shape[2]!=features))
            throw std::runtime_error("Expected YOLO output [1, 4+classes, N] or [1, N, 4+classes]. "
                                 "Check the class names/count and model export format.");
        const bool channelsFirst = shape[1] == features;
        const int64_t count = channelsFirst ? shape[2] : shape[1];
        const auto* data = output.GetTensorData<float>();
        const auto value = [&](int candidate,int feature) {
            return data[channelsFirst?feature*count+candidate:candidate*features+feature];
        };

        std::vector<OnnxDetection> candidates;
        for (int i = 0;i<count;i++) {
            int classId = -1;
            float score = -1;
            for (int c = 0;c<options.classCount;c++) {
                const float probability = value(i,c+4);
                if (std::isfinite(probability)&&probability>score) {
                    score = probability;
                    classId = static_cast<int>(c);
                }
            }
            if (classId<0||score<=options.confidenceThreshold)
                continue;
            const float cx= value(i,0),cy = value(i,1);
            const float width = value(i,2),height = value(i,3);
            if (!std::isfinite(cx)||!std::isfinite(cy)||!std::isfinite(width)||!std::isfinite(height)||
                cx<0||cy<0||width<=0||height<=0)
                continue;
            const float x1 = std::clamp((cx-width/2-padLeft)/scale,0.0f,static_cast<float>(result.sourceImage.cols));
            const float y1 = std::clamp((cy-height/2-padTop)/scale,0.0f,static_cast<float>(result.sourceImage.rows));
            const float x2 = std::clamp((cx+width/2-padLeft)/scale,0.0f,static_cast<float>(result.sourceImage.cols));
            const float y2 = std::clamp((cy+height/2-padTop)/scale,0.0f,static_cast<float>(result.sourceImage.rows));

            if (x2>x1&&y2>y1)
                candidates.push_back({cv::Rect2f(x1,y1,x2-x1,y2-y1),classId,score});
        }
        for (size_t c = 0;c<options.classCount;c++) {
            std::vector<cv::Rect2d> boxes;
            std::vector<float> scores;
            std::vector<size_t> indices;
            for (size_t i = 0;i<candidates.size();i++) {
                if (candidates[i].classId!=static_cast<int>(c))
                    continue;
                boxes.push_back(candidates[i].box);
                scores.push_back(candidates[i].confidence);
                indices.push_back(i);
            }
            std::vector<int> kept;
            cv::dnn::NMSBoxes(boxes,scores,options.confidenceThreshold,options.nmsIouThreshold,kept);
            for (const auto& i : kept)
                result.detections.push_back(candidates[indices[i]]);
        }
        std::sort(result.detections.begin(),result.detections.end(),
                  [&](const OnnxDetection& a,const OnnxDetection& b) {
                      return a.confidence>b.confidence;
                  });
    }
}

OnnxModelInfo readOnnxModelInfo(const std::filesystem::path &modelPath) {
    OnnxModelInfo result;
    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "readOnnxModelInfo");
        Ort::SessionOptions options;
        Ort::Session session(env, modelPath.c_str(), options);
        Ort::AllocatorWithDefaultOptions allocator;
        const auto metadata = session.GetModelMetadata();
        const auto names = metadata.LookupCustomMetadataMapAllocated("names", allocator);
        if (!names || !*names.get()) {
            result.warning = "The model does not contain class names metadata. Supply a class label file before detection.";
        } else {
            try {
                const std::string text(names.get());
                result.classNames = ClassNamesParser(text).parse();
                if (result.classNames.empty())
                    result.warning = "The model contains an empty class names list.";
            } catch (const std::exception &e) {
                result.warning = std::string("Cannot read class names metadata: ") + e.what();
            }
        }
        result.success = true;
    } catch (const std::exception &e) {
        result.error = e.what();
    }
    return result;
}

OnnxInferenceResult runOnnxInference(const std::filesystem::path &modelPath, const std::filesystem::path &imagePath, int inputWidth, int inputHeight, const OnnxDetectionOptions *detectionOptions) {
    OnnxInferenceResult result;
    try {
        if (detectionOptions&&
            (!std::isfinite(detectionOptions->confidenceThreshold)||
            !std::isfinite(detectionOptions->nmsIouThreshold)||
            detectionOptions->confidenceThreshold<0||detectionOptions->confidenceThreshold>1||
            detectionOptions->nmsIouThreshold<0||detectionOptions->nmsIouThreshold>1||
            detectionOptions->classCount==0||detectionOptions->classCount>100000))
            throw std::runtime_error("Invalid detection thresholds or class count.");
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING,"loadONNX");
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        Ort::Session session(env, modelPath.c_str(), options);
        Ort::AllocatorWithDefaultOptions allocator;

        if (session.GetInputCount()!=1||session.GetOutputCount()==0)
            throw std::runtime_error("This demo requires one image input and at least one output.");

        const auto inputName = session.GetInputNameAllocated(0, allocator);
        const auto inputType = session.GetInputTypeInfo(0);
        if (inputType.GetONNXType()!=ONNX_TYPE_TENSOR)
            throw std::runtime_error("This demo requires tensor input.");
        const auto inputInfo = inputType.GetTensorTypeAndShapeInfo();
        auto inputShape = inputInfo.GetShape();
        result.inputName = inputName.get();
        result.modelInputShape = inputShape;

        if (inputInfo.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||
            inputShape.size()!=4||
            (inputShape[0]>0&&inputShape[0]!=1)||
            (inputShape[1]>0&&inputShape[1]!=3))
            throw std::runtime_error("This demo requires float input of shape [1, 3, inputHeight, inputWidth].");

        inputShape[0] = 1;
        inputShape[1] = 3;
        if (inputShape[2]<0)
            inputShape[2] = inputHeight;
        if (inputShape[3]<0)
            inputShape[3] = inputWidth;
        for (const auto dimension : inputShape) {
            if (dimension<=0||dimension>std::numeric_limits<size_t>::max())
                throw std::runtime_error("This demo requires number of dimensions.");
        }
        result.inputShape = inputShape;
        std::ifstream imageFile(imagePath,std::ios::binary);
        if (!imageFile)
            throw std::runtime_error("Failed to open image file.");
        const std::vector<unsigned char> imageBytes{
            std::istreambuf_iterator<char>(imageFile), std::istreambuf_iterator<char>()
        };
        const cv::Mat image = cv::imdecode(imageBytes, cv::IMREAD_COLOR);
        if (image.empty())
            throw std::runtime_error("Failed to read image file.");

        const cv::Size size(static_cast<int>(inputShape[3]), static_cast<int>(inputShape[2]));
        cv::Mat prepared = image;
        float scale = 1;
        int padLeft = 0, padTop = 0;
        if (detectionOptions) {
            scale = (std::min)(static_cast<float>(size.width) / image.cols,
                               static_cast<float>(size.height) / image.rows);
            const int resizedWidth = std::clamp(static_cast<int>(std::round(image.cols * scale)), 1, size.width);
            const int resizedHeight = std::clamp(static_cast<int>(std::round(image.rows * scale)), 1, size.height);
            padLeft = (size.width - resizedWidth) / 2;
            padTop = (size.height - resizedHeight) / 2;
            cv::Mat resized;
            cv::resize(image, resized, cv::Size(resizedWidth, resizedHeight));

            cv::copyMakeBorder(resized, prepared, padTop, size.height - resizedHeight - padTop,
                padLeft, size.width - resizedWidth - padLeft, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
            result.sourceImage = image;
        }

        cv::Mat blob = cv::dnn::blobFromImage(prepared, 1.0 / 255.0, size,
                                             cv::Scalar(), true, false, CV_32F);

        const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto inputTensor = Ort::Value::CreateTensor<float>(memoryInfo,
            blob.ptr<float>(), blob.total(), inputShape.data(), inputShape.size());
        const char *inputNames[] = {inputName.get()};

        std::vector<std::string> outputNameStorage;
        outputNameStorage.reserve(session.GetOutputCount());
        for (size_t i = 0; i < session.GetOutputCount(); ++i) {
            const auto name = session.GetOutputNameAllocated(i, allocator);
            outputNameStorage.emplace_back(name.get());
        }

        std::vector<const char *> outputNames;
        outputNames.reserve(outputNameStorage.size());
        for (const auto &name : outputNameStorage)
            outputNames.push_back(name.c_str());


        const auto start = std::chrono::steady_clock::now();
        const auto outputs = session.Run(Ort::RunOptions{nullptr}, inputNames,
            &inputTensor, 1, outputNames.data(), outputNames.size());
        result.inferenceTimeMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        for (size_t i = 0; i < outputs.size(); ++i) {
            OnnxOutputSummary summary;
            summary.name = outputNames[i];
            summary.isTensor = outputs[i].IsTensor();
            if (!outputs[i].IsTensor()) {
                result.outputs.push_back(std::move(summary));
                continue;
            }
            const auto info = outputs[i].GetTensorTypeAndShapeInfo();
            summary.shape = info.GetShape();
            summary.elementType = info.GetElementType();
            summary.elementCount = info.GetElementCount();
            if (info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                const float *data = outputs[i].GetTensorData<float>();
                for (size_t j = 0; j < (std::min)(info.GetElementCount(), size_t{10}); ++j)
                    summary.firstValues.push_back(data[j]);
            }
            result.outputs.push_back(std::move(summary));
        }
        if (detectionOptions) {
            if (outputs.size() != 1)
                throw std::runtime_error("Detection requires a single YOLO output tensor.");
            decodeDetections(outputs[0], *detectionOptions, scale, padLeft, padTop, result);
        }
        result.success = true;
    }catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
}

int loadONNX(const std::filesystem::path &modelPath,
             const std::filesystem::path &imagePath,
             int inputWidth, int inputHeight) {
    const auto result = runOnnxInference(modelPath, imagePath, inputWidth, inputHeight);
    if (!result.success) {
        std::cerr << "ONNX demo failed: " << result.error << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Input: " << result.inputName << ' ';
    printShape(result.modelInputShape);
    std::cout << "\nCPU inference: " << result.inferenceTimeMs << " ms\n";
    for (const auto &output : result.outputs) {
        std::cout << "Output: " << output.name;
        if (!output.isTensor) {
            std::cout << " (non-tensor output)\n";
            continue;
        }
        std::cout << ' ';
        printShape(output.shape);
        std::cout << ", type=" << output.elementType << ", elements=" << output.elementCount << '\n';
        if (!output.firstValues.empty()) {
            std::cout << "  First values:";
            for (const float value : output.firstValues)
                std::cout << ' ' << value;
            std::cout << '\n';
        }
    }
    return EXIT_SUCCESS;
}
