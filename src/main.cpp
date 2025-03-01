#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QAudioInput>
#include <QAudioFormat>
#include <QIODevice>
#include <QTimer>
#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QFuture>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QDateTime>
#include <vector>
#include <cstdint>
#include "whisper.h"  // Заголовок из whisper.cpp

// Константы для аудио
constexpr int SAMPLE_RATE = 16000;
constexpr int BYTES_PER_SAMPLE = 2;  // 16-bit
constexpr int CHUNK_SECONDS = 5;
constexpr int CHUNK_BYTES = SAMPLE_RATE * BYTES_PER_SAMPLE * CHUNK_SECONDS;  // 5 секунд
constexpr int MIN_BYTES = SAMPLE_RATE * BYTES_PER_SAMPLE;  // минимум 1 секунда = 32000 байт

//
// Класс для накопления аудио в памяти
//
class AudioBuffer : public QIODevice {
    Q_OBJECT
public:
    AudioBuffer(QObject* parent = nullptr) : QIODevice(parent) { }
    
    qint64 readData(char *data, qint64 maxSize) override {
        Q_UNUSED(data)
        Q_UNUSED(maxSize)
        return 0;
    }
    
    qint64 writeData(const char *data, qint64 len) override {
        QMutexLocker locker(&mutex);
        buffer.append(data, len);
        return len;
    }
    
    // Извлекает из буфера первые bytesToTake байт (если их достаточно)
    QByteArray takeChunk(int bytesToTake) {
        QMutexLocker locker(&mutex);
        if(buffer.size() < bytesToTake)
            return QByteArray();
        QByteArray chunk = buffer.left(bytesToTake);
        buffer.remove(0, bytesToTake);
        return chunk;
    }
    
    // Извлекает все накопленные данные (например, при остановке записи)
    QByteArray takeAllData() {
        QMutexLocker locker(&mutex);
        QByteArray all = buffer;
        buffer.clear();
        return all;
    }
    
    int available() const {
        QMutexLocker locker(&mutex);
        return buffer.size();
    }
    
private:
    QByteArray buffer;
    mutable QMutex mutex;
};

//
// Основное окно приложения
//
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr)
        : QMainWindow(parent),
          audioInput(nullptr),
          audioBuffer(nullptr),
          chunkTimer(new QTimer(this)),
          isRecording(false),
          transcriptionRunning(false),
          whisperCtx(nullptr),
          chunkCounter(0)
    {
        // Создаем UI
        QWidget* centralWidget = new QWidget(this);
        QVBoxLayout* layout = new QVBoxLayout(centralWidget);
        recordButton = new QPushButton("Начать запись", this);
        transcriptEdit = new QTextEdit(this);
        transcriptEdit->setReadOnly(true);
        layout->addWidget(recordButton);
        layout->addWidget(transcriptEdit);
        setCentralWidget(centralWidget);
        
        // Загружаем модель Whisper (убедитесь, что файл находится в рабочей директории)
        whisperCtx = whisper_init_from_file("external/whisper.cpp/models/ggml-large-v3-turbo-q5_0.bin");
        if (!whisperCtx) {
            transcriptEdit->append("Ошибка загрузки модели Whisper.");
        }
        
        // Связываем кнопку
        connect(recordButton, &QPushButton::clicked, this, &MainWindow::toggleRecording);
        // Таймер для разбиения аудио на куски по 5 секунд
        chunkTimer->setInterval(CHUNK_SECONDS * 1000);
        connect(chunkTimer, &QTimer::timeout, this, &MainWindow::onChunkTimerTimeout);
        
        // Связь для завершения фоновой транскрипции
        connect(&futureWatcher, &QFutureWatcher<QString>::finished, this, &MainWindow::transcriptionFinished);
    }
    
    ~MainWindow() {
        if (whisperCtx) {
            whisper_free(whisperCtx);
        }
    }
    
private slots:
    void toggleRecording() {
        if (!isRecording) {
            transcriptEdit->append("=== Запись началась: " + QDateTime::currentDateTime().toString() + " ===");
            recordButton->setText("Остановить запись");
            startRecording();
        } else {
            recordButton->setText("Начать запись");
            stopRecording();
            transcriptEdit->append("=== Запись остановлена: " + QDateTime::currentDateTime().toString() + " ===");
        }
        isRecording = !isRecording;
    }
    
    // Извлечение куска из аудиобуфера каждые 5 секунд
    void onChunkTimerTimeout() {
        if (!audioBuffer)
            return;
        // Если накоплено хотя бы 5 секунд данных, извлекаем ровно CHUNK_BYTES
        if (audioBuffer->available() >= CHUNK_BYTES) {
            QByteArray chunk = audioBuffer->takeChunk(CHUNK_BYTES);
            enqueueChunk(chunk);
        }
    }
    
    // Обработка завершения транскрипции одного куска
    void transcriptionFinished() {
        QString result = futureWatcher.result();
        transcriptEdit->append(result);
        {
            QMutexLocker locker(&queueMutex);
            transcriptionRunning = false;
        }
        processNextChunk();
    }
    
private:
    // Начало записи
    void startRecording() {
        QAudioFormat format;
        format.setSampleRate(SAMPLE_RATE);
        format.setChannelCount(1);
        format.setSampleSize(16);
        format.setSampleType(QAudioFormat::SignedInt);
        format.setByteOrder(QAudioFormat::LittleEndian);
        format.setCodec("audio/pcm");
        
        QAudioDeviceInfo info = QAudioDeviceInfo::defaultInputDevice();
        if (!info.isFormatSupported(format)) {
            transcriptEdit->append("Запрашиваемый формат не поддерживается, используется ближайший доступный.");
            format = info.nearestFormat(format);
        }
        
        audioBuffer = new AudioBuffer(this);
        audioBuffer->open(QIODevice::WriteOnly | QIODevice::Append);
        
        audioInput = new QAudioInput(format, this);
        audioInput->start(audioBuffer);
        
        chunkTimer->start();
    }
    
    // Остановка записи
    void stopRecording() {
        if (audioInput) {
            audioInput->stop();
            chunkTimer->stop();
            QByteArray leftover = audioBuffer->takeAllData();
            if (!leftover.isEmpty()) {
                if (leftover.size() < MIN_BYTES) {
                    leftover.append(QByteArray(MIN_BYTES - leftover.size(), '\0'));
                }
                enqueueChunk(leftover);
            }
            audioBuffer->close();
            audioBuffer->deleteLater();
            audioBuffer = nullptr;
            audioInput->deleteLater();
            audioInput = nullptr;
        }
    }
    
    // Добавляет кусок аудио в очередь для транскрипции
    void enqueueChunk(const QByteArray& chunk) {
        bool shouldProcess = false;
        {
            QMutexLocker locker(&queueMutex);
            transcriptionQueue.enqueue(chunk);
            if (!transcriptionRunning)
                shouldProcess = true;
        }
        if (shouldProcess)
            processNextChunk();
    }
    
    // Запускает обработку следующего куска из очереди
    void processNextChunk() {
        QMutexLocker locker(&queueMutex);
        if (transcriptionQueue.isEmpty() || transcriptionRunning)
            return;
        QByteArray chunk = transcriptionQueue.dequeue();
        transcriptionRunning = true;
        int currentChunk = ++chunkCounter;
        QFuture<QString> future = QtConcurrent::run([this, chunk, currentChunk]() -> QString {
            // Если данных меньше MIN_BYTES, дополняем нулями
            QByteArray procChunk = chunk;
            if (procChunk.size() < MIN_BYTES) {
                procChunk.append(QByteArray(MIN_BYTES - procChunk.size(), '\0'));
            }
            int sampleCount = procChunk.size() / BYTES_PER_SAMPLE;
            std::vector<float> audioFloat(sampleCount);
            const int16_t* samples = reinterpret_cast<const int16_t*>(procChunk.constData());
            for (int i = 0; i < sampleCount; ++i) {
                audioFloat[i] = samples[i] / 32768.0f;
            }
            
            QElapsedTimer timer;
            timer.start();
            
            QString resultText;
            {
                QMutexLocker locker(&whisperMutex);
                whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
                params.translate = false;
                params.language = nullptr;

                int lang_id = whisper_full_lang_id(whisperCtx);
                const char* detectedLanguage = whisper_lang_str(lang_id);
                qDebug() << "Detected language:" << QString::fromUtf8(detectedLanguage);

                int ret = whisper_full(whisperCtx, params, audioFloat.data(), audioFloat.size());
                if (ret != 0) {
                    return QString("Chunk %1: Ошибка распознавания").arg(currentChunk);
                }
                int n_segments = whisper_full_n_segments(whisperCtx);
                for (int i = 0; i < n_segments; ++i) {
                    // Используем fromUtf8 для преобразования текста
                    resultText += QString::fromUtf8(whisper_full_get_segment_text(whisperCtx, i));
                }
            }
            
            qint64 elapsed = timer.elapsed();
            double chunkDurationSec = procChunk.size() / static_cast<double>(SAMPLE_RATE * BYTES_PER_SAMPLE);
            double transcriptionTimeSec = elapsed / 1000.0;
            double speedFactor = chunkDurationSec / transcriptionTimeSec;
            
            // Используем Qt-плейсхолдеры (%1, %2, …)
            // QString logMsg = QString("Chunk %1 (%2 сек): транскрипция заняла %3 мс (%4x реального времени)\n%5")
            //                     .arg(currentChunk)
            //                     .arg(chunkDurationSec, 0, 'f', 2)
            //                     .arg(elapsed)
            //                     .arg(speedFactor, 0, 'f', 2)
            //                     .arg(resultText);
            // return logMsg;
            return resultText;
        });
        futureWatcher.setFuture(future);
    }
    
    // UI
    QPushButton* recordButton;
    QTextEdit* transcriptEdit;
    
    // Аудио
    QAudioInput* audioInput;
    AudioBuffer* audioBuffer;
    QTimer* chunkTimer;
    
    bool isRecording;
    
    // Очередь для транскрипции
    QQueue<QByteArray> transcriptionQueue;
    QMutex queueMutex;
    bool transcriptionRunning;
    
    // Whisper
    whisper_context* whisperCtx;
    QMutex whisperMutex;
    
    int chunkCounter;
    
    QFutureWatcher<QString> futureWatcher;
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow w;
    w.setWindowTitle("Whisper Transcriber");
    w.resize(600, 400);
    w.show();
    return app.exec();
}
