//
// Created by GRZ on 2026/11/4.
//

#ifndef DEEPVISIONWORKBENCH_RESULTVISUALIZATION_H
#define DEEPVISIONWORKBENCH_RESULTVISUALIZATION_H

#include <QWidget>
#include <QStringList>
#include <QPixmap>
#include <opencv2/core/mat.hpp>

struct OnnxInferenceResult;
struct OnnxModelInfo;
class QDialog;


QT_BEGIN_NAMESPACE

namespace Ui {
    class ResultVisualization;
}

QT_END_NAMESPACE

class ResultVisualization : public QWidget {
    Q_OBJECT

public:
    explicit ResultVisualization(QWidget *parent = nullptr);

    ~ResultVisualization() override;

    // 显示图片并同步滚动区域的内容尺寸。
    void setImage(const QPixmap &image);

    void setInferenceResult(const OnnxInferenceResult &result);
    void setModelInfo(const OnnxModelInfo &info);

private slots:
    // 由 setupUi 的 connectSlotsByName 自动连接。
    void on_importModelButton_clicked();
    void on_openImageButton_clicked();
    void on_openDirectoryButton_clicked();
    void on_startInferenceButton_clicked();
    void on_clearResultsButton_clicked();
    void on_refreshImagesButton_clicked();
    void on_saveResultButton_clicked();
    void on_previousImageButton_clicked();
    void on_nextImageButton_clicked();
    void on_zoomInButton_clicked();
    void on_zoomOutButton_clicked();
    void on_zoomComboBox_currentIndexChanged(int index);
    void on_fitToWindowButton_clicked();
    void on_fullScreenButton_clicked();
    void on_previousThumbnailsButton_clicked();
    void on_nextThumbnailsButton_clicked();
    void on_imageListWidget_currentRowChanged(int row);
    void on_thumbnailListWidget_currentRowChanged(int row);
    void on_confidenceSlider_valueChanged(int value);
    void on_confidenceSpinBox_valueChanged(double value);
    void on_nmsSlider_valueChanged(int value);
    void on_nmsSpinBox_valueChanged(double value);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void updateImageDisplay();
    void changeZoom(bool zoomIn);
    void refreshThumbnailList();

    QPixmap sourcePixmap;
    double zoomScale = 1.0;
    bool fitToWindow = false;
    QDialog *fullScreenDialog = nullptr;
    std::vector<QString> imageList;
    QStringList modelClassNames;
private:
    Ui::ResultVisualization *ui;
};


#endif //DEEPVISIONWORKBENCH_RESULTVISUALIZATION_H
