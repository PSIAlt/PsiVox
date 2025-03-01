#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QAudioInput>
#include <QAudioFormat>
#include <QBuffer>
#include <QByteArray>
#include <QFuture>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <vector>
#include <cstdint>
#include "whisper.h"  // Заголовочный файл из whisper.cpp

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr)
        : QMainWindow(parent),
          audioInput(nullptr),
          recordingBuffer(new QBuffer(this)),
          isRecording(false),
          whisperCtx(nullptr)
    {
        // Создаем UI: кнопку и поле для вывода транскрипции
        QWidget* centralWidget = new QWidget(this);
        QVBoxLayout* layout = new QVBoxLayout(centralWidget);
        recordButton = new QPushButton("Начать запись", this);
        transcriptEdit = new QTextEdit(this);
        transcriptEdit->setReadOnly(true);
        layout->addWidget(recordButton);
        layout->addWidget(transcriptEdit);
        setCentralWidget(centralWidget);

        // Загружаем модель Whisper (файл модели должен быть доступен)
        // Проверьте, что ggml-large-v3-turbo-q5_0.bin или другая выбранная модель действительно находится в рабочей директории.
        whisperCtx = whisper_init_from_file("external/whisper.cpp/models/ggml-large-v3-turbo-q5_0.bin");
        if (!whisperCtx) {
            transcriptEdit->setPlainText("Ошибка загрузки модели Whisper.");
        }

        // Связываем нажатие кнопки с обработчиком
        connect(recordButton, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    }

    ~MainWindow() {
        if (whisperCtx) {
            whisper_free(whisperCtx);
        }
    }

private slots:
    void toggleRecording() {
        if (!isRecording) {
            // Начало записи
            transcriptEdit->clear();
            recordButton->setText("Остановить запись");
            startRecording();
        } else {
            // Остановка записи и запуск транскрипции
            recordButton->setText("Начать запись");
            stopRecording();
        }
        isRecording = !isRecording;
    }

    // Слот, вызываемый по завершении фоновой транскрипции
    void transcriptionFinished() {
        QString result = futureWatcher.result();
        transcriptEdit->setPlainText(result);
    }

private:
    void startRecording() {
        // Настраиваем формат аудио: 16 кГц, 16 бит, моно
        QAudioFormat format;
        format.setSampleRate(16000);
        format.setChannelCount(1);
        format.setSampleSize(16);
        format.setSampleType(QAudioFormat::SignedInt);
        format.setByteOrder(QAudioFormat::LittleEndian);
        format.setCodec("audio/pcm");

        QAudioDeviceInfo info = QAudioDeviceInfo::defaultInputDevice();
        if (!info.isFormatSupported(format)) {
            transcriptEdit->setPlainText("Запрашиваемый формат не поддерживается, используется ближайший доступный.");
            format = info.nearestFormat(format);
        }

        audioInput = new QAudioInput(format, this);
        recordingBuffer->open(QIODevice::WriteOnly | QIODevice::Truncate);
        audioInput->start(recordingBuffer);
    }

    void stopRecording() {
        if (audioInput) {
            audioInput->stop();
            recordingBuffer->close();

            QByteArray audioData = recordingBuffer->data();
            // Преобразуем данные (PCM 16-бит, little-endian) в вектор float (нормализованные значения от -1 до 1)
            std::vector<float> audioFloat;
            const int16_t* samples = reinterpret_cast<const int16_t*>(audioData.constData());
            int sampleCount = audioData.size() / sizeof(int16_t);
            audioFloat.resize(sampleCount);
            for (int i = 0; i < sampleCount; ++i) {
                audioFloat[i] = samples[i] / 32768.0f;
            }

            // Фоновый запуск распознавания с помощью QtConcurrent
            QFuture<QString> future = QtConcurrent::run([this, audioFloat]() -> QString {
                // Используем мьютекс для защиты контекста от одновременного доступа
                QMutexLocker locker(&whisperMutex);
                
                // Параметры для whisper (используем режим WHISPER_SAMPLING_GREEDY)
                whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
                int ret = whisper_full(whisperCtx, params, audioFloat.data(), audioFloat.size());
                if (ret != 0) {
                    return QString("Ошибка распознавания");
                }
                QString full_text;
                int n_segments = whisper_full_n_segments(whisperCtx);
                for (int i = 0; i < n_segments; ++i) {
                    full_text += whisper_full_get_segment_text(whisperCtx, i);
                }
                return full_text;
            });

            futureWatcher.setFuture(future);
            connect(&futureWatcher, &QFutureWatcher<QString>::finished, this, &MainWindow::transcriptionFinished);

            // Готовимся к следующей записи: удаляем старый объект аудио
            audioInput->deleteLater();
            audioInput = nullptr;
            recordingBuffer->reset();
        }
    }

    QPushButton* recordButton;
    QTextEdit* transcriptEdit;
    QAudioInput* audioInput;
    QBuffer* recordingBuffer;
    bool isRecording;
    whisper_context* whisperCtx;
    QFutureWatcher<QString> futureWatcher;
    QMutex whisperMutex;  // Мьютекс для защиты доступа к whisperCtx
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow w;
    w.setWindowTitle("Whisper Transcriber");
    w.resize(400, 300);
    w.show();
    return app.exec();
}
