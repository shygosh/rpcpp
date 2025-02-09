#include "rpcpp.hpp"

/*
 * Change with your own app's id if you made one.
 * default: 934099338374824007
 */
#define APP_ID  (1334839099227635772)

DistroAsset distroAsset = {"fedora", "Fedora Linux"};
DistroAsset windowAsset = {"kwin", "KWin"};

void *updateRPC(void *ptr)
{
    DiscordState *state = (struct DiscordState *)ptr;
    startTime = time(0) - ms_uptime();
    wm = string(wm_info(disp));

    log("Distro: " + distroAsset.text, LogType::DEBUG);
    log("WM: " + wm, LogType::DEBUG);
    log("Starting RPC loop.", LogType::DEBUG);

    while (true)
    {
        setActivity(*state, string("CPU: " + to_string(getCPU()) + "% | " +
                                   "RAM: " + to_string(getRAM()) + "% | " +
                                   "D: " + to_string(getNetRX() >> 20) + "MB | "
                                   "U: " + to_string(getNetTX() >> 20) + "MB"),
                    "WM: " + wm, windowAsset.image, windowAsset.text, distroAsset.image, distroAsset.text,
                    startTime, discord::ActivityType::Playing);
        sleep(config.updateInterval);
    }
}

int main(int argc, char **argv)
{
    parseConfigs();
    parseArgs(argc, argv);

    if (config.printHelp)
    {
        cout << helpMsg << endl;
        exit(0);
    }

    if (config.printVersion)
    {
        cout << "RPC++ version " << VERSION << endl;
        exit(0);
    }

    const char* isRunning = "pidof -s Discord > /dev/null";
    while (system(isRunning))
    {
        log("Waiting for Discord to run.", LogType::INFO);
        sleep(60);
    }
    /*
     * Corner-case issue:
     * Once its running, wait for a few seconds for it to fully loaded.
     */
    sleep(7);

    DiscordState state{};
    discord::Core *core{};
    auto result = discord::Core::Create(APP_ID, DiscordCreateFlags_Default, &core);
    state.core.reset(core);
    if (!state.core)
    {
        cout << "Failed to instantiate discord core! (err " << static_cast<int>(result)
             << ")\n";
        exit(-1);
    }
    log("Connected to Discord.", LogType::INFO);

    disp = XOpenDisplay(NULL);
    if (!disp)
    {
        cout << "Can't open display" << endl;
        return -1;
    }
    // this is kinda dumb to do since it shouldn't be anything else other than 11, but whatever
    log("Xorg version " + to_string(XProtocolVersion(disp)), LogType::DEBUG);

    static int (*old_error_handler)(Display *, XErrorEvent *);
    trapped_error_code = 0;
    old_error_handler = XSetErrorHandler(error_handler);

    pthread_t updateThread;
    pthread_create(&updateThread, 0, updateRPC, ((void *)&state));
    log("Threads started.", LogType::DEBUG);

    if (config.debug)
    {
        state.core->SetLogHook(
            discord::LogLevel::Debug, [](discord::LogLevel level, const char *message)
            { cerr << "Log(" << static_cast<uint32_t>(level) << "): " << message << "\n"; });
    }

    signal(SIGINT, [](int) { should_exit = true; });
    signal(SIGPIPE, [](int) { should_exit = true; });
    signal(SIGTERM, [](int) { should_exit = true; });

    do
    {
        state.core->RunCallbacks();
        this_thread::sleep_for(chrono::seconds(1));
    } while (!should_exit);

    cout << "Exiting..." << endl;
    XCloseDisplay(disp);
    pthread_kill(updateThread, 9);

    return 0;
}
