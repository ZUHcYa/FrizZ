// remote.cpp: firmware/remote.py against the virtual CHOMPI (twin/twin.h). The twin's USB MIDI
// sits on a pseudo-terminal in place of the CHOMPI's ALSA node, and remote.py runs as itself
// (--device), while the twin plays at the wall clock's pace: remote.py's SysEx, its parsing of
// the answers and its script player are checked, end to end, against the firmware's.
#include <fcntl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include "check.h"
#include "script.h"
#include "twin.h"

using namespace twin;

static const int kFilterKeyLed = 20;
static const uint32_t kReadyMs = 6000;

static int master_fd = -1;
static std::string slave_path;
static std::string remote_py;
static std::string tmp_dir;

/** The pseudo-terminal: raw, so every byte goes through as it is */
static bool OpenPty()
{
    master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0 || grantpt(master_fd) || unlockpt(master_fd))
        return false;
    slave_path = ptsname(master_fd);
    // kept open, so the terminal stays up between remote.py's runs
    const int slave = open(slave_path.c_str(), O_RDWR | O_NOCTTY);
    termios t;
    if (slave < 0 || tcgetattr(slave, &t))
        return false;
    cfmakeraw(&t);
    tcsetattr(slave, TCSANOW, &t);
    fcntl(master_fd, F_SETFL, O_NONBLOCK);
    return true;
}

/** What remote.py wrote, into the twin's USB; what the twin sent, back to it */
static void Pump()
{
    uint8_t buf[256];
    ssize_t n;
    while ((n = read(master_fd, buf, sizeof(buf))) > 0)
        for (ssize_t i = 0; i < n; i++)
            UsbMidi(buf[i]);
    const std::string out = TakeUsbOut();
    if (!out.empty() && write(master_fd, out.data(), out.size()) < 0)
        perror("pty");
}

/** Runs ms of silence, as fast as the twin goes */
static void RunMs(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t++)
    {
        Pump();
        Run(2, nullptr, nullptr);
    }
}

/** remote.py ARGS, the twin running at the wall clock's pace until it's done (30 s at most).
 *  Its exit code; out gets what it printed, stdout and stderr */
static int Remote(const std::vector<std::string>& args, std::string& out)
{
    int pipe_fd[2];
    if (pipe(pipe_fd))
        return -1;
    const pid_t pid = fork();
    if (pid == 0)
    {
        dup2(pipe_fd[1], 1);
        dup2(pipe_fd[1], 2);
        close(pipe_fd[0]);
        setenv("PYTHONDONTWRITEBYTECODE", "1", 1); // no __pycache__ in the repo
        setenv("FRIZZ_CHOMPI_HELD", "1", 1); // the twin, not the CHOMPI: no lock (tools/chompi.py)
        std::vector<const char*> argv = {"python3", remote_py.c_str(), "--device",
                                         slave_path.c_str()};
        for (const std::string& a : args)
            argv.push_back(a.c_str());
        argv.push_back(nullptr);
        execvp("python3", const_cast<char* const*>(argv.data()));
        _exit(127);
    }
    close(pipe_fd[1]);
    fcntl(pipe_fd[0], F_SETFL, O_NONBLOCK);
    out.clear();
    const auto start = std::chrono::steady_clock::now();
    const uint32_t twin_start = NowMs();
    int status = -1;
    while (true)
    {
        char buf[512];
        ssize_t n;
        while ((n = read(pipe_fd[0], buf, sizeof(buf))) > 0)
            out.append(buf, n);
        if (waitpid(pid, &status, WNOHANG) == pid)
            break;
        const auto wall = std::chrono::steady_clock::now() - start;
        if (wall > std::chrono::seconds(30))
        {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            out += "(timed out)";
            break;
        }
        // one ms of the twin per ms of the wall clock
        if (NowMs() - twin_start < std::chrono::duration_cast<std::chrono::milliseconds>(wall).count())
            RunMs(1);
        else
            std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    char buf[512];
    ssize_t n;
    while ((n = read(pipe_fd[0], buf, sizeof(buf))) > 0)
        out.append(buf, n);
    close(pipe_fd[0]);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static bool Has(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

static std::string Hex(const Rgb& c)
{
    char s[8];
    snprintf(s, sizeof(s), "%02x%02x%02x", c.r, c.g, c.b);
    return s;
}

static std::string Card(const char* path)
{
    auto it = CardFiles().find(path);
    return it == CardFiles().end() ? "" : it->second;
}

static void Write(const std::string& path, const std::string& text)
{
    std::ofstream(path) << text;
}

static std::string Read(const std::string& path)
{
    std::ifstream f(path);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/** An effect's knob in remote.py's scene JSON ("filter": [a, b, c, d]), the index'th */
static float JsonParam(const std::string& json, const char* fx, int index)
{
    const size_t at = json.find("\"" + std::string(fx) + "\": [");
    if (at == std::string::npos)
        return -1.f;
    size_t p = json.find('[', at) + 1;
    for (int i = 0; i < index; i++)
        p = json.find(',', p) + 1;
    return strtof(json.c_str() + p, nullptr);
}

int main()
{
    remote_py = std::string(__FILE__).substr(0, std::string(__FILE__).rfind('/')) + "/../remote.py";
    char dir[] = "/tmp/frizz-remote-XXXXXX";
    tmp_dir = mkdtemp(dir);
    if (!OpenPty())
    {
        Check(false, "remote: a pseudo-terminal for the twin's USB");
        return Finish();
    }
    Boot();
    RunMs(kReadyMs);
    std::string out;

    int rc = Remote({"state"}, out);
    Check(rc == 0 && Has(out, "looper     empty") && Has(out, "knobs on   freezer")
              && Has(out, "tempo      120.0 BPM") && Has(out, "out 0.75, in 0.75"),
          "remote: state reads the play page: no loop, the freezer's knobs, 120 BPM, the gains");
    if (rc != 0)
        printf("%s\n", out.c_str());

    rc = Remote({"leds"}, out);
    const std::string leds = LedLine();
    Check(rc == 0 && out == leds + "\n", "remote: leds prints every LED as the twin's LED log does");
    if (out != leds + "\n")
        printf("      remote: %s      twin:   %s\n", out.c_str(), leds.c_str());

    rc = Remote({"load"}, out);
    Check(rc == 0 && Has(out, "max") && Has(out, "mean"), "remote: load reads the load");

    rc = Remote({"load", "--device", slave_path}, out);
    Check(rc == 0 && Has(out, "max"), "remote: --device after the subcommand counts too");
    if (rc != 0)
        printf("%s\n", out.c_str());

    Remote({"channel", "5"}, out);
    Remote({"transport", "on"}, out);
    rc = Remote({"settings"}, out);
    Check(rc == 0 && out == "channel 5, transport on, clock auto, out off\n",
          "remote: channel and transport set, settings reads them");
    Remote({"channel", "0"}, out);
    rc = Remote({"settings"}, out);
    Check(rc == 0 && out == "channel all, transport on, clock auto, out off\n", "remote: channel 0 is every channel");
    Remote({"source", "usb"}, out);
    rc = Remote({"settings"}, out);
    Check(rc == 0 && out == "channel all, transport on, clock usb, out off\n", "remote: source sets the clock source");
    Remote({"out", "trs"}, out);
    rc = Remote({"settings"}, out);
    Check(rc == 0 && out == "channel all, transport on, clock usb, out trs\n", "remote: out sets MIDI out");
    RunMs(2500);
    Check(Has(Card("/FRIZZ/frizz_master.txt"), "midi_channel 0\nmidi_transport 1\n")
              && Has(Card("/FRIZZ/frizz_master.txt"), "clock_source 2\nmidi_out 1\n"),
          "remote: and they go to the card");

    // MIDI out over USB: its clock comes between the answers, and remote.py reads past it
    Remote({"out", "all"}, out);
    RunMs(200);
    TakeMidiOut();
    rc = Remote({"state"}, out);
    size_t usb_ticks = 0;
    for (const MidiOutByte& b : TakeMidiOut())
        usb_ticks += b.usb && b.byte == 0xF8;
    Check(rc == 0 && Has(out, "looper     empty") && usb_ticks > 0,
          "remote: with MIDI out over USB, its clock comes in between, and state still reads");
    if (rc != 0)
        printf("%s\n", out.c_str());
    Remote({"out", "off"}, out);

    // a scene: the blank one read, the filter closed and latched in it, sent to slot 2
    const std::string blank = tmp_dir + "/blank.json", mine = tmp_dir + "/mine.json",
                      back = tmp_dir + "/back.json";
    rc = Remote({"scene", "get", "0", blank}, out);
    std::string json = Read(blank);
    Check(rc == 0 && Has(json, "\"used\": true") && fabsf(JsonParam(json, "filter", 0) - .5f) < 1e-4f,
          "remote: scene get reads the blank scene, the filter's cutoff at its centre");
    const size_t cut = json.find("\"filter\": [") + 11;
    json.replace(cut, json.find(',', cut) - cut, "0.25");
    const size_t latched = json.find("\"latched\": [") + 12;
    json.insert(latched, "\"filter\"");
    Write(mine, json);
    rc = Remote({"scene", "put", "2", mine}, out);
    RunMs(2500);
    const std::string scenes = Card("/FRIZZ/frizz_scenes.txt");
    Check(rc == 0 && Has(scenes, "scene 2\n") && Has(scenes, "filter 1 250"),
          "remote: scene put stores it in slot 2, on the card");
    if (rc != 0)
        printf("%s\n", out.c_str());
    rc = Remote({"scene", "get", "2", back}, out);
    json = Read(back);
    Check(rc == 0 && fabsf(JsonParam(json, "filter", 0) - .25f) < 1e-4f && Has(json, "\"filter\"\n"),
          "remote: and scene get reads it back, the same within 14 bits");
    rc = Remote({"scene", "put", "0", mine}, out);
    Check(rc != 0 && Has(out, "refused"), "remote: never into the blank scene");
    Check(fabsf(JsonParam(json, "shifter", 4) - 1.f) < 1e-4f,
          "remote: a scene has both pages, the shifter's mix 5th, on its default");
    // a scene from before page 2, four knobs each: page 2 comes from the blank scene
    std::string old = "{\"used\": true, \"latched\": [], \"params\": {";
    for (const char* fx : {"freezer", "shifter", "folder", "crusher", "filter", "flanger",
                           "resonator", "slicer", "warble", "tapestop", "delay", "reverb"})
        old += std::string(fx[0] == 'f' && fx[1] == 'r' ? "" : ", ") + "\"" + fx
               + "\": [0.5, 0.5, 0.5, 0.5]";
    Write(mine, old + "}}");
    rc = Remote({"scene", "put", "3", mine}, out);
    rc |= Remote({"scene", "get", "3", back}, out);
    json = Read(back);
    Check(rc == 0 && fabsf(JsonParam(json, "shifter", 0) - .5f) < 1e-4f
              && fabsf(JsonParam(json, "shifter", 4) - 1.f) < 1e-4f,
          "remote: a scene of four knobs each puts page 2 on its defaults");

    // a script played on the device: the filter's key held, its LED expected as the twin
    // shows it held and let go, with the load
    const std::string off = Hex(SmtLedFull(kFilterKeyLed));
    Press("KEY_5", true);
    RunMs(300);
    const std::string on = Hex(SmtLedFull(kFilterKeyLed));
    Press("KEY_5", false);
    RunMs(300);
    const std::string script = tmp_dir + "/script.txt";
    Write(script, "input sine 220 0.3\nbooted\nat 100\ndown KEY_5\nwait 300\nexpect led smt 20 " + on +
                      "\nup KEY_5\nwait 300\nexpect led smt 20 " + off + "\n");
    rc = Remote({"play", script, "--cpu"}, out);
    Check(rc == 0 && on != off && Has(out, "skipped (twin only): input") && Has(out, "load: worst max"),
          "remote: play holds a key over SysEx, its LED as expected, and reports the load");
    if (rc != 0)
        printf("%s\n", out.c_str());
    Write(script, "booted\ndown KEY_5\nwait 300\nexpect led smt 20 " + off + "\nup KEY_5\n");
    rc = Remote({"play", script}, out);
    Check(rc == 1 && Has(out, "line 4: smt 20 is " + on + ", not " + off),
          "remote: and an LED that isn't as expected fails the run, telling which");
    rc = Remote({"state"}, out);
    Check(rc == 0 && Has(out, "knobs on   filter"), "remote: the script's key selected the filter");

    // the mode switch over SysEx (kCmdSwitch): up shows the settings page, and SysEx keys
    // reach it as the hand's do; `switch hand` gives the real switch back
    Remote({"switch", "up"}, out);
    rc = Remote({"state"}, out);
    Check(rc == 0 && Has(out, "page       settings; the mode switch stands down, SysEx holds it up"),
          "remote: switch up shows the settings page, and state says so");
    Write(script, "booted\ntap KEY_18\nwait 100\n"); // F#: mono
    rc = Remote({"play", script}, out);
    const std::string refused = out;
    Remote({"state"}, out);
    Check(rc != 0 && Has(refused, "line 2: tap KEY_18 on the settings page") && Has(refused, "--force")
              && !Has(out, ", mono"),
          "remote: play refuses a key on the settings page the device shows, saying why, and sends none (#58)");
    if (rc == 0 || !Has(refused, "--force"))
        printf("%s\n", refused.c_str());
    Remote({"play", script, "--force"}, out);
    Remote({"switch", "hand"}, out);
    rc = Remote({"state"}, out);
    Check(rc == 0 && Has(out, ", mono") && Has(out, "page       play; the mode switch stands down\n"),
          "remote: a SysEx key there sets mono; switch hand, the play page again");
    // a script's toggle: up and down over SysEx, the real switch again at its end
    Write(script, "booted\ntoggle 1\nwait 100\ntap KEY_18\nwait 100\ntoggle 0\nwait 100\n");
    rc = Remote({"play", script}, out);
    Check(rc != 0 && Has(out, "line 4: tap KEY_18 on the settings page"),
          "remote: and one the script toggles up to, by the line (#58)");
    rc = Remote({"play", script, "--force"}, out);
    const int played = rc;
    rc = Remote({"state"}, out);
    Check(played == 0 && rc == 0 && !Has(out, ", mono") && Has(out, "page       play; the mode switch stands down\n"),
          "remote: play's toggle 1 / toggle 0 reach the settings page (mono off again), the real switch after");
    if (!Has(out, "page       play"))
        printf("%s\n", out.c_str());
    // the settings page only looked at plays; a setting over SysEx doesn't
    Write(script, "booted\ntoggle 1\nwait 100\ntoggle 0\nwait 100\ntap KEY_18\n");
    rc = Remote({"play", script}, out);
    Check(rc == 0, "remote: a script that toggles up and down again, no key there, plays");
    if (rc != 0)
        printf("%s\n", out.c_str());
    Write(script, "booted\nmidi F0 7D 43 48 13 00 05 F7\n");
    rc = Remote({"play", script}, out);
    const std::string refused_sysex = out;
    Remote({"settings"}, out);
    Check(rc != 0 && Has(refused_sysex, "line 2: a setting over SysEx") && Has(out, "channel all,"),
          "remote: and a script that sets the channel over SysEx is refused, the channel kept");

    unlink(blank.c_str());
    unlink(mine.c_str());
    unlink(back.c_str());
    unlink(script.c_str());
    rmdir(tmp_dir.c_str());
    return Finish();
}
