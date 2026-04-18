#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <cerrno>
  #include <cstring>
  #include <fcntl.h>
  #include <termios.h>
  #include <unistd.h>
#endif

struct LogEntry {
    std::string timestamp;
    std::string direction;
    std::string message;
};

std::string makeTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t timeValue = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localTime = {};
    localtime_s(&localTime, &timeValue);
#else
    localTime = *std::localtime(&timeValue);
#endif

    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void printHelp() {
    std::cout << "\nVerfuegbare Befehle:\n"
              << "  <a> <op> <b>      Ausdruck an den Mikrocontroller senden, z. B. 34 * 72\n"
              << "  :save <datei>     Gesamtes Protokoll als Textdatei speichern\n"
              << "  :help             Hilfe anzeigen\n"
              << "  :quit             Programm beenden\n\n";
}

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort() { close(); }

    void open(const std::string& portName, int baudRate) {
#ifdef _WIN32
        std::string fullPortName = portName;
        if (portName.rfind("COM", 0) == 0 && portName.size() > 4) {
            fullPortName = "\\\\.\\" + portName;
        }

        handle_ = CreateFileA(fullPortName.c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              nullptr);

        if (handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Serielle Schnittstelle konnte nicht geoeffnet werden.");
        }

        DCB dcb = {0};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(handle_, &dcb)) {
            close();
            throw std::runtime_error("Serielle Schnittstelle konnte nicht konfiguriert werden.");
        }

        dcb.BaudRate = static_cast<DWORD>(baudRate);
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fTXContinueOnXoff = TRUE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;

        if (!SetCommState(handle_, &dcb)) {
            close();
            throw std::runtime_error("Fehler beim Setzen der seriellen Parameter.");
        }

        COMMTIMEOUTS timeouts = {0};
        timeouts.ReadIntervalTimeout = 20;
        timeouts.ReadTotalTimeoutConstant = 20;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 100;
        timeouts.WriteTotalTimeoutMultiplier = 0;

        if (!SetCommTimeouts(handle_, &timeouts)) {
            close();
            throw std::runtime_error("Serielle Timeouts konnten nicht gesetzt werden.");
        }
#else
        fd_ = ::open(portName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (fd_ < 0) {
            throw std::runtime_error("Serielle Schnittstelle konnte nicht geoeffnet werden: " + std::string(std::strerror(errno)));
        }

        termios tty{};
        if (tcgetattr(fd_, &tty) != 0) {
            close();
            throw std::runtime_error("Fehler beim Lesen der seriellen Konfiguration.");
        }

        speed_t speed = mapBaudRate(baudRate);
        cfsetospeed(&tty, speed);
        cfsetispeed(&tty, speed);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
        tty.c_iflag &= ~IGNBRK;
        tty.c_lflag = 0;
        tty.c_oflag = 0;
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_cflag &= ~(PARENB | PARODD);
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;

        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            close();
            throw std::runtime_error("Fehler beim Setzen der seriellen Konfiguration.");
        }
#endif
    }

    void close() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

    void writeLine(const std::string& text) {
        std::string payload = text + "\n";
#ifdef _WIN32
        DWORD bytesWritten = 0;
        if (!WriteFile(handle_, payload.c_str(), static_cast<DWORD>(payload.size()), &bytesWritten, nullptr) ||
            bytesWritten != payload.size()) {
            throw std::runtime_error("Senden ueber die serielle Schnittstelle fehlgeschlagen.");
        }
#else
        ssize_t result = ::write(fd_, payload.c_str(), payload.size());
        if (result < 0 || static_cast<size_t>(result) != payload.size()) {
            throw std::runtime_error("Senden ueber die serielle Schnittstelle fehlgeschlagen.");
        }
#endif
    }

    bool readLine(std::string& line, int timeoutMs) {
        line.clear();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() < deadline) {
            char c = '\0';
            int readResult = readOneByte(c);

            if (readResult < 0) {
                throw std::runtime_error("Lesen ueber die serielle Schnittstelle fehlgeschlagen.");
            }

            if (readResult == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                if (!line.empty()) {
                    return true;
                }
                continue;
            }

            line.push_back(c);
        }

        return false;
    }

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;

    static speed_t mapBaudRate(int baudRate) {
        switch (baudRate) {
            case 9600:   return B9600;
            case 19200:  return B19200;
            case 38400:  return B38400;
            case 57600:  return B57600;
            case 115200: return B115200;
            default:
                throw std::runtime_error("Nicht unterstuetzte Baudrate fuer POSIX-Systeme.");
        }
    }
#endif

    int readOneByte(char& c) {
#ifdef _WIN32
        DWORD bytesRead = 0;
        if (!ReadFile(handle_, &c, 1, &bytesRead, nullptr)) {
            return -1;
        }
        return static_cast<int>(bytesRead);
#else
        ssize_t result = ::read(fd_, &c, 1);
        if (result < 0) {
            return -1;
        }
        return static_cast<int>(result);
#endif
    }
};

void addLog(std::vector<LogEntry>& log, const std::string& direction, const std::string& message) {
    log.push_back({makeTimestamp(), direction, message});
}

bool saveLogToFile(const std::vector<LogEntry>& log, const std::string& fileName) {
    std::ofstream output(fileName);
    if (!output) {
        return false;
    }

    output << "Kommunikationsprotokoll PC <-> Mikrocontroller\n";
    output << "=========================================\n\n";

    for (const auto& entry : log) {
        output << '[' << entry.timestamp << "] " << entry.direction << ": " << entry.message << '\n';
    }

    return true;
}

int main(int argc, char* argv[]) {
    std::string portName;
    int baudRate = 115200;
    int responseTimeoutMs = 3000;

    if (argc >= 2) {
        portName = argv[1];
    }
    if (argc >= 3) {
        baudRate = std::stoi(argv[2]);
    }

    if (portName.empty()) {
        std::cout << "Serielle Schnittstelle eingeben (z. B. COM3 oder /dev/cu.usbmodemXXXX): ";
        std::getline(std::cin, portName);
    }

    std::vector<LogEntry> log;
    SerialPort serial;

    try {
        std::cout << "Verbinde mit " << portName << " bei " << baudRate << " Baud ...\n";
        serial.open(portName, baudRate);

        std::cout << "Warte auf Startmeldung des Arduino ...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        std::string startupLine;
        while (serial.readLine(startupLine, 300)) {
            addLog(log, "uC -> PC", startupLine);
            std::cout << "[uC] " << startupLine << '\n';
        }

        std::cout << "Verbindung hergestellt.\n";
        printHelp();

        while (true) {
            std::cout << "> ";
            std::string input;
            if (!std::getline(std::cin, input)) {
                break;
            }

            if (input.empty()) {
                continue;
            }

            if (input == ":quit") {
                break;
            }

            if (input == ":help") {
                printHelp();
                continue;
            }

            if (input.rfind(":save ", 0) == 0) {
                std::string fileName = input.substr(6);
                if (fileName.empty()) {
                    std::cout << "Bitte Dateinamen angeben.\n";
                    continue;
                }

                if (saveLogToFile(log, fileName)) {
                    std::cout << "Protokoll gespeichert: " << fileName << '\n';
                } else {
                    std::cout << "Fehler beim Speichern der Datei.\n";
                }
                continue;
            }

            serial.writeLine(input);
            addLog(log, "PC -> uC", input);
            std::cout << "[PC] " << input << '\n';

            std::string response;
            if (serial.readLine(response, responseTimeoutMs)) {
                addLog(log, "uC -> PC", response);
                std::cout << "[uC] " << response << '\n';
            } else {
                addLog(log, "uC -> PC", "TIMEOUT");
                std::cout << "[uC] Keine Antwort innerhalb von " << responseTimeoutMs << " ms.\n";
            }
        }

        std::cout << "Programm beendet.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fehler: " << ex.what() << '\n';
        return 1;
    }
}
