#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <time.h>
#include <thread>
#include <unistd.h>

// Discord RPC
#include "discord/discord.h"

using namespace std;
namespace fs = filesystem;

/*
 * Change with your own app's id if you made one
 * Default: 934099338374824007
 */
#define DISCORD_APP_ID 1334839099227635772
#define VERSION "3.0"

long uptime();
unsigned long cpuUsage();
long memoryUsage();

volatile bool shouldExit;
string quoteBucket;
string activityBucket;

// distro names in /etc/lsb_release
map<string, string> distros_lsb = {{"Arch|Artix", "archlinux"},
                                   {"LinuxMint", "lmint"},
                                   {"Gentoo", "gentoo"},
                                   {"Ubuntu", "ubuntu"},
                                   {"ManjaroLinux", "manjaro"}};
// same but in /etc/os-release (fallback)
map<string, string> distros_os = {{"Arch Linux", "archlinux"},
                                  {"Linux Mint", "lmint"},
                                  {"Gentoo", "gentoo"},
                                  {"Ubuntu", "ubuntu"},
                                  {"Manjaro Linux", "manjaro"}};
string helpMsg = string("Usage:\n") + " rpcpp [options]\n\n" + "Options:\n" +
                 " --interval=3           rich presence update interval in second(s)\n" +
                 " -h, --help             display this help and exit\n" +
                 " -v, --version          output version number and exit";

regex processRegex("\\/proc\\/\\d+\\/cmdline");
regex updateRegex("^interval=(\\d+)$");

vector<pair<regex, string>> distros_lsb_regex = {};
vector<pair<regex, string>> distros_os_regex = {};

regex memavailr("MemAvailable: +(\\d+) kB");
regex memtotalr("MemTotal: +(\\d+) kB");

struct DiscordState {
    discord::User currentUser;
    unique_ptr<discord::Core> core;
};

struct InfoAsset {
    string image;
    string text;
};

struct Config {
    int interval = 3;
    bool printHelp = false;
    bool printVersion = false;
};

Config config;

discord::Activity g_activity{};

inline __attribute__((always_inline))
void setActivity(DiscordState &state, string *sstate)
{
    g_activity.SetState(sstate->c_str());
    state.core->ActivityManager().UpdateActivity(g_activity, [](discord::Result result) {});
}

void setQuote(DiscordState &state, string *details)
{
    g_activity.SetDetails(details->c_str());
    state.core->ActivityManager().UpdateActivity(g_activity, [](discord::Result result) {});
}

void parseConfigOption(Config *config, char *option, bool arg)
{
    smatch matcher;
    string s = option;

    if (arg) {
        if (s == "-h" || s == "--help") {
            config->printHelp = true;
            return;
        }

        if (s == "-v" || s == "--version") {
            config->printVersion = true;
            return;
        }

        if (!strncmp(option, "--", 2)) {
            s = s.substr(2, s.size() - 2);
        }
    }

    if (regex_search(s, matcher, updateRegex)) {
        config->interval = stoi(matcher[1]);
        return;
    }
}

void parseConfig(string configFile, Config *config)
{
    ifstream file(configFile);
    if (file.is_open()) {
        string line;
        while (getline(file, line)) {
            parseConfigOption(config, (char *)line.c_str(), false);
        }
        file.close();
    }
}

/**
 * @brief Parse default configs
 * /etc/rpcpp/config < ~/.config/rpcpp/config
 */
void parseConfigs()
{
    char *home = getenv("HOME");
    if (!home) {
        parseConfig("/etc/rpcpp/config", &config);
        return;
    }

    string configFile = string(home) + "/.config/rpcpp/config";
    parseConfig(configFile, &config);
    if (ifstream(configFile).fail()) {
        parseConfig("/etc/rpcpp/config", &config);
    }
}

void parseArgs(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        parseConfigOption(&config, argv[i], true);
    }
}

string getDistro()
{
    string distro = "";
    string line;
    ifstream release;
    regex distroreg;
    smatch distromatcher;
    if (fs::exists("/etc/lsb-release")) {
        distroreg = regex("DISTRIB_ID=\"?([a-zA-Z0-9 ]+)\"?");
        release.open("/etc/lsb-release");
    } else if (fs::exists("/etc/os-release")) {
        distroreg = regex("NAME=\"?([a-zA-Z0-9 ]+)\"?");
        release.open("/etc/os-release");
    } else {
        return distro;
    }
    while (getline(release, line)) {
        if (regex_search(line, distromatcher, distroreg)) {
            distro = distromatcher[1];
            break;
        }
    }
    return distro;
}

InfoAsset getDistroAsset(string d)
{
    InfoAsset dist{};
    dist.text = d + " / rpcpp ";
    dist.image = "tux";

    for (const auto &kv : distros_lsb_regex) {
        regex r = kv.first;
        smatch m;
        if (regex_match(d, m, r)) {
            dist.image = kv.second;
            break;
        }
    }
    if (dist.image == "tux") {
        for (const auto &kv : distros_os_regex) {
            regex r = kv.first;
            smatch m;
            if (regex_match(d, m, r)) {
                dist.image = kv.second;
                break;
            }
        }
    }

    return dist;
}

/**
 * @brief Compile strings to regular expressions
 */
void compileRegexes(map<string, string> *from, vector<pair<regex, string>> *to)
{

    for (const auto &kv : *from) {
        const regex r = regex(kv.first);
        to->push_back({r, kv.second});
    }
}

/**
 * @brief Compile all strings to regular expressions
 */
void compileAllRegexes()
{
    compileRegexes(&distros_lsb, &distros_lsb_regex);
    compileRegexes(&distros_os, &distros_os_regex);
}

void *updateRPC(void *ptr)
{
    DiscordState *state = (struct DiscordState *)ptr;

    while (true) {
        activityBucket = "CPU: " + to_string(cpuUsage()) + "% | " +
                         "RAM: " + to_string(memoryUsage() >> 10) + "MB";
        setActivity(*state, &activityBucket);
        this_thread::sleep_for(chrono::seconds(config.interval));
    }

    return nullptr;
}

void *updateQuote(void *ptr)
{
    DiscordState *state = (struct DiscordState *)ptr;
    static char buffer[256]{};
    static const char *cmd = "curl -s https://zenquotes.io/api/random | jq -r '.[] | \"\\(.q) — \\(.a)\"'";

    sleep(3);
    while (true) {
        FILE *f = popen(cmd, "r");
        if (!f) {
            this_thread::sleep_for(chrono::seconds(30));
            continue;
        }

        if (fgets(buffer, sizeof(buffer), f))
            quoteBucket = buffer;

        pclose(f);

        if (quoteBucket.empty()) {
            this_thread::sleep_for(chrono::seconds(30));
            continue;
        } else {
            if (quoteBucket.back() == '\n')
                quoteBucket.pop_back();
        }

        setQuote(*state, &quoteBucket);
        this_thread::sleep_for(chrono::minutes(30));
    }

    return nullptr;
}

int main(int argc, char **argv)
{
    parseConfigs();
    parseArgs(argc, argv);

    if (config.printHelp) {
        cout << helpMsg << endl;
        exit(0);
    }
    if (config.printVersion) {
        cout << "RPC++ version " << VERSION << endl;
        exit(0);
    }

    auto shouldWait{[](const char *name) -> int {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "pgrep -if %s", name);
        FILE *f{popen(cmd, "r")};
        if (!f)
            return true;
        char buffer[16];
        fgets(buffer, sizeof(buffer), f);
        pclose(f);
        return buffer[0] == '\0';
    }};

    while (shouldWait("discord") && shouldWait("vesktop"))
        sleep(60);
    sleep(5);

    compileAllRegexes();

    DiscordState state{};

    discord::Core *core{};
    auto result = discord::Core::Create(DISCORD_APP_ID, DiscordCreateFlags_Default, &core);
    state.core.reset(core);
    if (!state.core) {
        cout << "Failed to instantiate discord core! (err " << static_cast<int>(result) << ")\n";
        exit(-1);
    }

    InfoAsset distroAsset = getDistroAsset(getDistro());
    g_activity.GetAssets().SetLargeImage(distroAsset.image.c_str());
    g_activity.GetAssets().SetLargeText(distroAsset.text.c_str());
    g_activity.GetAssets().SetSmallImage("");
    g_activity.GetAssets().SetSmallText("");
    g_activity.GetTimestamps().SetStart(static_cast<long>(time(nullptr) - uptime()));
    g_activity.SetType(discord::ActivityType::Playing);

    pthread_t updateRPC_thread;
    pthread_create(&updateRPC_thread, 0, updateRPC, ((void *)&state));

    pthread_t updateQuote_thread;
    pthread_create(&updateQuote_thread, 0, updateQuote, ((void *)&state));

    signal(SIGINT, [](int) { shouldExit = true; });
    signal(SIGPIPE, [](int) { shouldExit = true; });
    signal(SIGTERM, [](int) { shouldExit = true; });

    do {
        state.core->RunCallbacks();
        sleep(config.interval);
    } while (!shouldExit);

    cout << "Exiting..." << endl;

    pthread_kill(updateRPC_thread, 9);
    pthread_kill(updateQuote_thread, 9);

    return 0;
}

long uptime()
{
    static char buffer[256]{};
    long ret{};
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        if (fgets(buffer, sizeof(buffer), f))
            ret = atol(buffer);
        fclose(f);
    }
    return ret;
}

long memoryUsage()
{
    long ramtotal{}, ramavail{};
    smatch matcher;
    string line;
    ifstream meminfo;
    meminfo.open("/proc/meminfo");
    while (getline(meminfo, line)) {
        if (ramtotal && ramavail)
            break;
        else if (regex_search(line, matcher, memavailr))
            ramavail = stoi(matcher[1]);
        else if (regex_search(line, matcher, memtotalr))
            ramtotal = stoi(matcher[1]);
    }
    meminfo.close();

    return ramtotal - ramavail;
}

unsigned long cpuUsage()
{
    static unsigned long userP = 0, niceP = 0, systemP = 0, idleP = 0;
    unsigned long user, nice, system, idle;

    FILE *file = fopen("/proc/stat", "r");
    if (!file) return 0;
    fscanf(file, "cpu %llu %llu %llu %llu", &user, &nice, &system, &idle);
    fclose(file);

    unsigned long totalDiff = (user - userP) + (nice - niceP) +
                              (system - systemP) + (idle - idleP);

    unsigned long usage = totalDiff ? 100 * ((user - userP) + (nice - niceP) +
                          (system - systemP)) / totalDiff : 0;

    userP = user;
    niceP = nice;
    systemP = system;
    idleP = idle;

    return usage;
}
