#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>

namespace {

speed_t baudToSpeed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:
            throw std::invalid_argument("Nicht unterstuetzte Baudrate");
    }
}

std::string nowTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::tm localTm{};
    localtime_r(&nowTime, &localTm);

    std::ostringstream oss;
    oss << std::put_time(&localTm, "%Y-%m-%d %H:%M:%S")
        << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

class Logger {
public:
    Logger() = default;

    explicit Logger(const std::string& path) {
        if (!path.empty()) {
            file_.open(path, std::ios::app);
            if (!file_) {
                throw std::runtime_error("Logdatei konnte nicht geoeffnet werden: " + path);
            }
            enabled_ = true;
        }
    }

    void log(const std::string& direction, const std::string& line) {
        if (!enabled_) {
            return;
        }
        file_ << "[" << nowTimestamp() << "] " << direction << " " << line << "\n";
        file_.flush();
    }

private:
    bool enabled_ = false;
    std::ofstream file_;
};

class SerialPort {
public:
    SerialPort() = default;

    ~SerialPort() {
        closePort();
    }

    void openPort(const std::string& device, int baud) {
        closePort();

        fd_ = open(device.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (fd_ < 0) {
            throw std::runtime_error("Serieller Port konnte nicht geoeffnet werden: " + device);
        }

        termios tty{};
        if (tcgetattr(fd_, &tty) != 0) {
            closePort();
            throw std::runtime_error("tcgetattr fehlgeschlagen");
        }

        cfmakeraw(&tty);

        speed_t speed = baudToSpeed(baud);
        if (cfsetispeed(&tty, speed) != 0 || cfsetospeed(&tty, speed) != 0) {
            closePort();
            throw std::runtime_error("Baudrate konnte nicht gesetzt werden");
        }

        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_cflag &= ~PARENB;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8;

#ifdef CRTSCTS
        tty.c_cflag &= ~CRTSCTS;
#endif

        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        tty.c_oflag = 0;
        tty.c_lflag = 0;

        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;  // 0.1 Sekunden pro read()

        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            closePort();
            throw std::runtime_error("tcsetattr fehlgeschlagen");
        }

        tcflush(fd_, TCIOFLUSH);
    }

    void closePort() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    void writeLine(const std::string& line) {
        std::string payload = line;
        if (payload.empty() || payload.back() != '\n') {
            payload.push_back('\n');
        }
        writeAll(payload.data(), payload.size());
    }

    bool readLine(std::string& outLine, std::chrono::milliseconds timeout) {
        outLine.clear();
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            char ch = '\0';
            ssize_t n = read(fd_, &ch, 1);

            if (n < 0) {
                throw std::runtime_error("Lesefehler auf serieller Schnittstelle");
            }

            if (n == 0) {
                continue;
            }

            if (ch == '\r') {
                continue;
            }

            if (ch == '\n') {
                return true;
            }

            outLine.push_back(ch);
        }

        return false;
    }

private:
    void writeAll(const char* data, size_t len) {
        size_t writtenTotal = 0;
        while (writtenTotal < len) {
            ssize_t n = write(fd_, data + writtenTotal, len - writtenTotal);
            if (n < 0) {
                throw std::runtime_error("Schreibfehler auf serieller Schnittstelle");
            }
            writtenTotal += static_cast<size_t>(n);
        }
        tcdrain(fd_);
    }

    int fd_ = -1;
};

void printUsage(const char* programName) {
    std::cerr << "Verwendung:\n";
    std::cerr << "  " << programName << " <serieller-port> [baudrate] [logdatei]\n\n";
    std::cerr << "Beispiel:\n";
    std::cerr << "  " << programName << " /dev/cu.usbmodem1101 115200\n";
    std::cerr << "  " << programName << " /dev/cu.usbmodem1101 115200 sitzung.log\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 4) {
        printUsage(argv[0]);
        return 1;
    }

    std::string device = argv[1];
    int baud = 115200;
    if (argc >= 3) {
        baud = std::stoi(argv[2]);
    }

    std::string logFile;
    if (argc == 4) {
        logFile = argv[3];
    }

    try {
        Logger logger(logFile);
        SerialPort port;

        port.openPort(device, baud);

        std::cout << "Port geoeffnet: " << device << " @ " << baud << " Baud\n";
        std::cout << "Warte kurz auf den Arduino-Reset...\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // Erste Zeile lesen, falls der Arduino z. B. "OK ready" sendet
        std::string startupLine;
        if (port.readLine(startupLine, std::chrono::milliseconds(1500))) {
            std::cout << "Arduino: " << startupLine << "\n";
            logger.log("RX", startupLine);
        }

        while (true) {
            std::cout << "expr> " << std::flush;

            std::string input;
            if (!std::getline(std::cin, input)) {
                std::cout << "\nProgramm beendet.\n";
                break;
            }

            if (input == "exit" || input == "quit") {
                std::cout << "Programm beendet.\n";
                break;
            }

            if (input.empty()) {
                continue;
            }

            port.writeLine(input);
            logger.log("TX", input);

            std::string response;
            bool gotResponse = port.readLine(response, std::chrono::milliseconds(2000));

            if (!gotResponse) {
                std::cerr << "Keine Antwort innerhalb des Timeouts.\n";
                continue;
            }

            logger.log("RX", response);

            if (response.rfind("OK ", 0) == 0) {
                std::cout << "Ergebnis: " << response.substr(3) << "\n";
            } else if (response.rfind("ERR ", 0) == 0) {
                std::cout << "Fehler: " << response.substr(4) << "\n";
            } else {
                std::cout << "Unerwartete Antwort: " << response << "\n";
            }
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fehler: " << ex.what() << "\n";
        return 1;
    }
}