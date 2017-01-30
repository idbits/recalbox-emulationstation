/*
 * File:   RecalboxSystem.cpp
 * Author: digitallumberjack
 * 
 * Created on 29 novembre 2014, 03:15
 */

#include "RecalboxSystem.h"
#include <sys/statvfs.h>
#include "Settings.h"
#include "Log.h"
#include "HttpReq.h"

#include "AudioManager.h"
#include "VolumeControl.h"

#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <boost/algorithm/string/replace.hpp>


RecalboxSystem::RecalboxSystem() {
}

RecalboxSystem *RecalboxSystem::instance = NULL;


RecalboxSystem *RecalboxSystem::getInstance() {
    if (RecalboxSystem::instance == NULL) {
        RecalboxSystem::instance = new RecalboxSystem();
    }
    return RecalboxSystem::instance;
}

unsigned long RecalboxSystem::getFreeSpaceGB(std::string mountpoint) {
    struct statvfs fiData;
    const char *fnPath = mountpoint.c_str();
    int free = 0;
    if ((statvfs(fnPath, &fiData)) >= 0) {
        free = (fiData.f_bfree * fiData.f_bsize) / (1024 * 1024 * 1024);
    }
    return free;
}

std::string RecalboxSystem::getFreeSpaceInfo() {
    struct statvfs fiData;
    std::string sharePart = Settings::getInstance()->getString("SharePartition");
    if (sharePart.size() > 0) {
        const char *fnPath = sharePart.c_str();
        if ((statvfs(fnPath, &fiData)) < 0) {
            return "";
        } else {
            unsigned long total = (fiData.f_blocks * (fiData.f_bsize / 1024)) / (1024L * 1024L);
            unsigned long free = (fiData.f_bfree * (fiData.f_bsize / 1024)) / (1024L * 1024L);
            unsigned long used = total - free;
            unsigned long percent = 0;
            std::ostringstream oss;
            if (total != 0) {  //for small SD card ;) with share < 1GB
                percent = used * 100 / total;
                oss << used << "GB/" << total << "GB (" << percent << "%)";
            } else
                oss << "N/A";
            return oss.str();
        }
    } else {
        return "ERROR";
    }
}

bool RecalboxSystem::isFreeSpaceLimit() {
    std::string sharePart = Settings::getInstance()->getString("SharePartition");
    if (sharePart.size() > 0) {
        return getFreeSpaceGB(sharePart) < 2;
    } else {
        return "ERROR";
    }

}

std::string RecalboxSystem::getVersion() {
    std::string version = Settings::getInstance()->getString("VersionFile");
    if (version.size() > 0) {
        std::ifstream ifs(version);

        if (ifs.good()) {
            std::string contents;
            std::getline(ifs, contents);
            return contents;
        }
    }
    return "";
}


bool RecalboxSystem::updateLastChangelogFile() {
    std::ostringstream oss;
    oss << "cp  " << Settings::getInstance()->getString("Changelog").c_str() << " " <<
        Settings::getInstance()->getString("LastChangelog").c_str();
    if (system(oss.str().c_str())) {
        LOG(LogWarning) << "Error executing " << oss.str().c_str();
        return false;
    } else {
        LOG(LogError) << "Last version file updated";
        return true;
    }
}

std::string RecalboxSystem::getChangelog() {
    std::ostringstream oss;
    std::string last = Settings::getInstance()->getString("LastChangelog");
    std::ifstream f(last);
    if(!f.good()){
        std::ofstream outfile (last);
        outfile << " ";
        outfile.close();
    }
    oss << "diff --changed-group-format='%>' --unchanged-group-format='' " <<
        last.c_str() << " " <<
        Settings::getInstance()->getString("Changelog").c_str();

    FILE *pipe = popen(oss.str().c_str(), "r");
    char line[1024];

    if (pipe == NULL) {
        return "";
    }
    std::ostringstream res;
    while (fgets(line, 1024, pipe)) {
        res << line;
    }
    pclose(pipe);

    return res.str();

}

bool RecalboxSystem::setAudioOutputDevice(std::string device) {
    int commandValue = -1;
    int returnValue = false;

    if (device == "auto") {
        commandValue = 0;
    } else if (device == "jack") {
        commandValue = 1;
    } else if (device == "hdmi") {
        commandValue = 2;
    } else {
        LOG(LogWarning) << "Unable to find audio output device to use !";
    }

    if (commandValue != -1) {
        std::ostringstream oss;
        oss << "amixer cset numid=3 " << commandValue;
        std::string command = oss.str();
        LOG(LogInfo) << "Launching " << command;
        if (system(command.c_str())) {
            LOG(LogWarning) << "Error executing " << command;
            returnValue = false;
        } else {
            LOG(LogInfo) << "Audio output device set to : " << device;
            returnValue = true;
        }
    }
    return returnValue;
}


bool RecalboxSystem::setOverscan(bool enable) {

    std::ostringstream oss;
    oss << Settings::getInstance()->getString("RecalboxSettingScript") << " " << "overscan";
    if (enable) {
        oss << " " << "enable";
    } else {
        oss << " " << "disable";
    }
    std::string command = oss.str();
    LOG(LogInfo) << "Launching " << command;
    if (system(command.c_str())) {
        LOG(LogWarning) << "Error executing " << command;
        return false;
    } else {
        LOG(LogInfo) << "Overscan set to : " << enable;
        return true;
    }

}

bool RecalboxSystem::setOverclock(std::string mode) {
    if (mode != "") {
        std::ostringstream oss;
        oss << Settings::getInstance()->getString("RecalboxSettingScript") << " "
            << "overclock" << " " << mode;
        std::string command = oss.str();
        LOG(LogInfo) << "Launching " << command;
        if (system(command.c_str())) {
            LOG(LogWarning) << "Error executing " << command;
            return false;
        } else {
            LOG(LogInfo) << "Overclocking set to " << mode;
            return true;
        }
    }

    return false;
}


std::pair<std::string, int> RecalboxSystem::updateSystem() {
    std::string updatecommand = Settings::getInstance()->getString("UpdateCommand");
    FILE *pipe = popen(updatecommand.c_str(), "r");
    char line[1024];
    if (pipe == NULL) {
        return std::pair<std::string, int>(std::string("Cannot call update command"), -1);
    }
    if (fgets(line, 1024, pipe)) {
        strtok(line, "\n");
    }
    int exitCode = pclose(pipe) / 256;
    if (strlen(line) == 0) {
        return std::pair<std::string, int>(std::string("Cannot call update command"), exitCode);
    } else {
        return std::pair<std::string, int>(std::string(line), exitCode);
    }
}

bool RecalboxSystem::ping() {
    std::string updateserver = Settings::getInstance()->getString("UpdateServer");
    std::string s("ping -c 1 " + updateserver);
    int exitcode = system(s.c_str());
    return exitcode == 0;
}

bool RecalboxSystem::canUpdate() {
    std::ostringstream oss;
    oss << Settings::getInstance()->getString("RecalboxSettingScript") << " " << "canupdate";
    std::string command = oss.str();
    LOG(LogInfo) << "Launching " << command;
    if (system(command.c_str()) == 0) {
        LOG(LogInfo) << "Can update ";
        return true;
    } else {
        LOG(LogInfo) << "Cannot update ";
        return false;
    }
}

bool RecalboxSystem::launchKodi(Window *window) {

    LOG(LogInfo) << "Attempting to launch kodi...";


    AudioManager::getInstance()->deinit();
    VolumeControl::getInstance()->deinit();

    std::string commandline = InputManager::getInstance()->configureEmulators();
    std::string command = "configgen -system kodi -rom '' " + commandline;

    window->deinit();

    int exitCode = system(command.c_str());
    if (WIFEXITED(exitCode)) {
        exitCode = WEXITSTATUS(exitCode);
    }

    window->init();
    VolumeControl::getInstance()->init();
    AudioManager::getInstance()->resumeMusic();
    window->normalizeNextUpdate();

    // handle end of kodi
    switch (exitCode) {
        case 10: // reboot code
            reboot();
            return true;
            break;
        case 11: // shutdown code
            shutdown();
            return true;
            break;
    }

    return exitCode == 0;

}

bool RecalboxSystem::enableWifi(std::string ssid, std::string key) {
    std::ostringstream oss;
    boost::replace_all(ssid, "\"", "\\\"");
    boost::replace_all(key, "\"", "\\\"");
    oss << Settings::getInstance()->getString("RecalboxSettingScript") << " "
        << "wifi" << " "
        << "enable" << " \""
        << ssid << "\" \"" << key << "\"";
    std::string command = oss.str();
    LOG(LogInfo) << "Launching " << command;
    if (system(command.c_str()) == 0) {
        LOG(LogInfo) << "Wifi enabled ";
        return true;
    } else {
        LOG(LogInfo) << "Cannot enable wifi ";
        return false;
    }
}

bool RecalboxSystem::disableWifi() {
    std::ostringstream oss;
    oss << Settings::getInstance()->getString("RecalboxSettingScript") << " "
        << "wifi" << " "
        << "disable";
    std::string command = oss.str();
    LOG(LogInfo) << "Launching " << command;
    if (system(command.c_str()) == 0) {
        LOG(LogInfo) << "Wifi disabled ";
        return true;
    } else {
        LOG(LogInfo) << "Cannot disable wifi ";
        return false;
    }
}


bool RecalboxSystem::halt(bool reboot, bool fast) {
    SDL_Event *quit = new SDL_Event();
    if (fast)
        if (reboot)
            quit->type = SDL_FAST_QUIT | SDL_RB_REBOOT;
        else
            quit->type = SDL_FAST_QUIT | SDL_RB_SHUTDOWN;
    else if (reboot)
        quit->type = SDL_QUIT | SDL_RB_REBOOT;
    else
        quit->type = SDL_QUIT | SDL_RB_SHUTDOWN;
    SDL_PushEvent(quit);
    return 0;
}

bool RecalboxSystem::reboot() {
    return halt(true, false);
}

bool RecalboxSystem::fastReboot() {
    return halt(true, true);
}

bool RecalboxSystem::shutdown() {
    return halt(false, false);
}

bool RecalboxSystem::fastShutdown() {
    return halt(false, true);
}


std::string RecalboxSystem::getIpAdress() {
    struct ifaddrs *ifAddrStruct = NULL;
    struct ifaddrs *ifa = NULL;
    void *tmpAddrPtr = NULL;

    std::string result = "NOT CONNECTED";
    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) { // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr = &((struct sockaddr_in *) ifa->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer);
            if (std::string(ifa->ifa_name).find("eth") != std::string::npos ||
                std::string(ifa->ifa_name).find("wlan") != std::string::npos) {
                result = std::string(addressBuffer);
            }
        }
    }
    // Seeking for ipv6 if no IPV4
    if (result.compare("NOT CONNECTED") == 0) {
        for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) {
                continue;
            }
            if (ifa->ifa_addr->sa_family == AF_INET6) { // check it is IP6
                // is a valid IP6 Address
                tmpAddrPtr = &((struct sockaddr_in6 *) ifa->ifa_addr)->sin6_addr;
                char addressBuffer[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
                printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer);
                if (std::string(ifa->ifa_name).find("eth") != std::string::npos ||
                    std::string(ifa->ifa_name).find("wlan") != std::string::npos) {
                    return std::string(addressBuffer);
                }
            }
        }
    }
    if (ifAddrStruct != NULL) freeifaddrs(ifAddrStruct);
    return result;
}

std::vector<std::string> *RecalboxSystem::scanBluetooth() {
    std::vector<std::string> *res = new std::vector<std::string>();
    std::ostringstream oss;
    oss << Settings::getInstance()->getString("RecalboxSettingScript") << " " << "hcitoolscan";
    FILE *pipe = popen(oss.str().c_str(), "r");
    char line[1024];

    if (pipe == NULL) {
        return NULL;
    }

    while (fgets(line, 1024, pipe)) {
        strtok(line, "\n");
        res->push_back(std::string(line));
    }
    pclose(pipe);

    return res;
}

bool RecalboxSystem::pairBluetooth(std::string &controller) {
    std::ostringstream oss;
    oss << Settings::getInstance()->getString("RecalboxSettingScript") << " " << "hiddpair" << " " << controller;
    int exitcode = system(oss.str().c_str());
    return exitcode == 0;
}

std::vector<std::string> RecalboxSystem::getAvailableStorageDevices() {

    std::vector<std::string> res;
    std::ostringstream oss;
    oss << Settings::getInstance()->getString("RecalboxSettingScript") << " " << "storage list";
    FILE *pipe = popen(oss.str().c_str(), "r");
    char line[1024];

    if (pipe == NULL) {
        return res;
    }

    while (fgets(line, 1024, pipe)) {
        strtok(line, "\n");
        res.push_back(std::string(line));
    }
    pclose(pipe);

    return res;
}

std::string RecalboxSystem::getCurrentStorage() {

    std::ostringstream oss;
    oss << Settings::getInstance()->getString("RecalboxSettingScript") << " " << "storage current";
    FILE *pipe = popen(oss.str().c_str(), "r");
    char line[1024];

    if (pipe == NULL) {
        return "";
    }

    if (fgets(line, 1024, pipe)) {
        strtok(line, "\n");
        pclose(pipe);
        return std::string(line);
    }
    return "INTERNAL";
}

bool RecalboxSystem::setStorage(std::string selected) {
    std::ostringstream oss;
    oss << Settings::getInstance()->getString("RecalboxSettingScript") << " " << "storage" << " " << selected;
    int exitcode = system(oss.str().c_str());
    return exitcode == 0;
}

bool RecalboxSystem::forgetBluetoothControllers() {
    std::ostringstream oss;
    oss << Settings::getInstance()->getString("RecalboxSettingScript") << " " << "forgetBT";
    int exitcode = system(oss.str().c_str());
    return exitcode == 0;
}

std::string RecalboxSystem::getRootPassword() {

    std::ostringstream oss;
    oss << Settings::getInstance()->getString("RecalboxSettingScript") << " " << "getRootPassword";
    FILE *pipe = popen(oss.str().c_str(), "r");
    char line[1024];

    if (pipe == NULL) {
        return "";
    }

    if (fgets(line, 1024, pipe)) {
        strtok(line, "\n");
        pclose(pipe);
        return std::string(line);
    }
    return oss.str().c_str();
}
