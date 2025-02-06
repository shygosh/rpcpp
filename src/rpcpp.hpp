#include <iostream>
#include <csignal>
#include <thread>
#include <unistd.h>
#include <time.h>
#include <regex>
#include <fstream>
#include <filesystem>
#include <climits>

// Discord RPC
#include "discord/discord.h"

// X11 libs
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

// variables
#define VERSION "2.2.0"

namespace
{
    volatile bool should_exit{false};
}
namespace fs = std::filesystem;
using namespace std;

Display *disp;
long startTime = 0;
static int trapped_error_code = 0;
string wm;
string helpMsg = string("Usage:\n") +
                 " rpcpp [options]\n\n" +
                 "Options:\n" +
                 " --debug                print debug messages\n" +
                 " --update-interval=N    update interval in second(s)\n" +
                 " -h, --help             display this help and exit\n" +
                 " -v, --version          output version number and exit";

// regular expressions

regex memavailr("MemAvailable: +(\\d+) kB");
regex memtotalr("MemTotal: +(\\d+) kB");
regex processRegex("\\/proc\\/\\d+\\/cmdline");
regex updateRegex("^update-interval=(\\d+)$");

struct DiscordState
{
    discord::User currentUser;

    unique_ptr<discord::Core> core;
};

struct DistroAsset
{
    string image;
    string text;
};

struct Config
{
    bool debug = false;
    int updateInterval = 1;
    bool printHelp = false;
    bool printVersion = false;
};

Config config;

// local imports

#include "logging.hpp"
#include "wm.hpp"

// methods

static int error_handler(Display *display, XErrorEvent *error)
{
    trapped_error_code = error->error_code;
    return 0;
}

string lower(string s)
{
    transform(s.begin(), s.end(), s.begin(),
              [](unsigned char c)
              { return tolower(c); });
    return s;
}

void setActivity(DiscordState &state, string details, string sstate, string smallimage, string smallimagetext, string largeimage, string largeimagetext, long uptime, discord::ActivityType type)
{
    discord::Activity activity{};
    activity.SetDetails(details.c_str());
    activity.SetState(sstate.c_str());
    activity.GetAssets().SetSmallImage(smallimage.c_str());
    activity.GetAssets().SetSmallText(smallimagetext.c_str());
    activity.GetAssets().SetLargeImage(largeimage.c_str());
    activity.GetAssets().SetLargeText(largeimagetext.c_str());
    activity.GetTimestamps().SetStart(uptime);
    activity.SetType(type);

    state.core->ActivityManager().UpdateActivity(activity, [](discord::Result result)
                { if(config.debug) log(string((result == discord::Result::Ok) ? "Succeeded" : "Failed") + " updating activity!", LogType::DEBUG); });
}

long ms_uptime(void)
{
    FILE *in = fopen("/proc/uptime", "r");
    long retval = 0;
    char tmp[256] = {0x0};
    if (in != NULL)
    {
        fgets(tmp, sizeof(tmp), in);
        retval = atof(tmp);
        fclose(in);
    }
    return retval;
}

static unsigned long long
lastTotalUser = 0, lastTotalUserLow = 0, lastTotalSys = 0,
lastTotalIdle = 0, lastPercent = 0;

unsigned long long getCPU(void)
{
    unsigned long long totalUser, totalUserLow, totalSys, totalIdle, total, percent;
    FILE *file = fopen("/proc/stat", "r");
    if (!file) return lastPercent;
    if (fscanf(file, "cpu %llu %llu %llu %llu", &totalUser, &totalUserLow, &totalSys, &totalIdle) != 4) {
        fclose(file);
        return lastPercent;
    }
    fclose(file);

    total = (totalUser - lastTotalUser) +
            (totalUserLow - lastTotalUserLow) +
            (totalSys - lastTotalSys) +
            (totalIdle - lastTotalIdle);

    if (total == 0) return 0;

    percent = ((totalUser - lastTotalUser) +
               (totalUserLow - lastTotalUserLow) +
               (totalSys - lastTotalSys)) * 100 / total;

    lastTotalUser = totalUser;
    lastTotalUserLow = totalUserLow;
    lastTotalSys = totalSys;
    lastTotalIdle = totalIdle;

    return lastPercent = percent;
}

long getRAM(void)
{
    long total = 0, available = 0;
    smatch matcher;
    string line;
    ifstream meminfo;
    meminfo.open("/proc/meminfo");
    while (getline(meminfo, line))
    {
        if (regex_search(line, matcher, memavailr))
            available = stoi(matcher[1]);
        if (regex_search(line, matcher, memtotalr))
            total = stoi(matcher[1]);
        if (total && available)
            break;
    }
    meminfo.close();

    return (total - available) * 100 / total;
}

unsigned long getNetRX(void)
{
    FILE *file = nullptr;
    unsigned long RX = 0, total = 0;
    file = fopen("/sys/class/net/eno1/statistics/rx_bytes", "r");
    if (file)
    {
        fscanf(file, "%lu", &RX);
        fclose(file);
        file = nullptr;
    }
    total += RX;
    file = fopen("/sys/class/net/wlo1/statistics/rx_bytes", "r");
    if (file)
    {
        fscanf(file, "%lu", &RX);
        fclose(file);
        file = nullptr;
    }
    total += RX;
    return total;
}

unsigned long getNetTX(void)
{
    FILE *file = nullptr;
    unsigned long TX = 0, total = 0;
    file = fopen("/sys/class/net/eno1/statistics/tx_bytes", "r");
    if (file)
    {
        fscanf(file, "%lu", &TX);
        fclose(file);
        file = nullptr;
    }
    total += TX;
    file = fopen("/sys/class/net/wlo1/statistics/tx_bytes", "r");
    if (file)
    {
        fscanf(file, "%lu", &TX);
        fclose(file);
        file = nullptr;
    }
    total += TX;
    return total;
}

void parseConfigOption(Config *config, char *option, bool arg)
{
    smatch matcher;
    string s = option;

    if (arg)
    {
        if (s == "-h" || s == "--help")
        {
            config->printHelp = true;
            return;
        }

        if (s == "-v" || s == "--version")
        {
            config->printVersion = true;
            return;
        }

        if (s == "--debug")
        {
            config->debug = true;
            return;
        }

        if (!strncmp(option, "--", 2))
            s = s.substr(2, s.size() - 2);
    }

    if (regex_search(s, matcher, updateRegex))
    {
        config->updateInterval = stoi(matcher[1]);
        return;
    }
}

void parseConfig(string configFile, Config *config)
{
    ifstream file(configFile);
    if (file.is_open())
    {
        string line;
        while (getline(file, line))
            parseConfigOption(config, (char *)line.c_str(), false);
        file.close();
    }
}

/**
 * @brief Parse default configs
 * /etc/rpcpp/config < ~/.config/rpcpp/config
 */
void parseConfigs(void)
{
    char *home = getenv("HOME");
    if (!home)
    {
        parseConfig("/etc/rpcpp/config", &config);
        return;
    }

    string configFile = string(home) + "/.config/rpcpp/config";
    parseConfig(configFile, &config);
    if (ifstream(configFile).fail())
        parseConfig("/etc/rpcpp/config", &config);
}

void parseArgs(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        parseConfigOption(&config, argv[i], true);
}
