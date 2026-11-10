//
// Created by GRZ on 2026/11/4.
//

// You may need to build the project (run Qt uic code generator) to get "ui_ResultVisualization.h" resolved

#include "ResultVisualization.h"
#include "../utils/loadONNX.h"

#include <complex>
#include <algorithm>
#include <iostream>
#include <QFileDialog>
#include <QFileInfo>
#include <qmessagebox.h>
#include <QScrollBar>
#include <QDialog>
#include <QEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QColor>
#include <QTextDocument>
#include <cmath>

#include "ui_ResultVisualization.h"


ResultVisualization::ResultVisualization(QWidget *parent) : QWidget(parent), ui(new Ui::ResultVisualization) {
    ui->setupUi(this);

    ui->zoomComboBox->setEditable(false);
    ui->zoomComboBox->insertItem(0, tr("适应窗口"));
    ui->zoomComboBox->setCurrentIndex(0);
    ui->imageScrollArea->viewport()->installEventFilter(this);
    ui->statusFrame->setVisible(true);

    // 设置缩略图列表的显示模式为图标模式，水平滚动，不换行，静态移动
    ui->thumbnailListWidget->setViewMode(QListView::IconMode);
    ui->thumbnailListWidget->setFlow(QListView::LeftToRight);
    ui->thumbnailListWidget->setWrapping(false);
    ui->thumbnailListWidget->setMovement(QListView::Static);

    ui->thumbnailListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->thumbnailListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

ResultVisualization::~ResultVisualization() {
    if (fullScreenDialog)
        fullScreenDialog->reject();
    delete ui;
}

// 按图片实际显示尺寸调整 QLabel，让 QScrollArea 自动计算滚动范围。
void ResultVisualization::setImage(const QPixmap &image) {
    sourcePixmap = image;
    zoomScale = 1.0;
    fitToWindow = true;
    updateImageDisplay();
}

void ResultVisualization::updateImageDisplay() {
    if (sourcePixmap.isNull()) {
        ui->imageLabel->clear();
        ui->imageLabel->resize(380, 280);
        return;
    }

    if (fitToWindow) {
        // 不把当前滚动条占用的宽高计入可用区域，避免适应时反复缩放。
        const QSize available = ui->imageScrollArea->maximumViewportSize();
        if (available.isEmpty())
            return;
        const QSizeF originalSize = sourcePixmap.deviceIndependentSize();
        zoomScale = qMin(available.width() / originalSize.width(),
                         available.height() / originalSize.height());
    }

    const QSize targetSize(qMax(1, qRound(sourcePixmap.width() * zoomScale)),
                           qMax(1, qRound(sourcePixmap.height() * zoomScale)));
    const QPixmap displayed = sourcePixmap.scaled(
        targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui->imageLabel->setScaledContents(false);
    ui->imageLabel->setPixmap(displayed);
    ui->imageLabel->resize(displayed.deviceIndependentSize().toSize());

    const QSignalBlocker blocker(ui->zoomComboBox);
    if (fitToWindow) {
        ui->zoomComboBox->setItemText(0,
            tr("适应窗口 (%1%)").arg(zoomScale * 100.0, 0, 'f', 1));
        ui->zoomComboBox->setCurrentIndex(0);
    } else {
        ui->zoomComboBox->setCurrentIndex(ui->zoomComboBox->findText(
            QString::number(zoomScale * 100.0, 'f', 0) + '%'));
    }
}

bool ResultVisualization::eventFilter(QObject *watched, QEvent *event) {
    if (watched == ui->imageScrollArea->viewport() &&
        event->type() == QEvent::Resize && fitToWindow) {
        // 等布局和全屏窗口完成尺寸调整，再重新适应。
        QTimer::singleShot(0, this, [this] {
            if (fitToWindow)
                updateImageDisplay();
        });
    }
    return QWidget::eventFilter(watched, event);
}


// 显示推理结果
void ResultVisualization::setInferenceResult(const OnnxInferenceResult &result) {
    ui->inferenceTimeValue->setText(QString::number(result.inferenceTimeMs) + " ms");
    cv::Mat image = result.sourceImage.clone();
    auto sumConfidence = 0.0;

    ui->detectionsTableWidget->clearContents();
    ui->detectionsTableWidget->setRowCount(static_cast<int>(result.detections.size()));

    int classFlag = ui->classFilterComboBox->currentIndex()-1;

    for (int i = 0; i < result.detections.size(); ++i) {
        const OnnxDetection &detection = result.detections[i];
        auto id = static_cast<int>(detection.classId);
        if (classFlag != -1 && id != classFlag)
            continue;
        auto confidence = detection.confidence;
        sumConfidence += detection.confidence;
        auto className = modelClassNames[id];
        auto text = QString("%1 %2%").arg(className).arg(confidence * 100.0, 0, 'f', 1);
        auto x1 = static_cast<int>(detection.box.x);
        auto y1 = static_cast<int>(detection.box.y);
        auto x2 = static_cast<int>(detection.box.x + detection.box.width);
        auto y2 = static_cast<int>(detection.box.y + detection.box.height);
        QColor color = QColor::fromHsv(static_cast<int>((id * 137) % 360), 190, 210);
        const cv::Scalar boxColor(color.blue(), color.green(), color.red());
        cv::rectangle(image,
            cv::Point(x1, y1),
            cv::Point(x2, y2),
            boxColor, 2);

        const std::string label = text.toStdString();
        const int font = cv::FONT_HERSHEY_DUPLEX;
        const double fontScale = 0.75;
        const int thickness = 2;
        const int padding = 4;
        int baseline = 0;
        const cv::Size textSize = cv::getTextSize(
            label, font, fontScale, thickness, &baseline);
        const int labelWidth = (std::min)(image.cols, textSize.width + padding * 2);
        const int labelHeight = (std::min)(image.rows, textSize.height + baseline + padding * 2);
        const int labelLeft = std::clamp(x1, 0, image.cols - labelWidth);
        // 优先放在框上方，顶部空间不足时放到框内。
        const int labelTop = std::clamp(
            y1 >= labelHeight ? y1 - labelHeight : y1, 0, image.rows - labelHeight);
        cv::rectangle(image, cv::Rect(labelLeft, labelTop, labelWidth, labelHeight),
            boxColor, cv::FILLED);
        cv::putText(image, label,
            cv::Point(labelLeft + padding, labelTop + padding + textSize.height),
            font, fontScale, cv::Scalar(255, 255, 255), thickness, cv::LINE_AA);

        ui->detectionsTableWidget->setItem(i,0,new QTableWidgetItem(QString::number(i+1)));
        ui->detectionsTableWidget->setItem(i,1,new QTableWidgetItem(className));
        ui->detectionsTableWidget->setItem(i,2,new QTableWidgetItem(QString::number(confidence*100.0,'f',1)));
        ui->detectionsTableWidget->setItem(i,3,new QTableWidgetItem(QString::number(x1)+", "+QString::number(y1)+", "+QString::number(x2)+", "+QString::number(y2)));
    }

    QImage img(image.data, image.cols, image.rows, static_cast<qsizetype>(image.step), QImage::Format_BGR888);
    setImage(QPixmap::fromImage(img));

    ui->totalDetectionsValue->setText(QString::number(result.detections.size()));
    ui->averageConfidenceValue->setText(QString::number(sumConfidence / result.detections.size() * 100.0, 'f', 2) + "%");
    ui->detectionCountValue->setText(QString::number(result.detections.size()));
}

// 设置模型信息
void ResultVisualization::setModelInfo(const OnnxModelInfo &info) {
    if (!info.success)
        return;

    const QSignalBlocker blocker(ui->classFilterComboBox);
    modelClassNames.clear();
    ui->classFilterComboBox->clear();
    ui->classFilterComboBox->addItem(tr("全部类别"), -1);
    QStringList legendRows;
    for (size_t i = 0; i < info.classNames.size(); ++i) {
        const QString name = QString::fromUtf8(info.classNames[i].data(),
                                              static_cast<qsizetype>(info.classNames[i].size()));
        modelClassNames.push_back(name);
        ui->classFilterComboBox->addItem(name, static_cast<int>(i));
        const QString color = QColor::fromHsv(static_cast<int>((i * 137) % 360), 190, 210).name();
        legendRows.push_back(QStringLiteral("<span style=\"color:%1\">&#9632;</span> %2: %3")
            .arg(color).arg(i).arg(name.toHtmlEscaped()));
    }
    ui->classLegendPlaceholderLabel->setTextFormat(Qt::RichText);
    if (modelClassNames.empty()) {
        ui->classLegendCardTitle->setText(tr("类别图例"));
        ui->classLegendPlaceholderLabel->setText(tr("无法读取类别名称，请提供类别标签文件。"));
        ui->classLegendPlaceholderLabel->setToolTip(QString::fromStdString(info.warning));
        ui->modelStatusLabel->setText(tr("模型：ONNX（类别未知）"));
    } else {
        ui->classLegendCardTitle->setText(tr("类别图例 (%1)").arg(modelClassNames.size()));
        ui->classLegendPlaceholderLabel->setText(legendRows.join(QStringLiteral("<br/>")));
        ui->classLegendPlaceholderLabel->setToolTip(modelClassNames.join(QStringLiteral("\n")));
        ui->modelStatusLabel->setText(tr("模型：ONNX（%1 类）").arg(modelClassNames.size()));
    }
    // 图例原来只有一行占位文字，按实际内容保留高度，避免多类别被压缩裁切。
    QTextDocument legendDocument;
    legendDocument.setDefaultFont(ui->classLegendPlaceholderLabel->font());
    legendDocument.setDocumentMargin(0);
    legendDocument.setHtml(ui->classLegendPlaceholderLabel->text());
    legendDocument.setTextWidth(ui->rightPanel->minimumWidth() - 48);
    ui->classLegendPlaceholderLabel->setMinimumHeight(
        static_cast<int>(std::ceil(legendDocument.size().height())) + 4);
}

// 导入模型按钮
void ResultVisualization::on_importModelButton_clicked() {
    auto modelPath = QFileDialog::getOpenFileName(this,
        "选择ONNX模型文件",QDir::currentPath(),"*.onnx");
    if (modelPath.isEmpty()) {
        QMessageBox::information(this,tr("提示"),tr("未选择模型文件"));
        return;
    }
#ifdef _WIN32
    const std::filesystem::path path(modelPath.toStdWString());
#else
    const std::filesystem::path path(modelPath.toStdString());
#endif
    const auto info = readOnnxModelInfo(path);
    if (!info.success) {
        QMessageBox::warning(this, tr("模型加载失败"), QString::fromStdString(info.error));
        return;
    }
    ui->modelPathEdit->setText(modelPath);
    const QString modelName = QFileInfo(modelPath).fileName();
    ui->modelNameValue->setText(modelName);
    setModelInfo(info);
    if (!info.warning.empty()) {
        QMessageBox::information(this, tr("类别信息"),
            tr("模型已加载，但没有可用的类别名称。请提供类别标签文件。\n\n%1")
                .arg(QString::fromStdString(info.warning)));
    }
    ui->modelStatusLabel->setText(tr("模型：已加载"));
}


// 打开图片按钮
void ResultVisualization::on_openImageButton_clicked() {
    auto imagePath = QFileDialog::getOpenFileName(this,
        "选择图片文件",QDir::currentPath(),"*.jpg *.jpeg *.png *.bmp");
    if (imagePath.isEmpty()) {
        QMessageBox::information(this,tr("提示"),tr("未选择图片文件"));
        return;
    }
    imageList.clear();
    ui->imageListWidget->clear();
    imageList.push_back(imagePath);
    ui->imageListCardTitle->setText("图片列表 (1)");
    ui->totalImagesValue->setText("1");
    const QString fileName = QFileInfo(imagePath).fileName();
    auto* item = new QListWidgetItem(fileName, ui->imageListWidget);
    item->setData(Qt::UserRole, imagePath);
    item->setToolTip(imagePath);
    ui->imageListWidget->addItem(item);

    QFileInfo fileInfo = QFileInfo(imagePath);
    QDateTime birthTime = fileInfo.birthTime();
    float fileSize = fileInfo.size()/1024.0f/1024.0f;

    QPixmap pixmap(imagePath);
    setImage(pixmap);
    QSize size = pixmap.size();

    ui->imageNameValue->setText(fileName);
    ui->imageSizeValue->setText(QString::number(size.width()) + " x " + QString::number(size.height()));
    if (fileSize > 1)
        ui->fileSizeValue->setText(QString::number(fileSize,'f',2) + " MB");
    else
        ui->fileSizeValue->setText(QString::number(fileSize*1024,'f',2)+"KB");
    ui->captureTimeValue->setText(birthTime.toString("yyyy-MM-dd hh:mm:ss"));
    ui->imageCountStatusLabel->setText(tr("图片数： %1").arg(imageList.size()));
    ui->imageListWidget->setCurrentRow(0);
    refreshThumbnailList();
}

void ResultVisualization::on_openDirectoryButton_clicked() {
    auto directoryPath = QFileDialog::getExistingDirectory(this,
        "选择图片目录", QDir::currentPath());
    if (directoryPath.isEmpty())
        return;
    const QDir dir(directoryPath);
    const QStringList imageFilters = {"*.jpg", "*.jpeg", "*.png", "*.bmp"};
    const QFileInfoList files = dir.entryInfoList(imageFilters, QDir::Files|QDir::Readable, QDir::Name);

    imageList.clear();
    ui->imageListWidget->clear();
    for (const auto & file : files) {
        QString imagePath = file.filePath();
        QString lastImagePath = QDir::fromNativeSeparators(imagePath).section('/',-1);
        imageList.push_back(imagePath);
        auto *item = new QListWidgetItem(lastImagePath, ui->imageListWidget);
        item->setData(Qt::UserRole, imagePath);
        item->setToolTip(imagePath);
    }
    ui->imageListCardTitle->setText(tr("待检测图片 (%1)").arg(files.size()));
    ui->imageCountStatusLabel->setText(tr("图片数： %1").arg(imageList.size()));
    ui->totalImagesValue->setText(QString::number(files.size()));
    ui->imageListWidget->setCurrentRow(0);
    refreshThumbnailList();
}

void ResultVisualization::on_startInferenceButton_clicked() {
    QString modelPath = ui->modelPathEdit->text();
    auto item = ui->imageListWidget->currentItem();
    QString imagePath = item->data(Qt::UserRole).toString();
    OnnxDetectionOptions options;
    options.confidenceThreshold = ui->confidenceSpinBox->value();
    options.nmsIouThreshold = ui->nmsSpinBox->value();
    options.classCount = modelClassNames.size();
    auto result = runOnnxInference(modelPath.toStdString(), imagePath.toStdString(), 640, 640, &options);
    if (!result.success) {
        QMessageBox::warning(this, tr("推理失败"), QString::fromStdString(result.error));
        return;
    }
    setInferenceResult(result);
}

void ResultVisualization::on_clearResultsButton_clicked() {\
    ui->inferenceTimeValue->setText("0 ms");
    ui->detectionCountValue->setText("0");
    ui->detectionsTableWidget->clearContents();
    ui->detectionsTableWidget->setRowCount(0);
    ui->totalDetectionsValue->setText("0");
    ui->averageConfidenceValue->setText("0.00%");
    auto item = ui->imageListWidget->currentItem();
    if (item) {
        QString imagePath = item->data(Qt::UserRole).toString();
        QPixmap pixmap(imagePath);
        setImage(pixmap);
    }
}

void ResultVisualization::on_refreshImagesButton_clicked() {
    if (imageList.empty()) {
        QMessageBox::information(this, tr("提示"), tr("请先添加图片"));
        return;
    }
    ui->imageListWidget->clear();
    for (const auto& imagePath : imageList) {
        QString lastImagePath = QDir::fromNativeSeparators(imagePath).section('/', -1);
        auto* item = new QListWidgetItem(lastImagePath,ui->imageListWidget);
        item->setData(Qt::UserRole, imagePath);
        item->setToolTip(lastImagePath);
    }
    ui->imageListCardTitle->setText(tr("待检测图片 (%1)").arg(imageList.size()));
    ui->totalImagesValue->setText(QString::number(imageList.size()));
    ui->imageListWidget->setCurrentRow(0);
    ui->imageCountStatusLabel->setText(tr("图片数： %1").arg(imageList.size()));
}

void ResultVisualization::on_saveResultButton_clicked() {
    if (sourcePixmap.isNull()) {
        QMessageBox::information(this, tr("提示"), tr("请先加载图片"));
        return;
    }

    QString directory = QDir::currentPath();
    QString fileName = QStringLiteral("result.png");
    if (const auto* item = ui->imageListWidget->currentItem()) {
        const QFileInfo imageInfo(item->data(Qt::UserRole).toString());
        directory = imageInfo.absolutePath();
        fileName = imageInfo.completeBaseName() + QStringLiteral("_result.png");
    }

    const QString pngFilter = tr("PNG 图片 (*.png)");
    const QString jpgFilter = tr("JPEG 图片 (*.jpg *.jpeg)");
    QFileDialog dialog(this, tr("保存图片"), directory);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({pngFilter, jpgFilter});
    dialog.setDefaultSuffix(QStringLiteral("png"));
    dialog.selectFile(fileName);
    connect(&dialog, &QFileDialog::filterSelected, &dialog,
            [&dialog, jpgFilter](const QString& filter) {
        dialog.setDefaultSuffix(filter == jpgFilter
            ? QStringLiteral("jpg") : QStringLiteral("png"));
    });

    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;

    const QString savePath = dialog.selectedFiles().first();
    const QString suffix = QFileInfo(savePath).suffix().toLower();
    if (suffix != QStringLiteral("png") && suffix != QStringLiteral("jpg") &&
        suffix != QStringLiteral("jpeg")) {
        QMessageBox::warning(this, tr("保存失败"), tr("请使用 .png、.jpg 或 .jpeg 扩展名"));
        return;
    }

    const char* format = suffix == QStringLiteral("png") ? "PNG" : "JPG";
    if (!sourcePixmap.save(savePath, format)) {
        QMessageBox::warning(this, tr("保存失败"),
            tr("无法保存图片，请检查保存位置和写入权限。\n%1").arg(savePath));
        return;
    }

    QMessageBox::information(this, tr("保存成功"), tr("图片已保存至：\n%1").arg(savePath));
}

void ResultVisualization::on_previousImageButton_clicked() {
    int currentRow = ui->imageListWidget->currentRow();
    if (currentRow > 0) {
        ui->imageListWidget->setCurrentRow(currentRow - 1);
    } else {
        QMessageBox::information(this, tr("提示"), tr("当前已是第一张图片"));
        return;
    }
}

void ResultVisualization::on_nextImageButton_clicked() {
    int currentRow = ui->imageListWidget->currentRow();
    if (currentRow < ui->imageListWidget->count() - 1) {
        ui->imageListWidget->setCurrentRow(currentRow + 1);
    } else {
        QMessageBox::information(this, tr("提示"), tr("当前已是最后一张图片"));
        return;
    }
}

void ResultVisualization::on_zoomInButton_clicked() {
    changeZoom(true);
}

void ResultVisualization::on_zoomOutButton_clicked() {
    changeZoom(false);
}

void ResultVisualization::changeZoom(bool zoomIn) {
    if (sourcePixmap.isNull())
        return;
    int targetIndex = -1;
    double targetScale = 0.0;
    for (int i = 1; i < ui->zoomComboBox->count(); ++i) {
        QString text = ui->zoomComboBox->itemText(i);
        text.remove('%');
        const double scale = text.toDouble() / 100.0;
        const bool beyondCurrent = zoomIn ? scale > zoomScale + 1e-6
                                          : scale < zoomScale - 1e-6;
        const bool closer = targetIndex < 0 || (zoomIn ? scale < targetScale
                                                      : scale > targetScale);
        if (beyondCurrent && closer) {
            targetIndex = i;
            targetScale = scale;
        }
    }
    if (targetIndex >= 0) {
        fitToWindow = false;
        zoomScale = targetScale;
        updateImageDisplay();
    }
}

void ResultVisualization::refreshThumbnailList() {
    if (imageList.empty()) {
        return;
    }
    ui->thumbnailListWidget->clear();
    for (int i = 0;i<imageList.size();i++) {
        QPixmap pixmap = QPixmap(imageList[i]);
        auto marginPix = pixmap.scaled(116,82,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        QIcon icon = QIcon(marginPix);
        QListWidgetItem *newItem = new QListWidgetItem();
        newItem->setIcon(icon);
        QFileInfo fileInfo = QFileInfo(imageList[i]);
        newItem->setText(fileInfo.fileName());
        newItem->setData(Qt::UserRole, imageList[i]);
        newItem->setToolTip(imageList[i]);
        ui->thumbnailListWidget->addItem(newItem);
    }
}


void ResultVisualization::on_fitToWindowButton_clicked() {
    if (sourcePixmap.isNull())
        return;
    fitToWindow = true;
    updateImageDisplay();
}

void ResultVisualization::on_fullScreenButton_clicked() {
    if (sourcePixmap.isNull() || fullScreenDialog)
        return;

    fullScreenDialog = new QDialog(this, Qt::Window);
    fullScreenDialog->setObjectName(QStringLiteral("imageFullScreenDialog"));
    fullScreenDialog->setWindowTitle(tr("图片全屏预览"));
    fullScreenDialog->setStyleSheet(QStringLiteral("background: #f4f7fb;"));
    auto *layout = new QVBoxLayout(fullScreenDialog);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *exitButton = new QPushButton(tr("退出全屏 (Esc)"), fullScreenDialog);
    exitButton->setAutoDefault(false);
    layout->addWidget(exitButton, 0, Qt::AlignRight);
    connect(exitButton, &QPushButton::clicked, fullScreenDialog, &QDialog::reject);

    ui->centerPanelLayout->removeWidget(ui->imageScrollArea);
    layout->addWidget(ui->imageScrollArea, 1);
    ui->imageScrollArea->show();

    connect(fullScreenDialog, &QDialog::finished, this, [this] {
        QDialog *dialog = fullScreenDialog;
        fullScreenDialog = nullptr;
        // 从全屏窗口取回同一个滚动区域，保留图片和手动缩放比例。
        dialog->layout()->removeWidget(ui->imageScrollArea);
        ui->imageScrollArea->setParent(ui->centerPanelLayout->parentWidget());
        ui->centerPanelLayout->insertWidget(1, ui->imageScrollArea, 1);
        ui->imageScrollArea->show();
        ui->centerPanelLayout->activate();
        updateImageDisplay();
        dialog->deleteLater();
        ui->fullScreenButton->setFocus();
    });
    fullScreenDialog->showFullScreen();
    ui->imageScrollArea->setFocus();
}

void ResultVisualization::on_previousThumbnailsButton_clicked() {
    int index = ui->thumbnailListWidget->currentRow();
    if (index<=0) {
        QMessageBox::warning(this,tr("警告"),tr("这是第一张图片"));
        return;
    }
    int previousIndex = index - 1;
    ui->thumbnailListWidget->setCurrentRow(previousIndex);
}

void ResultVisualization::on_nextThumbnailsButton_clicked() {
    int index = ui->thumbnailListWidget->currentRow();
    if (index>=ui->thumbnailListWidget->count()-1) {
        QMessageBox::warning(this,tr("警告"),tr("这是最后一张图片"));
        return;
    }
    int nextIndex = index + 1;
    ui->thumbnailListWidget->setCurrentRow(nextIndex);
}

void ResultVisualization::on_zoomComboBox_currentIndexChanged(int index) {
    if (index < 0 || index >= ui->zoomComboBox->count() || sourcePixmap.isNull())
        return;
    if (index == 0) {
        on_fitToWindowButton_clicked();
        return;
    }
    QString text = ui->zoomComboBox->itemText(index);
    text.remove('%');
    const double scale = text.toDouble() / 100.0;
    if (!std::isfinite(scale) || scale <= 0.0)
        return;
    fitToWindow = false;
    zoomScale = scale;
    updateImageDisplay();
}

void ResultVisualization::on_imageListWidget_currentRowChanged(int row) {
    auto* item = ui->imageListWidget->item(row);
    if (!item)
        return;

    const QString imagePath = item->data(Qt::UserRole).toString();
    const QPixmap pixmap(imagePath);
    if (pixmap.isNull())
        return;

    setImage(pixmap);  // 切换图片时自动适应窗口。
    ui->imageScrollArea->horizontalScrollBar()->setValue(0);
    ui->imageScrollArea->verticalScrollBar()->setValue(0);

    const QFileInfo fileInfo(imagePath);
    const QSize size = pixmap.size();
    const double fileSize = fileInfo.size() / 1024.0 / 1024.0;

    ui->imageNameValue->setText(fileInfo.fileName());
    ui->imageSizeValue->setText(
        QString("%1 × %2").arg(size.width()).arg(size.height())
    );
    ui->fileSizeValue->setText(
        fileSize >= 1.0
            ? QString("%1 MB").arg(fileSize, 0, 'f', 2)
            : QString("%1 KB").arg(fileSize * 1024, 0, 'f', 2)
    );
    ui->captureTimeValue->setText(
        fileInfo.birthTime().toString("yyyy-MM-dd hh:mm:ss")
    );

    const QSignalBlocker blocker(ui->thumbnailListWidget);
    if (auto* thumbnail = ui->thumbnailListWidget->item(row)) {
        ui->thumbnailListWidget->setCurrentRow(row);
        ui->thumbnailListWidget->scrollToItem(thumbnail);
    }
    ui->currentIndexValue->setText(QString::number(row + 1)+"/"+QString::number(ui->imageListWidget->count()));
}

void ResultVisualization::on_thumbnailListWidget_currentRowChanged(int row) {
    if (row < 0 || row >= ui->imageListWidget->count())
        return;
    ui->imageListWidget->setCurrentRow(row);
}

void ResultVisualization::on_confidenceSlider_valueChanged(int value) {
    QSignalBlocker blocker(ui->confidenceSpinBox);
    ui->confidenceSpinBox->setValue(value/100.0);
}

void ResultVisualization::on_confidenceSpinBox_valueChanged(double value) {
    QSignalBlocker blocker(ui->confidenceSlider);
    ui->confidenceSlider->setValue(static_cast<int>(value * 100.0));
}

void ResultVisualization::on_nmsSlider_valueChanged(int value) {
    QSignalBlocker blocker(ui->nmsSpinBox);
    ui->nmsSpinBox->setValue(value/100.0);
}

void ResultVisualization::on_nmsSpinBox_valueChanged(double value) {
    QSignalBlocker blocker(ui->nmsSlider);
    ui->nmsSlider->setValue(static_cast<int>(value * 100.0));
}

